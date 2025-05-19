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
#include <semaphore.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/msg.h> // Colas de mensajes
#include "init_cuentas.h"
#include "estructura.h"
#include "memoria.h"

#define MAX_USUARIOS 5
sem_t *sem_usuarios;
sem_t *sem_banco;									  // Semáforo para controlar el acceso a las cuentas de los usuarios
int pipe_banco_usuario[MAX_USUARIOS][2];			  // Tubería para la comunicación del proceso padre (banco) con el proceso hijo (usuario)
int pipe_banco_monitor[2], npipes = 0;				// Tubería para la comunicación del proceso padre (banco) con el proceso hijo (monitor)
char mensaje[31]="";
pid_t pidMonitor;

typedef struct Config
{
	int limite_retiro;
	int limite_transferencia;
	int umbral_retiros;
	int umbral_transferencias;
	int num_hilos;
	char archivo_cuentas[50];
	char archivo_log[50];
} Config;
Config configuracion;

Config leer_configuracion(const char *ruta)
{
	FILE *archivo = fopen(ruta, "r");
	if (archivo == NULL)
	{
		perror("Error al abrir config.txt");
		exit(1);
	}

	Config config;
	char linea[100];
	while (fgets(linea, sizeof(linea), archivo))
	{
		if (linea[0] == '#' || strlen(linea) < 3)
			continue; // Ignorar comentarios y líneas vacías
		if (strstr(linea, "LIMITE_RETIRO"))
			sscanf(linea, "LIMITE_RETIRO=%d", &config.limite_retiro);
		else if (strstr(linea, "LIMITE_TRANSFERENCIA"))
			sscanf(linea, "LIMITE_TRANSFERENCIA=%d", &config.limite_transferencia);
		else if (strstr(linea, "UMBRAL_RETIROS"))
			sscanf(linea, "UMBRAL_RETIROS=%d", &config.umbral_retiros);
		else if (strstr(linea, "UMBRAL_TRANSFERENCIAS"))
			sscanf(linea, "UMBRAL_TRANSFERENCIAS=%d", &config.umbral_transferencias);
		else if (strstr(linea, "NUM_HILOS"))
			sscanf(linea, "NUM_HILOS=%d", &config.num_hilos);
		else if (strstr(linea, "ARCHIVO_CUENTAS"))
			sscanf(linea, "ARCHIVO_CUENTAS=%s", config.archivo_cuentas);
		else if (strstr(linea, "ARCHIVO_LOG"))
			sscanf(linea, "ARCHIVO_LOG=%s", config.archivo_log);
	}

	fclose(archivo);
	return config;
}

void registrar_operacion_log(const char *ruta_log, Operacion op)
{
	FILE *log = fopen(ruta_log, "a");
	if (!log)
	{
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
	{
		fprintf(log, " | Monto: %.2f", op.monto);
		fprintf(log, " | Monitor: %s", op.mensaje);
	}
	fprintf(log, "\n");
	fclose(log);
}

void final_pipes()
{
	int status;
	// Matamos el proceso de Monitor
	kill(pidMonitor,SIGTERM);
	printf("La función de finalización ha sido llamada.\n");

	wait(NULL);
	for (int i = 0; i < npipes; i++)
	{
		// Proceso padre
		close(pipe_banco_usuario[i][1]); // Cerramos escritura
		Operacion op;
		while (read(pipe_banco_usuario[i][0], &op, sizeof(op)) > 0)
		{ // Recibimos datos
			printf("\n[PADRE] Operación recibida: %s | Cuenta: %d", op.tipo_operacion, op.cuenta_origen);
			if (strcmp(op.tipo_operacion, "transferencia") == 0)
				printf(" -> %d", op.cuenta_destino);
			if (strcmp(op.tipo_operacion, "consulta") != 0)
			{
				printf(" | Monto: %.2f", op.monto);
				printf(" | Monitor: %s", op.mensaje);
			}
			printf("\n");
			registrar_operacion_log(configuracion.archivo_log, op);
		}
		// Se cierra la lectura del proceso padre
		close(pipe_banco_usuario[i][0]);
	}

	close(pipe_banco_monitor[1]); // Cerramos escritura
	while (read(pipe_banco_monitor[0], &mensaje, sizeof(mensaje)) > 0)
	{
		printf("%s\n", mensaje);
	}
	close(pipe_banco_monitor[0]);

	/* Esperamos a que se acaben todos los procesos hijos,
	visualizamos por pantalla los procesos hijos con su estado de salida y su pid,
	no sería necesario realizar esta acción pero lo llegamos a cabo para comprobar que todo se cierre bien */
	pid_t wpid;
	while ((wpid = waitpid(-1, &status, 0)) > 0)
	{
		if (WIFEXITED(status))
		{
			printf("Proceso hijo %d terminó con estado %d\n", wpid, WEXITSTATUS(status));
		}
	}

	printf("Todos los procesos hijos han terminado.\n");
	// Se cierra y se elimina el semáforo
	sem_close(sem_banco);
	sem_unlink("/sem_banco");
	sem_close(sem_usuarios);
	sem_unlink("/sem_usuarios");

	exit(0);
}

void maneja_ctrl_c(int senial)
{
	// Cuando se detecta el Ctrl^C cerramos el programa
	printf("\n");
	exit(0);
}

int main()
{
	int salida =1;

	// Controlamos si el usuario introduce en algún momento durante la ejecución un Ctrl^C
	// Si lo detectamos llamamos a la función maneja_ctrl_c
	signal(SIGINT, maneja_ctrl_c);
	// Antes de cerrar, se llamará a la función final_pipes que se encargará de liberar y cerrar todos los recursos
	atexit(final_pipes);

	// Se crea el semáforo
	char titular[50], limite_retiro[10], umbral_retiros[10], limite_transferencia[10], umbral_transferencias[10], fd_str[10], nCuenta[10], sshm_id[10];
	int nusers = 0, encontrado = 0, status, numCuenta;
	struct Cuenta cuenta;
	int i = 0;

	// Creamos un semáforo para controlar que no supere los 5 usuarios simultaneos
	sem_usuarios = sem_open("/sem_usuarios", O_CREAT, 0644, MAX_USUARIOS);
	if (sem_usuarios == SEM_FAILED)
	{
		perror("Error al crear el semáforo de usuarios");
		exit(1);
	}
	// Semáforo para el archivo de cuentas
	sem_banco = sem_open("/sem_banco", O_CREAT, 0644, 1);
	if (sem_banco == SEM_FAILED)
	{
		perror("Error al crear el semáforo");
		exit(1);
	}

	// Se lee la configuración del archivo "config.txt"
	// Config configuracion = leer_configuracion("./config.txt");
	configuracion = leer_configuracion("./config.txt");

	int shm_id = crear_memcompartida();
	if (shm_id == -1) 
	{
		perror("Error al crear la memoria compartida");
		exit(1);
	}

	TablaCuentas* tabla = asociar_memcompartida(shm_id);
	sem_wait(sem_banco);
	// Volvemos al inicio del archivo de cuentas, porque sigue abierto el archivo
	FILE *archivoCuentas = fopen(configuracion.archivo_cuentas, "rb"); // Abrimos el archivo en modo lectura binaria con "rb", ya que unicamente queremos leer datos
	if (archivoCuentas == NULL)
	{
		perror("Error al abrir el archivo de cuentas");
		sem_post(sem_banco);
		liberar_memcompartida(tabla, shm_id);
		return 0;
	}

	tabla->num_cuentas = 0;
	
	fseek(archivoCuentas, 0L, SEEK_SET);
	while (fread(&cuenta, sizeof(struct Cuenta), 1, archivoCuentas))
	{
		tabla->cuentas[i] = cuenta;
		tabla->num_cuentas++;
		i++;
	}

	fclose(archivoCuentas);
	sem_post(sem_banco);

	printf("Archivo de cuentas: %s\n", configuracion.archivo_cuentas);

	printf("Número máximo de hilos: %d\n", configuracion.num_hilos);


	// MONITOR
	// Se crea la tubería
	if (pipe(pipe_banco_monitor) == -1)
	{
		perror("Error al crear la tubería\n");
		exit(1);
	}

	// Se crea un proceso hijo para manejar el proceso hijo monitor
	pidMonitor = fork();
	if (pidMonitor == 0)
	{
		// Proceso hijo
		close(pipe_banco_monitor[0]);
		// Convertimos las variables int en char* para poder pasarlas como argumento en el execlp
		sprintf(fd_str, "%d", pipe_banco_monitor[1]);
		sprintf(umbral_retiros, "%d", configuracion.umbral_retiros);
		sprintf(umbral_transferencias, "%d", configuracion.umbral_transferencias);
		execlp("./monitor", "./monitor", fd_str, umbral_retiros, umbral_transferencias, NULL);
		perror("Error al ejecutar monitor");
		exit(1);
	}
	else if (pidMonitor < 0)
	{
		perror("Error al crear el proceso hijo\n");
		exit(1);
	}

	// USUARIO
	while (salida == 1)
	{
		printf("Introduzca el titular de la cuenta (o 'S' para salir): ");
		// Controlamos que solo haya 5 usuarios ejecutándose
		sem_wait(sem_usuarios);
		// Limpiamos el buffer 
		fflush(stdin);

		fgets(titular, 50, stdin);
		titular[strcspn(titular, "\n")] = '\0';
		if (strcmp(titular, "S")==0 || strcmp(titular,"s")== 0)
		{
			break;
		}

		encontrado = 0;
		for (i = 0; i < tabla->num_cuentas; i++)
		{
			titular[strcspn(titular, "\n")] = '\0';
			if (strcasecmp(tabla->cuentas[i].titular, titular) == 0)
			{
				printf("Número de cuenta: %d\n", tabla->cuentas[i].numero_cuenta);
				numCuenta = tabla->cuentas[i].numero_cuenta;
				encontrado = 1;
				nusers++;
				break;
			}
		}
		// Si no encontramos el usuario hacemos un sem_post para incrementar el semáforo y que podamos seguir introduciendo el mismo número de usuarios
		if (encontrado == 0)
		{
			printf("\nUsuario no encontrado\n");
			sem_post(sem_usuarios);
			continue;
		}
		// Se crea la tubería
		if (pipe(pipe_banco_usuario[npipes]) == -1)
		{
			perror("Error al crear la tubería\n");
			exit(1);
		}
		npipes++;
		// Se crea un proceso hijo para manejar el menú del usuario
		pid_t pidUsuario = fork();
		if (pidUsuario == 0)
		{
			// Proceso hijo
			close(pipe_banco_usuario[npipes - 1][0]);
			// Convertimos las variables int en char* para poder pasarlas como argumento en el execlp
			sprintf(fd_str, "%d", pipe_banco_usuario[npipes - 1][1]);
			sprintf(limite_retiro, "%d", configuracion.limite_retiro);
			sprintf(limite_transferencia, "%d", configuracion.limite_transferencia);
			sprintf(nCuenta, "%d", numCuenta);
			sprintf(sshm_id, "%d", shm_id);
			execlp("xterm", "xterm", "-e", "./usuario", fd_str, nCuenta, limite_retiro, limite_transferencia, configuracion.archivo_cuentas, sshm_id, NULL);
			perror("Error al ejecutar usuario");
			exit(1);
		}
		else if (pidUsuario < 0)
		{
			perror("Error al crear el proceso hijo\n");
			exit(1);
		}
		else
		{
			// Utilizamos continue para que vuelva a pedir los datos del usuario
			continue;
		}
	}

	exit(0);
}