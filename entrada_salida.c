#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "init_cuentas.h"
#include "estructura.h"
#include "entrada_salida.h"

#define RUTA_ARCHIVO_CUENTAS "cuentas.dat" // Donde guardamos los datos de las cuentas
const char *ruta_archivo = RUTA_ARCHIVO_CUENTAS; // Ruta constante porque vamos a usarla en varios void

// Creamos una instancia global del buffer
BufferEstructurado buffer = {
    .inicio = 0, // Índice de inicio
    .fin = 0, // Índice de fin
    .mutex = PTHREAD_MUTEX_INITIALIZER // Inicializamos el semáforo
};  

// Función para agregar una cuenta modificada al buffer circular
void agregar_a_buffer(struct Cuenta op) {
    pthread_mutex_lock(&buffer.mutex); // Bloqueamos el buffer para escritura
    int siguiente = (buffer.fin + 1) % TAM_BUFFER; // Es + 1 porque calculamos el índice de la próxima dirección

    // Comprobamos si hay hueco en el buffer
    if (siguiente != buffer.inicio) { 
        buffer.operaciones[buffer.fin] = op; // Guarda la operación
        buffer.fin = siguiente; // Lo movemos al siguiente
    } 

    pthread_mutex_unlock(&buffer.mutex); // Liberamos el buffer
}

// Hilo que gestiona las escrituras en disco de forma asíncrona
void *gestionar_entrada_salida(void *arg) {
    while (1) {
        pthread_mutex_lock(&buffer.mutex); // Bloqueamos acceso al buffer para lectura

        // Comprobamos si el buffer está vacío
        if (buffer.inicio != buffer.fin) {
            struct Cuenta op = buffer.operaciones[buffer.inicio];
            buffer.inicio = (buffer.inicio + 1) % TAM_BUFFER;
            pthread_mutex_unlock(&buffer.mutex); // Liberamos el buffer

            // Abrimos el archivo
            FILE *archivo = fopen(ruta_archivo, "rb+");

            // En caso de no existir, lanzamos un error
            if (archivo == NULL) {
                continue;
            }

            // Ponemos el puntero en la cuenta que se va a actualiza
            fseek(archivo, (op.numero_cuenta - 1) * sizeof(struct Cuenta), SEEK_SET);
            // Escribimos en el archivo
            fwrite(&op, sizeof(struct Cuenta), 1, archivo);
            // Cerramos el archivo
            fclose(archivo);
            // Lanzamos aviso 
        } 
        
        else {
            // Si no hay operaciones pendientes, liberamos el mutex
            pthread_mutex_unlock(&buffer.mutex);
        }
    }

    return NULL;
}

// Forzar escritura de todas las operaciones pendientes
void sincronizar_buffer_final() {
    pthread_mutex_lock(&buffer.mutex); // Volvemos a bloquear el buffer

     // Mientras existan operaciones en el buffer, las va sacando y escribiendo.
    while (buffer.inicio != buffer.fin) {
        struct Cuenta op = buffer.operaciones[buffer.inicio];
        // Nos movemos al siguiente puesto del buffer
        buffer.inicio = (buffer.inicio + 1) % TAM_BUFFER;

        FILE *archivo = fopen(ruta_archivo, "rb+");
        // Si el archivo existe nos ponemos en la posición del número de cuenta
        if (archivo != NULL) {
            fseek(archivo, (op.numero_cuenta - 1) * sizeof(struct Cuenta), SEEK_SET);
            // Escribimos en el archivo
            fwrite(&op, sizeof(struct Cuenta), 1, archivo);
            fclose(archivo);
            // Avisamos de que se ha realizado correctamente
        } 
        
        else {
            // En caso de que el archivo no exista, lanzamos un mensaje de error
            fprintf(stderr, "Error al abrir %s.\n", ruta_archivo);
        }
    }

    // Desbloqueamos mutex
    pthread_mutex_unlock(&buffer.mutex);
}
 