/**
UNIX Shell Project

Sistemas Operativos
Grados I. Informatica, Computadores & Software
Dept. Arquitectura de Computadores - UMA

Some code adapted from "Fundamentos de Sistemas Operativos", Silberschatz et al.

To compile and run the program:
   $ gcc Shell_project.c job_control.c -o Shell
   $ ./Shell
	(then type ^D to exit program)

**/

#include "job_control.h"   // remember to compile with module job_control.c
#include "parse_redir.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_LINE 256 /* 256 chars per line, per command, should be enough. */

//creamos la lista de trabajos:
job *lista_jobs = NULL;

//HANDLER PARA SIGCHLD

void handler_chld(int s){
	int pid;
	int wstatus;

	while(1){
		pid = waitpid(-1, &wstatus, WNOHANG | WUNTRACED | WCONTINUED);
		if (pid == -1) return;
		if (pid == 0){
			return;
		}
		job *actualizar_trabajo = get_item_bypid(lista_jobs, pid);
		//actualizamos el estado
		if(WIFEXITED(wstatus)){
			printf("Proceso Exited: %d, signal: %d\n", pid, WEXITSTATUS(wstatus));
			delete_job(lista_jobs, actualizar_trabajo);
		} else if (WIFSIGNALED(wstatus)){
			printf("Proceso terminado por señal: %d, signal: %d\n", pid, WTERMSIG(wstatus));
			delete_job(lista_jobs, actualizar_trabajo);
		} else if (WIFSTOPPED(wstatus)){
			printf("Proceso Stopped: %d, signal: %d\n", pid, WSTOPSIG(wstatus));
			actualizar_trabajo->state = STOPPED;
		} else if (WIFCONTINUED(wstatus)){
			printf("Proceso Continuado: %d\n", pid);
			actualizar_trabajo->state = BACKGROUND;
		}
	}
}

void handler_hup(int s){
	FILE *fp;
    fp=fopen("hup.txt","a"); // abre un fichero en modo 'append'
	if (fp == NULL){
		perror("Ha habido un error al intentar abrir el fichero con fopen");
		exit(1);
	}
    fprintf(fp, "SIGHUP recibido.\n"); //escribe en el fichero
	fclose(fp);
}

// -----------------------------------------------------------------------
//                            MAIN
// -----------------------------------------------------------------------

int main(void)
{
	char inputBuffer[MAX_LINE]; /* buffer to hold the command entered */
	int background;             /* equals 1 if a command is followed by '&' */
	char *args[MAX_LINE/2];     /* command line (of 256) has max of 128 arguments */
	// probably useful variables:
	int pid_fork, pid_wait; /* pid for created and waited process */
	int status;             /* status returned by wait */
	enum status status_res; /* status processed by analyze_status() */
	int info;				/* info processed by analyze_status() */
	lista_jobs = new_list("lista_jobs");

	signal(SIGCHLD, handler_chld); // Se trata y maneja la señal SIGCHLD
	signal(SIGHUP, handler_hup);
	ignore_terminal_signals(); //ignoramos todas las señales del terminal como ctrl + C, por ejemplo.
	while (1)   /* Program terminates normally inside get_command() after ^D is typed*/
	{
		printf("COMMAND->");
		fflush(stdout);
		get_command(inputBuffer, MAX_LINE, args, &background);  /* get next command */

		char *file_in, *file_out;
		parse_redirections(args, &file_in, &file_out);

		if(args[0]==NULL) continue;   // if empty command


		//Meter el comando cd:
		if (strcmp(args[0], "cd") == 0){

			if (args[1] == NULL){
				chdir("/");
			} else if (chdir(args[1]) != 0) {
				printf("Error al intentar cambiar de directorio %s\n", args[1]);
			}
			continue;
		}
		if(strcmp(args[0], "jobs") == 0){
			mask_signal(SIGCHLD, SIG_BLOCK);
			print_list(lista_jobs, print_item);
			mask_signal(SIGCHLD, SIG_UNBLOCK);
			continue;
		}
		if(strcmp(args[0], "fg") == 0){
			int n_fg;
			mask_signal(SIGCHLD, SIG_BLOCK);
			if(args[1] == NULL){
				n_fg = 1;
			} else {
				n_fg = atoi(args[1]);
			}
			if (n_fg > 0){
				job *trabajo = get_item_bypos(lista_jobs, n_fg);
				if (trabajo == NULL){
					perror("Se está intentando mandar a fg una tarea que no está en la lista_jobs");
					mask_signal(SIGCHLD, SIG_UNBLOCK);
					continue;
				}
				if(tcsetpgrp(STDIN_FILENO, trabajo->pgid) == -1){
					perror("Error al asignar el terminal al grupo de procesos");
					mask_signal(SIGCHLD, SIG_UNBLOCK);
					continue;
				}
				trabajo->state = FOREGROUND;
				killpg(trabajo->pgid, SIGCONT);

				/*Ahora el proceso padre que ha mandado al grupo de trabajo a la terminal, debe esperar a que termine*/
				pid_wait = waitpid(-trabajo->pgid, &status, WUNTRACED);
				if (pid_wait < 0){
					perror("Fallo en el waitpid que espera el padre");
					mask_signal(SIGCHLD, SIG_UNBLOCK);
					continue;
				}
				if(tcsetpgrp(STDIN_FILENO, getpid()) == -1){
					perror("Error al ceder la terminal al padre");
					mask_signal(SIGCHLD, SIG_UNBLOCK);
					continue;
				}
				status_res = analyze_status(status, &info);
				switch (status_res){
					case EXITED:
						printf("Foreground pid: %d,Command: %s, Exited, info: %d\n", pid_wait,trabajo->command, info);
						if(delete_job(lista_jobs, trabajo) == 0){
							fprintf(stderr, "Error al intentar eliminar el job\n");
							mask_signal(SIGCHLD, SIG_UNBLOCK);
							continue;
						}
					case SIGNALED:
						printf("Foreground pid: %d, Command: %s, Signaled, info: %d\n", pid_wait,trabajo->command, info);
						if(delete_job(lista_jobs, trabajo) == 0)
						{
							fprintf(stderr,"Error eliminando el job.\n");
							mask_signal(SIGCHLD, SIG_UNBLOCK);
							continue;
						}
					case SUSPENDED:
						printf("Foreground pid: %d,Command: %s, Stopped, info: %d\n", pid_wait, trabajo->command, info);
						trabajo->state = STOPPED;
				}
			} else {
				perror("El argumento pasado es incorrecto");
			}
			mask_signal(SIGCHLD, SIG_UNBLOCK);
			continue;
		}
		if(strcmp(args[0], "bg") == 0){
			int n_bg;
			mask_signal(SIGCHLD, SIG_BLOCK);
			if (args[1] == NULL){
				n_bg = 1;
			} else {
				n_bg = atoi(args[1]);
			}
			if (n_bg > 0){
				job *trabajo = get_item_bypos(lista_jobs, n_bg);
				if (trabajo == NULL){
					perror("Se ha intentado obtener un trabajo que no existe en la lista_jobs");
					mask_signal(SIGCHLD, SIG_UNBLOCK);
					continue;
				}
				if(trabajo->state != STOPPED){
					fprintf(stderr, "El trabajo está siendo ejecutado (no está suspendido)");
					mask_signal(SIGCHLD, SIG_UNBLOCK);
					continue;
				}
				trabajo->state = BACKGROUND;
				killpg(trabajo->pgid, SIGCONT); //Le decimos al grupo de procesos que se reanuden
				printf("Background job running... pid: %d, command: %s\n", trabajo->pgid, trabajo->command);
			} else {
				perror("Se ha pasado un argumento que es incorrecto");
			}
			mask_signal (SIGCHLD, SIG_UNBLOCK);
			continue;
		}

		if(strcmp(args[0], "currjob") == 0){
			job *trabajo = get_item_bypos(lista_jobs, 1);
			if (trabajo == NULL){
				printf("No hay trabajo actual");
				continue;
			}
			printf("Trabajo actual: PID=%d command=%s", trabajo->pgid, trabajo->command);
			continue;
		}
		/* the steps are:
			 (1) fork a child process using fork()
			 (2) the child process will invoke execvp()
			 (3) if background == 0, the parent will wait, otherwise continue
			 (4) Shell shows a status message for processed command
			 (5) loop returns to get_commnad() function
		*/
		pid_fork = fork();
		if (pid_fork == 0){ //Child
			restore_terminal_signals();
			if(setpgid(0,0) != 0){//El 0 del primer parámetro hace que la función afecte al proceso que la ejecuta, el segundo 0 dice que uses el pgid que hayas puesto como primer parámetro
				perror("Ha habido un error al establecer el grupo (setpgid)");
				exit(EXIT_FAILURE);
			}
			if (background == 0){
				if(tcsetpgrp(STDIN_FILENO, getpid()) == -1){ // Le cedemos el terminal al hijo
					perror("Error al dar la terminal al proceso");
					exit(EXIT_FAILURE);
				}
			}
			if (file_in){
				int in_fd = open(file_in, O_RDONLY);
				if (in_fd < 0){
					perror("Error en la apertura del archivo");
					continue;
				}
				if(dup2(in_fd, STDIN_FILENO) == -1){
					perror("Error al copiar la entrada");
					close(in_fd);
					continue;
				}
			}
			if(file_out){
				int out_fd = open(file_out, O_WRONLY | O_TRUNC | O_CREAT, 0644);
				if(out_fd < 0){
					perror("Error al intentar abrir el archivo");
					continue;
				}
				if(dup2(out_fd, STDOUT_FILENO) == -1){
					perror("Error al copiar la salida en el archivo");
					close(out_fd);
					continue;
				}
				close(out_fd);
			}

			execvp(args[0], args);
			fprintf(stderr, "Error, command not found %s\n", args[0]);
			exit(EXIT_FAILURE);
		} else if (pid_fork < 0) { //Error
			perror("Ha habido un error en la llamada a fork()\n");
			exit(EXIT_FAILURE);
		} else { //pid_fork > 0 //Padre
			if (background == 0){
				#ifdef DEBUG
				printf("El proceso se ejecuta en primer plano, esperar al hijo\n");
				#endif
				if((tcsetpgrp(STDIN_FILENO, pid_fork)) == -1){
					perror("Error al dar la terminal al hijo desde el padre");
					continue;
				}
				pid_wait = waitpid(pid_fork, &status, WUNTRACED);
				if (pid_wait == -1){
					perror("Fallo en el wait");
					continue;
				}
				if(tcsetpgrp(STDIN_FILENO, getpid()) == -1){
					perror("Error dando la terminal al padre");
					continue;
				}
				status_res = analyze_status(status, &info);
				switch (status_res) {
					case SUSPENDED:
						printf("Foreground pid: %d, command: %s, Stopped, info: %d\n", pid_fork, args[0], info);
						mask_signal(SIGCHLD, SIG_BLOCK);
						job *nuevo_trabajo = new_job(pid_fork, args[0], STOPPED); //preparar el job que lleva su pid, comando y estado
						add_job(lista_jobs, nuevo_trabajo); //añadir en lista de jobs
						mask_signal(SIGCHLD, SIG_UNBLOCK);
						break;
					case CONTINUED:
						printf("Foreground pid: %d, command: %s, Continued, info: %d\n", pid_fork, args[0], info);
						break;
					case SIGNALED:
						printf("Foreground pid: %d, command: %s, Signaled, info: %d\n", pid_fork, args[0], info);
						break;
					case EXITED:
						printf("Foreground pid: %d, command: %s, Exited, info: %d\n", pid_fork, args[0], info);
						break;
				}
			} else {
				printf("Background job running... pid: %d, command: %s\n", pid_fork, args[0]);
				mask_signal(SIGCHLD, SIG_BLOCK);
				job *nuevo_trabajo = new_job(pid_fork, args[0], BACKGROUND);
				add_job(lista_jobs, nuevo_trabajo);
				mask_signal(SIGCHLD, SIG_UNBLOCK);
			}
		}
	} // end while
}
