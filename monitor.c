#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h> // Poder manejar IPC
#include <sys/msg.h> // Colas de mensajes
#include <sys/types.h> // key y pid

// Definimos unas constantes que indiquen el máximo de dinero y las operaciones consecutivas a relaizar antes de que se mande una alerta
#define DINERO_MAX 10000
#define OP_CONSECUTIVAS_MAX 5

// ESTRUCTURA MENSAJE
struct msgbuf {
	long tipo;
	char texto[100];
};
//msgrcv(cola_mensajes, &mensaje, sizeof(mensaje.texto), 0, 0);

int main(void) {
	// Declaramos file descriptor
	int pipe_monitor_hijos[2]; // 0 lectura, 1 escritura
	pid_t pidC;
	struct msgbuf mensaje; // Mensaje
	key_t key; // Clave que identifica a la cola de mensajes
	int cola_mensajes;
	char alerta[100];

	// Creamos la tubería para las alertas
	// Primer caso: Error en la creación del pipe
	if(pipe(pipe_monitor_hijos) == -1) {
		perror("Error en la tubería"); // Mostramos el error
		exit(1);
	}

	// Creamos la cola de mensajes indicada en msgrcv
	key = ftok("banco.c", 65); // Genera la clave de comunicación entre procesos
	cola_mensajes = msgget(key, 0666 | IPC_CREAT); // Devuelve identificador

	// Hacemos un bucle infinito que parará cuando se de alguno de los sucesos 
	while(1) {
		// Segundo caso: Error al recibir el mensaje enviado
		if(msgrcv(cola_mensajes, &mensaje, sizeof(mensaje.texto), 0, 0) == -1) {
			perror("Error al leer el mensaje"); // Mostramos el error
			continue; // No paramos el bucle
		}

		// Creamos la conexión con el hijo
		pidC = fork();
		// Comprobamos si se ha creado correctamente
		if(pidC == -1) {
			perror("Error en el fork()");
			exit(1); // Salimos
		}

		// Si no da error, es decri, se crea
		if(pidC == 0) {
			// Cerramos la lectura en el hijo
			close(pipe_monitor_hijos[0]);
			
			// Comprobamos las cantidades retiradas
			if(strstr(mensaje.texto, "Retiro") != NULL) { // Vamos a la sección "retir", y en caso de que se haya retirado alguna cantidadi
				float cantidadRetirada;
				sscanf(mensaje.texto, "Retiro %f", &cantidadRetirada); // Pasamos la cantidad retirada a int para así poder comprobar si supera o no a la sospechosa
				
				// Comprobamos si la cantidad retirada alcanza la sospechosa
				if(cantidadRetirada > DINERO_MAX) {
					// Mostramos la alerta debida
					strcpy(alerta, "ALERTA: Transacción sospechosa en cuenta 1002\n");
					write(pipe_monitor_hijos[1], &alerta, 50);
				}
			}

			// Cerramos la escritura
                        close(pipe_monitor_hijos[1]);
			exit(0);
		} 
		// Comprobamos ahora el proceso padre
		else {
			// Creamos variable alerta, que tendrá un máximo de 50 caracteres, como en la previa
			char alerta[50];
			// Cerramos la escritura
			close(pipe_monitor_hijos[1]);
			// Guardamos el número de caracteres de la alerta enviada por el hijo
			int charAlerta = read(pipe_monitor_hijos[0], &charAlerta, sizeof(charAlerta));

			// Si hay algo escrito, la imprimimos
			if(charAlerta > 0)
				printf("%s", alerta);

			// Cerramos lectura
			close(pipe_monitor_hijos[0]);
		}
	}

	return 0;
}


