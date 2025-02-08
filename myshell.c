#include <stdio.h>
#include <unistd.h>
#include <wait.h>
#include <string.h>
#include <stdlib.h>
#include "parser.h"
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

typedef struct {
    pid_t *pids;           //lista de pids del job
    int num_pids;          //numero de pids en el job
    char command[4096];  //Comando ejecutado
    int estado;          //Indicador de si el trabajo sigue en ejecución o no
} job;

char *PROMPT = "msh> ";
job *jobs; //puntero de jobs
int stopped, num_jobs, max_jobs, file_des_in, file_des_out, file_des_err, fd_screen, fd_keyboard;
int num_pids_fg;
pid_t pid;
pid_t *pidhijos;
int **pipes;
char line_text[4096];

void liberarMemoriaJobs(int from, int to, job *arr_jobs){
    int i;
    for (i = from; i < to; i++){
        free(arr_jobs[i].pids); //Liberamos la memoria reservada para el array de pids por cada job
    }
    free(arr_jobs); //Liberamos la memoria reservada para el array de jobs
}

void liberarMemoriaPipesyPids(int to, int **arr_pipes, pid_t *arr_pids){
    int i;
    for (i = 0; i < to; i++){
        free(arr_pipes[i]);
    }
    free(arr_pipes);
    free(arr_pids);
}

void restaurarDescriptores(int file_des_in, int file_des_out, int file_des_err, int fdk, int fds){
    if(file_des_in != -1){
        dup2(fdk, STDIN_FILENO); //Redirigimos la entrada estandar al teclado, cuyo descriptor de fichero habíamos guardado en la variable fd_keyboard
    }

    if(file_des_out != -1){
        dup2(fds, STDOUT_FILENO); //Redirigimos la salida estandar a la pantalla, cuyo descriptor de fichero habíamos guardado en la variable fd_screen
    }

    if (file_des_err != -1){
        dup2(fds, STDERR_FILENO); //Redirigimos la salida de errores a la pantalla, cuyo descriptor de fichero habíamos guardado en la variable fd_screen
    }
}

void print_jobs(int n_jobs, job *arr_jobs) {
char *estado;
int i;
    for (i = 0; i < n_jobs; i++) {
        if (arr_jobs[i].estado == 1){
            estado = "Running";
        }
        else if(arr_jobs[i].estado == 2){
            estado = "Stopped";
        }
        else{
            estado = "Done";
        }
        printf("[%d]   %s\t\t%s", i + 1, estado, arr_jobs[i].command); //i el job sigue en ejecución o detenido lo imprime
    }
}

void revisar_jobs(int flag_jobs, int n_jobs, job *arr_jobs) {
    int i,j,fin;
    pid_t result;
    for (i = 0; i < n_jobs; i++) {
        if (arr_jobs[i].estado == 1) { //Por cada tarea corriendo
            fin = 1;
            for (j = 0; j < arr_jobs[i].num_pids; j++){ //Recorremos todos los procesos de una línea
                result = waitpid(arr_jobs[i].pids[j], NULL, WNOHANG); //Esperamos por todos los hijos de manera no bloqueante
                if (result == 0){
                    fin = 0;  //si uno de los procesos no ha terminado, fin vale 0.
                }
            }
            if (fin) { //Si todos los procesos de una linea han terminado
                arr_jobs[i].estado = 0; //La tarea i se pone como acabada
                if (!flag_jobs){ //Si se ha llamado la función con esa variable en 0, se muestra que el proceso ha terminado y si no (esto ocurre cuando se revisa justo antes de imprimir los jobs) no se muestra esta línea
                    printf("Proceso terminado [%d]\t\t%s\n", i + 1, arr_jobs[i].command);
                }
            }
        }
    }
}

void manejador_sigint(){ //Manejador para la señal SIGINT
    int i;
    restaurarDescriptores(file_des_in, file_des_out, file_des_err, fd_keyboard, fd_screen); //Para que stdin apunte al teclado y stdout y stderr a la pantalla
    printf("\n");
    if (num_pids_fg == 0){
        printf("%s", PROMPT);
        fflush(stdout);
    }
    else{
        for (i = 0; i < num_pids_fg; i++){
            if (kill(pidhijos[i], SIGTERM) != 0){ //Tratamos de mandar la señal SIGTERM
                // a todos los procesos hijos. ¡¡¡¡¡IMPORTANTE!!!!! VER MEMORIA PARA COMPRENDER
                // POR QUÉ NO SE ENVÍA SIGINT A CADA HIJO (Memoria, apartado 1, algoritmos utilizados)
                fprintf(stderr, "No se pudo finalizar el proceso %d : %s\n", i, strerror(errno));
                return;
            }
        }
    }
}

void manejador_sigtstp(){ //Manejador para la señal SIGTSTP
    int i, all_stopped;
    restaurarDescriptores(file_des_in, file_des_out, file_des_err, fd_keyboard, fd_screen); //Para que stdin apunte al teclado y stdout y stderr a la pantalla
    printf("\n");
    if (num_pids_fg == 0){ //Si se ha hecho control Z con una línea vacía
        printf("%s", PROMPT);
        fflush(stdout);
    }
    else{
        for (i = 0; i < num_pids_fg; i++){
            all_stopped = kill(pidhijos[i], SIGSTOP) == 0; //Tratamos de mandar la señal SIGSTOP
                // a todos los procesos hijos. ¡¡¡¡¡IMPORTANTE!!!!! VER MEMORIA PARA COMPRENDER
                // POR QUÉ NO SE ENVÍA SIGTSTP A CADA HIJO (Memoria, apartado 1, algoritmos utilizados)
        }

        if (all_stopped) { //Si todos los procesos hijos han sido recibido la señal SIGTSTP correctamente
            if (num_jobs == max_jobs){ //Si el array de jobs ya está lleno
                max_jobs += 5;
                job *new_jobs = realloc(jobs, max_jobs * sizeof(job)); //Hacemos un realloc para reservar más memoria
                if (!new_jobs) {
                    fprintf(stderr, "Error al redimensionar jobs: %s\n", strerror(errno));
                    liberarMemoriaPipesyPids(num_pids_fg - 1, pipes, pidhijos);
                    liberarMemoriaJobs(0, max_jobs - 5, jobs); //Liberamos la memoria del array jobs desde la posición 0 hasta la posición max_jobs - 5 (las que hay ahora mismo completas)
                    exit(-1);
                }
                jobs = new_jobs; //Si la reserva de memoria fue exitosa, ahora jobs apunta a la nueva memoria reservada con más espacio
            }
            jobs[num_jobs].pids = pidhijos;
            jobs[num_jobs].num_pids = num_pids_fg;
            strncpy(jobs[num_jobs].command, line_text, sizeof(jobs[num_jobs].command) - 1);
            jobs[num_jobs].command[sizeof(jobs[num_jobs].command) - 1] = '\0';
            jobs[num_jobs].estado = 2; // Estado detenido
            num_jobs++;
            printf("[%d]   Stopped\t\t%s", num_jobs, line_text);
            num_pids_fg = 0;
            stopped = 1;
        } else {
            fprintf(stderr, "Error. La tarea no pudo deternerse\n"); //Alguno de los procesos hijos no ha recibido bien la señal SIGTSTP
            return;
        }
    }
}

int main(){
    tline *line;
    int i, j, encontrado, reanudado, no_exist_command, current_mask;
    char *dir, *err_conv, *arg;
    long octal_mask;

    num_jobs = 0;
    max_jobs = 128;
    file_des_in = -1;
    file_des_out = -1;
    file_des_err = -1;

    signal(SIGINT, manejador_sigint); //Asignamos el manejador a la señal SIGINT (control c)
    signal(SIGTSTP, manejador_sigtstp); //Asignamos el manejador a la señal SIGTSTP (control z)

    fd_screen = dup(STDOUT_FILENO); //Guardamos el descriptor de fichero de la pantalla
    fd_keyboard = dup(STDIN_FILENO); //Guardamos el descriptor de fichero del teclado

    jobs = (job *) malloc(max_jobs * sizeof(job)); //Reservamos memoria para los jobs
    if (!jobs){
        fprintf(stderr, "Fallo en la reserva de memoria para los jobs: %s\n", strerror(errno));
        return -1;
    }
    stopped = 0; //El proceso, por defecto, no está parado

while(1){

        num_pids_fg = 0;
        revisar_jobs(0, num_jobs, jobs); //Revisamos si ha acabado alguna tarea
        restaurarDescriptores(file_des_in, file_des_out, file_des_err, fd_keyboard, fd_screen); //Para que stdin apunte al teclado y stdout y stderr a la pantalla
        printf("%s", PROMPT); //Mostramos el prompt
        
        if (fgets(line_text, sizeof(line_text), stdin) != NULL && strcmp(line_text, "\n") != 0){ //Si hay linea

            line = tokenize(line_text); //Dividimos la linea con la función tokenize proporcionada
            if (!line){
                fprintf(stderr, "Hay un error en la línea de comando\n");
                continue;
            }

            tcommand *commands = line->commands; //Guarda el array de tcommands por cada linea

            for (i = 0; i < line->ncommands; i++){
                no_exist_command = commands[i].filename == NULL;
            }
        
            if (line->redirect_input != NULL){ //En caso de que en la linea se haya introducido fichero de redireccion de entrada
                file_des_in = open(line->redirect_input, O_RDONLY); //Tratamos de abrir el fichero de redireccion de entrada en modo solo lectura
            
                if (file_des_in == -1){ //Si la apertura del fichero dio error
                    fprintf(stderr, "Fallo al abrir el fichero de redirección de entrada \"%s\"\n", line->redirect_input);
                    continue; //Volvemos a la primera linea del bucle while
                }
            
                else{
                    dup2(file_des_in, STDIN_FILENO); //Redirigimos la entrada estandar al fichero indicado
                    close(file_des_in);
                }
            }

            if (line->redirect_output != NULL){ //En caso de que en la linea se haya introducido fichero de redireccion de salida
                file_des_out = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0664); //Tratamos de abrir el fichero de redireccion de entrada en modo solo escritura, indicando esas flags para que, en caso de no existir el fichero, se cree uno con ese nombre y permisos rw-rw-r--
            
                if (file_des_out == -1){
                    fprintf(stderr, "Fallo al abrir el fichero de redirección de salida \"%s\"\n", line->redirect_output);
                    continue;
                }
            
                else{
                    dup2(file_des_out, STDOUT_FILENO); //Redigirimos la salida estandar al fichero indicado
                    close(file_des_out);
                }
            }

            if (line->redirect_error != NULL){ //En caso de que en la linea se haya introducido fichero de redireccion de error
                file_des_err = open(line->redirect_error, O_WRONLY | O_CREAT | O_TRUNC, 0664); //Tratamos de abrir el fichero de redireccion de entrada en modo solo escritura, indicando esas flags para que, en caso de no existir el fichero, se cree uno con ese nombre y permisos rw-rw-r--
            
                if (file_des_err == -1){ //Si hay fallo en la apertura del fichero indicado
                    fprintf(stderr, "Fallo al abrir el fichero de redirección de error \"%s\"\n", line->redirect_error);
                    continue;
                }

                else{
                    dup2(file_des_err, STDERR_FILENO); //Redirigimos la salida de errores al fichero indicado
                    close(file_des_err);
                }
            }
            num_pids_fg = line->ncommands;

//COMANDO CD     
            if (line->ncommands == 1 && strcmp(commands[0].argv[0], "cd") == 0){
                if (commands[0].argc > 2){
                    fprintf(stderr, "Demasiados argumentos para el comando cd\n");
                    continue;
                }

                else if (commands[0].argc == 2){
                    dir = line->commands[0].argv[1];           
                }
            
            else{
                dir = getenv("HOME");
                if (dir == NULL){
                    fprintf(stderr, "No existe la variable de entorno $HOME\n");
                }
            }

            if (chdir(dir) != 0){ //Tratamos de cambiar de directorio
                fprintf(stderr, "Falló el cambio de directorio : %s\n", strerror(errno));
            }
        }

//COMANDO UMASK
            else if (line->ncommands == 1 && strcmp(commands[0].argv[0], "umask") == 0){
                if (commands[0].argc > 2){
                    fprintf(stderr, "Error: Uso umask [mascara]\n");
                }
                else if(commands[0].argc == 2){
                    octal_mask = strtol(commands[0].argv[1], &err_conv, 8); //Convertimos el argumento a octal
                    if (*err_conv != '\0' || atoi(commands[0].argv[1]) < 0){ //Comprobamos que se convirtió bien y que no era un numero negativo o similares
                        fprintf(stderr, "La máscara no es un numero octal válido\n");
                    }
                    else{
                        umask(octal_mask); //Cambiamos la máscara
                    }
                }
                else{ //Si no hay argumentos, queremos mostrar la máscara actual
                    current_mask = umask(0); 
                    umask(current_mask); 
                    printf("%04o\n",current_mask);
                }
            }

//COMANDO JOBS
            else if (line->ncommands == 1 && strcmp(commands[0].argv[0], "jobs") == 0) {
                revisar_jobs(1, num_jobs, jobs);
                print_jobs(num_jobs, jobs);
            }

//COMANDO BG
            else if (line->ncommands == 1 && strcmp(commands[0].argv[0], "bg") == 0) {
                if (commands[0].argc > 2){
                    fprintf(stderr, "Error. Uso: bg <id_job>\n");
                    continue;
                }
                else if(commands[0].argc == 1){
                    i = num_jobs - 1;
                    encontrado = 0;
                    while ((!encontrado) && (i >= 0)){
                        encontrado = jobs[i].estado == 2; //Comprobamos si está detenido
                        i--;
                    }
                    i += 1;
                    if (!encontrado){ //Si no hay ninguna tarea para reanudar
                        fprintf(stderr, "No hay ninguna tarea para reanudar\n");
                        continue;
                    }
                }
                else {
                    arg = commands[0].argv[1];
                    if (arg[0] == '%') { //Quitamos el % en caso de que se haya llamado con %id
                        i = atoi(arg + 1) - 1; // Ajustamos indice a base 0
                    } 
                    else {
                        i = atoi(arg) - 1; // Sin '%' convertir normalmente
                    }

                    if (num_jobs == 0){
                        printf("No hay tareas en jobs\n");
                        continue;
                    }

                    else if (i < 0 || i >= num_jobs) {
                        fprintf(stderr, "El número de tarea a reanudar debe estar entre 1 y %d\n", num_jobs);
                        continue;
                    }

                    else if (jobs[i].estado == 0) {
                        fprintf(stderr, "La tarea [%d] ya está finalizada\n", i + 1); 
                        continue;
                    }
                    else if (jobs[i].estado == 1) {
                        fprintf(stderr, "La tarea [%d] ya se está ejecutando en background\n", i + 1); 
                        continue;    
                    }
                }
                for (j = 0; j < jobs[i].num_pids; j++){
                    reanudado = kill(jobs[i].pids[j], SIGCONT) == 0;
                    if (!reanudado){
                        fprintf(stderr, "Error al reanudar un proceso: %s\n", strerror(errno));
                    }
                }
                if (reanudado){
                    jobs[i].command[strlen(jobs[i].command) - 1] = '\0';
                    char *cat = " &\n"; //Para ahora aclarar que se está ejecutando en background 
                    strcat(jobs[i].command, cat);
                    printf("[%d]   %s", i + 1, jobs[i].command);
                    jobs[i].estado = 1;
                }
                else{
                    fprintf(stderr, "Error al reanudar la tarea\n");
                }
            }

//COMANDO EXIT

            else if(line->ncommands == 1 && strcmp(commands[0].argv[0], "exit") == 0){
                liberarMemoriaJobs(0, num_jobs, jobs);
                break;
            }

            else if(no_exist_command){
                fprintf(stderr, "Error: Comando no encontrado\n");
            }
        
            else{
                pidhijos = (pid_t *) malloc (line->ncommands * sizeof(pid_t)); //Comprobar reservas de memoria
                if (!pidhijos){
                    fprintf(stderr, "Falló la reserva de memoria para el array de pids: %s\n", strerror(errno));
                    continue;
                }
                pipes = (int **) malloc (((line->ncommands) - 1) * sizeof(int *));
                if(!pipes){
                    fprintf(stderr, "Falló la reserva de memoria para el array de pipes: %s\n", strerror(errno));
                    free(pidhijos);
                    continue;
                }
                for (i = 0; i < (line->ncommands) - 1; i++){
                    pipes[i] = (int *) malloc (2 * sizeof(int));

                    if (pipe(pipes[i]) < 0){ //Si hay fallo al crear la tuberia i
                        fprintf(stderr, "Fallo al crear uno de los pipes\n%s\n", strerror(errno)); 
                        break;
                    }
                }
                if (i != line->ncommands - 1){
                    liberarMemoriaPipesyPids(i, pipes, pidhijos);
                }
                //Comenzamos a hacer forks, cerrar pipes y redirigir entradas y salidas entre hijos
                for (i = 0; i < (line->ncommands); i++){
                    pid = fork(); //Creamos un proceso hijo
                    if (pid < 0){
                        fprintf(stderr, "Error al crear uno de los procesos\n");
                    }
                    else if (pid == 0){ //Proceso hijo 
                        signal(SIGINT, SIG_IGN);
                        signal(SIGTSTP, SIG_IGN);
                        for (j = 0; j < i - 1; j++){ //Cerramos extremo de lectura y de escritura de los pipes que están por detras del proceso i (sin contar el pipe cuyo extremo de lectura queremos redirigir como stdin del proceso i)
                            close(pipes[j][0]);
                            close(pipes[j][1]);
                        }

                        for (j = i + 1; j < (line->ncommands) - 1; j++){ //Cerramos extremo de lectura y de escritura de los pipes que están por delante del proceso i (sin contar el pipe cuyo extremo de escritura queremos redirigir como stdout del proceso i)
                            close(pipes[j][0]);
                            close(pipes[j][1]);
                        }

                        if (i != 0){ //Si no es el primer proceso hijo
                            close(pipes[i - 1][1]); //Cerramos el extremo de escritura del pipe cuyo extremo de lectura sera la entrada estandar de este proceso
                            dup2(pipes[i - 1][0], STDIN_FILENO); //Redirigimos la salida de modo que ahora el descriptor de entrada estandar apunta a el extremo de lectura del pipe inmediatamente anterior        
                        }

                        if (i != (line->ncommands) - 1){ //Si no es el último proceso hijo
                            close(pipes[i][0]); //Cerramos el extremo de lectura del pipe cuyo extremo de escrutura será la entrada estándar del proceso i
                            dup2(pipes[i][1], STDOUT_FILENO); //Redirigimos la salida de modo que ahora el descriptor de salida estandar apunta a el extremo de escritura del pipe inmediatamente posterior
                        }
                        execvp(commands[i].filename, commands[i].argv); //Ejecutamos el comando
                        fprintf(stderr, "Error al ejecutar el comando\n");
                        liberarMemoriaPipesyPids(line->ncommands - 1, pipes, pidhijos);
                        liberarMemoriaJobs(0, num_jobs, jobs);
                    }
                    else{
                        pidhijos[i] = pid; //Guardamos el pid del proceso que acabamos de crear
                    }
                }

                for (i = 0; i < (line->ncommands) - 1; i++){ //El padre cierra extremo de lectura y escritura de todos los pipes pues no usa ninguno
                    close(pipes[i][0]);
                    close(pipes[i][1]); 
                }

                if (line->background) {
                    printf("[%d] %d\n", num_jobs, pidhijos[line->ncommands - 1]); //Solo mostramos el pid del ultimo proceso
                    if (num_jobs == max_jobs){ //Si el array de jobs ya está lleno
                        max_jobs += 5;
                        job *new_jobs = realloc(jobs, max_jobs * sizeof(job)); //Hacemos un realloc para reservar más memoria
                        if (!new_jobs) {
                            fprintf(stderr, "Error al redimensionar jobs: %s\n", strerror(errno));
                            liberarMemoriaJobs(0, max_jobs - 5, jobs); //Liberamos la memoria del array jobs desde la posición 0 hasta la posición max_jobs - 5 (las que hay ahora mismo completas)
                            liberarMemoriaPipesyPids(line->ncommands - 1, pipes, pidhijos);
                            return -1;
                        }
                        jobs = new_jobs; //Si la reserva de memoria fue exitosa, ahora jobs apunta a la nueva memoria reservada con más espacio
                    }
                    jobs[num_jobs].pids = pidhijos; //apunto al array de pids
                    jobs[num_jobs].num_pids = line->ncommands; //guardo el numero de pids
                    strncpy(jobs[num_jobs].command, line_text, sizeof(jobs[num_jobs].command) - 1); //guardo el comando en el job
                    jobs[num_jobs].estado = 1; //pongo el comando en activo
                    num_jobs++;
                    }

                else{
                    for (i = 0; i < line->ncommands; i++) {
                        waitpid(pidhijos[i], NULL, WUNTRACED); // Espera a cada hijo para ver si ha acabado o ha sido parado
                    }
                    if (!stopped){ //Si no hubo que crear un job nuevo debido al envío de control Z por teclado
                        free(pidhijos); //Liberamos la memoria reservada para los pids de los procesos hijos
                    }
                }

                for (i = 0; i < (line->ncommands) - 1; i++){
                    free(pipes[i]); //Liberamos la memoria reservada para cada pipe
                }

                free(pipes); //Liberamos la memoria reservada para el array de pipes
            }
        }
        else if (strcmp(line_text, "\n") != 0){
            printf("^D detectado. Ejecutando exit()\n");
            liberarMemoriaJobs(0, num_jobs, jobs);
            break;
        }
    }
    return 0;
}
