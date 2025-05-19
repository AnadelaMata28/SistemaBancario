#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> //para el comando mkdir
#include <semaphore.h>
#include "init_cuentas.h"
#include <time.h> //para la fecha hora y tal

//Creamos un directorio con el numero de cuenta de cada usuario
//de forma dinamica cuando este se registra
void crear_directorio(int numero_cuenta) {
	char nCuenta[50];
	sprintf(nCuenta, "./transacciones/%d", numero_cuenta);
	if (access("./transacciones", F_OK) == -1) 
		mkdir("./transacciones", 0777);


	if (access(nCuenta, F_OK) == -1)
		mkdir(nCuenta, 0777);
	
}

//Para que se guarde el tiempo actual en el que se ha hecho la transaccion
void registrar_tiempoActual (char *buffer, int size) {
	time_t tiempoActual = time(NULL);
	struct tm *tm_info = localtime(&tiempoActual);
	strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", tm_info);
}

int registro_transacciones(int numero_cuenta, const char *mensaje, int monto, char *mensaje_monitor) 
{
	char ruta_archivo[150];
	snprintf(ruta_archivo, sizeof(ruta_archivo), "./transacciones/%d/transacciones.log", numero_cuenta);

	FILE *archivo = fopen(ruta_archivo, "a+");
	if (archivo == NULL) {
       	printf("No se pudo abrir el archivo de transacciones");
    	return 1;
   	}

	char tiempoActual [30];
	registrar_tiempoActual(tiempoActual, sizeof(tiempoActual));
	if (strcasecmp(mensaje, "consulta") == 0)
	{
		fprintf(archivo, "%s  %s\n",  tiempoActual, mensaje);
	}
	else
	{
		fprintf(archivo, "%s  %s | Monto: %d | Monitor: %s\n",  tiempoActual, mensaje, monto, mensaje_monitor);
	}
	fclose(archivo);

	return 0;
}
