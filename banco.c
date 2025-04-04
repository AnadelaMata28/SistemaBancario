#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>
#include "init_cuentas.h"
#include "estructura.h"

//Toda esta parte de código es para leer el archivo "config.txt"
typedef struct Config {
   int limite_retiro;
   int limite_transferencia;
   int umbral_retiros;
   int umbral_transferencias;
   int num_hilos;
   char archivo_cuentas[50];
   char archivo_log[50];
} Config;

int semid; //Semáforo para controlar el acceso a las cuentas de los usuarios
int pipe_padre_hijo[2]; //Tubería para la comunicación de los procesos padres con los procesos hijos

Config leer_configuracion(const char *ruta) {
	FILE *archivo = fopen(ruta, "r");

	if (archivo == NULL) {
        	perror("Error al abrir config.txt");
	        exit(1);
   	}

        Config config;
        char linea[100];
        while (fgets(linea, sizeof(linea), archivo)) {
		if (linea[0] == '#' || strlen(linea) < 3) continue; // Ignorar comentarios y líneas vacías
		if (strstr(linea, "LIMITE_RETIRO")) sscanf(linea, "LIMITE_RETIRO=%d", &config.limite_retiro);
		else if (strstr(linea, "LIMITE_TRANSFERENCIA")) sscanf(linea, "LIMITE_TRANSFERENCIA=%d", &config.limite_transferencia);
		else if (strstr(linea, "UMBRAL_RETIROS")) sscanf(linea, "UMBRAL_RETIROS=%d", &config.umbral_retiros);
		else if (strstr(linea, "UMBRAL_TRANSFERENCIAS")) sscanf(linea, "UMBRAL_TRANSFERENCIAS=%d", &config.umbral_transferencias);
		else if (strstr(linea, "NUM_HILOS")) sscanf(linea, "NUM_HILOS=%d", &config.num_hilos);
		else if (strstr(linea, "ARCHIVO_CUENTAS")) sscanf(linea, "ARCHIVO_CUENTAS=%s", config.archivo_cuentas);
		else if (strstr(linea, "ARCHIVO_LOG")) sscanf(linea, "ARCHIVO_LOG=%s", config.archivo_log);
	}

	fclose(archivo);
	return config;}

void registrar_operacion_log(const char *ruta_log, Operacion op) {
	FILE *log = fopen(ruta_log, "a");
	if (!log) {
        	perror("Error al abrir archivo de log");
	        return;
        }

        time_t t = time(NULL);
        struct tm *tm_info = localtime(&t);
        char fecha[30];
        strftime(fecha, sizeof(fecha), "%Y-%m-%d %H:%M:%S", tm_info);

        fprintf(log, "[%s] Tipo: %s | Cuenta origen: %d", fecha, op.tipo_operacion, op.cuenta_origen);
        if (strcmp(op.tipo_operacion, "transferencia") == 0)
    	    fprintf(log, " -> destino: %d", op.cuenta_destino);
        if (strcmp(op.tipo_operacion, "consulta") != 0)
    	    fprintf(log, " | Monto: %.2f", op.monto);
	fprintf(log, "\n");
	fclose(log);
}

int main(){
	//Se lee la configuración del archivo "config.txt"
	Config configuracion = leer_configuracion("./config.txt");
        printf("Archivo de cuentas: %s\n", configuracion.archivo_cuentas);

        printf("Número máximo de hilos: %d\n", configuracion.num_hilos);

	pthread_t hilos[configuracion.num_hilos];

	//Se abre el archivo de cuentas
	FILE *archivo = fopen(configuracion.archivo_cuentas, "rb+");
	if (archivo == NULL){
		perror("Error al abrir el archivo de cuentas\n");
		exit(1);
	}

	//Se crea la tubería
	if (pipe(pipe_padre_hijo) == -1){
		perror("Error al crear la tubería\n");
		exit(1);
	}

	//Se crea el semáforo
	key_t key = ftok("archivo_clave", 'A');
	semid = semget(key, 1, IPC_CREAT | 0666);
	if (semid == -1){
		perror("Error al crear el semáforo");
		exit(1);
	}

	semctl(semid, 0, SETVAL, 1); //Se inicializa el semáforo a 1

	//Se crea un proceso hijo para manejar el menú del usuario
	pid_t pid = fork();
	if (pid == 0) {
	        // Proceso hijo
	        close(pipe_padre_hijo[0]);
        	char fd_str[10];
	        sprintf(fd_str, "%d", pipe_padre_hijo[1]);
        	execl("./usuario", "usuario", fd_str, NULL); 
	        perror("Error al ejecutar usuario");
        	exit(1);
	} else if (pid < 0) {
	        perror("Error al crear el proceso hijo\n");
        	exit(1);
	} else {
	        // Proceso padre
        	close(pipe_padre_hijo[1]); // Cerramos escritura
    
		Operacion op;
        	while (read(pipe_padre_hijo[0], &op, sizeof(op)) > 0) { // Recibimos datos
	        	printf("\n[PADRE] Operación recibida: %s | Cuenta: %d", op.tipo_operacion, op.cuenta_origen);
		        if (strcmp(op.tipo_operacion, "transferencia") == 0)
                		printf(" -> %d", op.cuenta_destino);
		        if (strcmp(op.tipo_operacion, "consulta") != 0)
                		printf(" | Monto: %.2f", op.monto);
		        printf("\n");
		        registrar_operacion_log(configuracion.archivo_log, op); 
			if (op.monto > configuracion.limite_retiro){
           			printf("Error: Retiro excede el límite permitido (%d)\n", configuracion.limite_retiro);
		        }
        	}
    	}

	//Se cierra la lectura del proceso padre
	close(pipe_padre_hijo[0]);

	//Esperamos a que el proceso hijo acabe
	wait(NULL);

	//Se cierra el semáforo
	if (semctl(semid, 0, IPC_RMID) == -1){
		perror("Error al eliminar el semáforo");
		exit(1);
	}

	return 0;
}
