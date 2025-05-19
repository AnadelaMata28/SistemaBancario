#ifndef ENTSAL
    #define ENTSAL

#include <stdio.h>
#include <pthread.h>
#include "init_cuentas.h"
#include "estructura.h"

#define TAM_BUFFER 20 // cantidad máxima de operaciones que puede almacenar el buffer 

// Estructura del buffer circular para gestionar E/S de cuentas
typedef struct {
    struct Cuenta operaciones[TAM_BUFFER];
    int inicio; // Apuntamos a la próxima posición que será leída
    int fin; // Apuntamos a la próxima posición libre del buffer en la que escribiremos
    pthread_mutex_t mutex; // Sincronizar el acceso al buffer
} BufferEstructurado;

void agregar_a_buffer(struct Cuenta);
void *gestionar_entrada_salida(void *);
void sincronizar_buffer_final();

#endif