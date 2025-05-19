#include <stdio.h>
#include <stdlib.h>
//#include <pthread.h>   //hilos
#include <unistd.h>    //Para el manejo de archivo
//#include <fcntl.h>
//#include <string.h>
//#include <sys/ipc.h>   // Poder manejar IPC
//#include <sys/msg.h>   // Colas de mensajes
#include <sys/types.h> // key y pid
#include <sys/shm.h>    //Librería para gestionar la memoria compartida
#include "init_cuentas.h"

int crear_memcompartida(){
    int shm_id = shmget(IPC_PRIVATE, sizeof(TablaCuentas), IPC_CREAT | 0666);
    if(shm_id == -1){
        perror("Error al crear la memoria compartida\n");
        return -1;
    }
    return shm_id;
}

TablaCuentas *asociar_memcompartida(int shm_id){
    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);
    if (tabla == NULL){
        perror("Error al asociar la memoria compartida\n");
        return NULL;
    }
    return tabla;
}

void liberar_memcompartida(TablaCuentas* tabla, int shm_id){
    shmdt((void*) tabla);
    shmctl(shm_id, IPC_RMID, NULL);
}