#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h>   // Poder manejar IPC
#include <sys/msg.h>   // Colas de mensajes
#include <sys/types.h> // key y pid
#include <time.h>
#include "estructura.h"
#include <signal.h>


int pipe_monitor_hijos[2]; // 0 lectura, 1 escritura
int umbral_retiros, umbral_transferencias,cola_mensajes;
int numCuenta;
float importe;
int numCuentaDest;
int num_transacciones;
char plt_mensaje[12] = "%d %d %f %d";

void cierra_cola(){
	// Elimina la cola de mensajes y cierra el pipe
	msgctl(cola_mensajes, IPC_RMID, (struct msqid_ds *)NULL);
	close(pipe_monitor_hijos[1]);
}

int main(int argc, char *argv[])
{
	// Cuando hayamos llamado al kill de monitor se ejecutará esta función
	// Ocurra lo que ocurra se llamará a cierra_cola
	atexit(cierra_cola);
	if (argc == 4)
	{
		pipe_monitor_hijos[1] = atoi(argv[1]); // recupera fd del padre
		umbral_retiros = atoi(argv[2]);
		umbral_transferencias = atoi(argv[3]);
	}

	struct msgbuf mensaje; // Mensaje
	key_t key;			   // Clave que identifica a la cola de mensajes
	char alerta[100];

	// Creamos la cola de mensajes indicada en msgrcv
	key = ftok("banco.c", 65);					   // Genera la clave de comunicación entre procesos
	cola_mensajes = msgget(key, 0666 | IPC_CREAT); // Devuelve identificador

	// Hacemos un bucle infinito que parará cuando se de alguno de los sucesos
	int salir = 0;
	while (1 && salir == 0)
	{
		// Segundo caso: Error al recibir el mensaje enviado
		// Lee todos los mensajes sean del tipo que sean
		if (msgrcv(cola_mensajes, &mensaje, sizeof(mensaje), 0, 0) == -1)
		{
			perror("Error al leer el mensaje"); // Mostramos el error
			continue;							// No paramos el bucle
		}

		// Lo que venga en el mensaje.texto con la forma de la plantilla de plt_mensasje, lo metemos en cada una de las variables correspondientes
		sscanf(mensaje.texto, plt_mensaje, &numCuenta, &numCuentaDest,&importe, &num_transacciones);

		switch (mensaje.tipo)
		{
			case 2:
				if (num_transacciones >= umbral_retiros)
				{
					// El 0 indica que no se puede realizar la transacción
					strcpy(mensaje.texto, "0 ALERTA:Retiros consecutivos detectados.");
					mensaje.tipo = 2;
					write(pipe_monitor_hijos[1], "ALERTA:Transacción sospechosa", 31);
					msgsnd(cola_mensajes, &mensaje, sizeof(mensaje), IPC_NOWAIT);
				}
				else
				{
					// El 1 indica que se puede realizar la transacción
					strcpy(mensaje.texto, "1 Correcto");
					mensaje.tipo = 2;
					msgsnd(cola_mensajes, &mensaje, sizeof(mensaje), IPC_NOWAIT);
				}
				break;
			case 3:
				if (num_transacciones >= umbral_transferencias)
				{
					// El 0 indica que no se puede realizar la transacción
					strcpy(mensaje.texto, "0 ALERTA:Transferencias consecutivas entre cuentas detectadas.");
					mensaje.tipo = 3;
					write(pipe_monitor_hijos[1], "ALERTA:Transacción sospechosa", 31);
					msgsnd(cola_mensajes, &mensaje, sizeof(mensaje), IPC_NOWAIT);
				}
				else
				{
					// El 1 indica que se puede realizar la transacción
					strcpy(mensaje.texto, "1 Correcto");
					mensaje.tipo = 3;
					msgsnd(cola_mensajes, &mensaje, sizeof(mensaje), IPC_NOWAIT);
				}
				break;
			default:
				break;
		}
	}

	//close(pipe_monitor_hijos[1]);

	exit(0);
}

