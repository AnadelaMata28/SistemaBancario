#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h> //hilos
#include <semaphore.h> //Semaforos
#include <unistd.h> //Para el manejo de archivo
#include "init_cuentas.h" //O crear un define en init_cuenta e incluyo aqui el cuenta.h
#include "estructura.h"

sem_t semaforo;
pthread_mutex_t mutex;
int pipe_padre_hijo[2];

void inicializar() {
    sem_init(&semaforo, 0, 1);
    pthread_mutex_init(&mutex, NULL);
}

void enviar_operacion(const char *tipo, int origen, int destino, float monto) {  // ✅ AÑADIDO
    Operacion op;
    strcpy(op.tipo_operacion, tipo);
    op.cuenta_origen = origen;
    op.cuenta_destino = destino;
    op.monto = monto;
    write(pipe_padre_hijo[1], &op, sizeof(op));  // Enviamos al padre
}

void realizar_deposito(int num_cuenta, float monto) {
    // usamossemaforo - antes  del archivo esperar/bloq
    sem_wait(&semaforo);

    //Abrimos el archivo binario de cuentas
    FILE *archivo = fopen("cuentas.dat", "rb+");//rb -> modo lectura - escritura
    if (archivo == NULL) {
        perror("Error al abrir el archivo de cuentas");
        sem_post(&semaforo); // Liberamos el semáforo en caso de error
        return;
    }

    struct Cuenta cuenta;
    int encontrado = 0;

    pthread_mutex_lock(&mutex); // Bloqueamos la sección crítica con el mutex

    // Recorremos las cuentas en el archivo
    while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo)) {
        // Comprobamos si el número de cuenta coincide con el que se ha introducido
        if (cuenta.numero_cuenta == num_cuenta) {
            encontrado = 1; //SI LO ENCONTRAMOS

            // Actualizamos el saldo de la cuenta
            cuenta.saldo += monto;

            // Movemos el cursor hacia atrás para sobrescribir la cuenta actualizada
            fseek(archivo, -sizeof(struct Cuenta), SEEK_CUR);//SEEK_CUR -> pone a aprtir de donde esta el cursor en ese momento

            // Escribimos los datos actualizados de la cuenta en el archivo
            fwrite(&cuenta, sizeof(struct Cuenta), 1, archivo);
            break; // Salimos del bucle porque ya hemos actualizado la cuenta
        }
    }

    pthread_mutex_unlock(&mutex); // Liberamos la sección crítica

    fclose(archivo);
    sem_post(&semaforo); // Liberamos el semáforo

    if (encontrado) {
        printf("Deposito realizado en la cuenta %d. Nuevo saldo: %.2f\n", num_cuenta, cuenta.saldo);
    } else {
        printf("Cuenta %d no encontrada.\n", num_cuenta);
    }

    if (encontrado) { //Prueba que "if" funciona mejor
        printf("Deposito realizado en la cuenta %d. Nuevo saldo: %.2f\n", num_cuenta, cuenta.saldo);
        enviar_operacion("deposito", num_cuenta, 0, monto);  // ✅ AÑADIDO
    }
}

void realizar_retiro( int num_cuenta, float monto) {
    sem_wait(&semaforo);

    FILE *archivo = fopen("cuentas.dat", "rb+");
    if (archivo == NULL) { // vemos si hay error al abrir el archivo
        perror("Error al abrir el archivo de cuentas");
        sem_post(&semaforo); // Liberamos si ocurre un error
        return;
    }

    struct Cuenta cuenta;
    int encontrado = 0;

    pthread_mutex_lock(&mutex); // Bloqueamos la sección crítica con el mutex

 // Recorremos las cuentas para buscar el número de cuenta proporcionado
    while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo)) {
        if (cuenta.numero_cuenta == num_cuenta) {
            encontrado = 1;
            if (cuenta.saldo >= monto) { // Comprobamos que el saldo sea suficiente
                cuenta.saldo -= monto; // Restamos el monto al saldo actual
                fseek(archivo, -sizeof(struct Cuenta), SEEK_CUR); // Retrocedemos el cursor para sobrescribir
                fwrite(&cuenta, sizeof(struct Cuenta), 1, archivo); // Sobrescribimos la cuenta actualizada
            } else {
                // Si el saldo no es suficiente, mostramos un mensaje al usuario
                printf("Saldo insuficiente en la cuenta %d.\n", num_cuenta);
                fclose(archivo);
                sem_post(&semaforo);
                return; // Salimos de la función
            }
            break;
        }
    }

    pthread_mutex_unlock(&mutex); // Liberamos la sección crítica

    fclose(archivo); // Cerramos el archivo
    sem_post(&semaforo); // Liberamos el semáforo

    if (encontrado) {
        printf("Retiro realizado en la cuenta %d. Nuevo saldo: %.2f\n", num_cuenta, cuenta.saldo);
    } else {
        printf("Cuenta %d no encontrada.\n", num_cuenta);
    }

    if (encontrado) { //Prueba if...
        printf("Retiro realizado en la cuenta %d. Nuevo saldo: %.2f\n", num_cuenta, cuenta.saldo);
        enviar_operacion("retiro", num_cuenta, 0, monto);  // ✅ AÑADIDO
    }else {
        printf("Cuenta %d no encontrada.\n", num_cuenta);
    }
}

void realizar_transferencia(int num_cuenta_origen, int num_cuenta_destino, float monto) {
    sem_wait(&semaforo);

    FILE *archivo = fopen("cuentas.dat", "rb+");
    if (archivo == NULL) {
        perror("Error al abrir el archivo de cuentas");
        sem_post(&semaforo);
        return;
    }

    struct Cuenta cuenta_origen, cuenta_destino;
    int encontrado_origen = 0, encontrado_destino = 0;
    long pos_origen = -1, pos_destino = -1;

    // Buscar ambas cuentas
    while (fread(&cuenta_origen, sizeof(struct Cuenta), 1, archivo)) {
        if (cuenta_origen.numero_cuenta == num_cuenta_origen) {
            encontrado_origen = 1;
            pos_origen = ftell(archivo) - sizeof(struct Cuenta); // Copia de la cuenta asociada
        } else if (cuenta_origen.numero_cuenta == num_cuenta_destino) {
            cuenta_destino = cuenta_origen;
            encontrado_destino = 1;
            pos_destino = ftell(archivo) - sizeof(struct Cuenta); // Volvemos y la sobreescribimos
        }

        if (encontrado_origen && encontrado_destino)
            break;
    }

    if (!encontrado_origen || !encontrado_destino) { // Si alguna de las cuentas no existe, liberamos el semáforo.
        printf("Cuenta origen o destino no encontrada.\n");
        fclose(archivo);
        sem_post(&semaforo);
        return;
    }

    if (cuenta_origen.saldo < monto) {
        printf("Saldo insuficiente en la cuenta origen.\n");
        fclose(archivo);
        sem_post(&semaforo);
        return;
    }

    // Realizar la transferencia
    cuenta_origen.saldo -= monto;
    cuenta_destino.saldo += monto;

    // Guardar cuenta origen
    fseek(archivo, pos_origen, SEEK_SET);
    fwrite(&cuenta_origen, sizeof(struct Cuenta), 1, archivo);

    // Guardar cuenta destino
    fseek(archivo, pos_destino, SEEK_SET);
    fwrite(&cuenta_destino, sizeof(struct Cuenta), 1, archivo);

    // Usamos fseek para movernos a las posiciones exactas donde estaban las cuentas antes, y sobreescribimos con los datos ya modificados
    fclose(archivo);
    sem_post(&semaforo);
    // Cerramos el archivo y liberamos el semáforo

    printf("Transferencia realizada correctamente.\n");
    printf("Nuevo saldo de la cuenta %d: %.2f\n", num_cuenta_origen, cuenta_origen.saldo);
    printf("Nuevo saldo de la cuenta %d: %.2f\n", num_cuenta_destino, cuenta_destino.saldo);

    enviar_operacion("transferencia", num_cuenta_origen, num_cuenta_destino, monto);
}

void consultar_saldo(int num_cuenta) {
    sem_wait(&semaforo);

    FILE *archivo = fopen("cuentas.dat", "rb"); // Abrimos el archivo en modo lectura binaria con "rb", ya que unicamente queremos leer datos
    if (archivo == NULL) {
        perror("Error al abrir el archivo de cuentas");
        sem_post(&semaforo);
        return;
    }

    struct Cuenta cuenta;
    int encontrado = 0;

    while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo)) {
        if (cuenta.numero_cuenta == num_cuenta) {
            encontrado = 1;
            break;
        }
    }

    fclose(archivo);
    sem_post(&semaforo);

    if (encontrado) {
        printf("Titular: %s\n", cuenta.titular);
        printf("Saldo actual de la cuenta %d: %.2f\n", num_cuenta, cuenta.saldo);
        enviar_operacion("consulta", num_cuenta, 0, 0.0f); //Enviamos al padre la info
    } else {
        printf("Cuenta no encontrada.\n");
    }
}


void *operacion_deposito(void *arg) {
    int *data = (int *)arg;
    realizar_deposito(data[0], (float)data[1]);
    free(arg);
    return NULL;
}

void *operacion_retiro(void *arg) {
    // reciboo un argumento, que es un puntero a los datos de la operación de depósito.
    // 'data' contiene: el número de cuenta y el monto del depósito.
    int *data = (int *)arg;

    // Llamo a ->  realizar_deposito con los datos extraídos de 'data'.
    // 'data[0]' es el número de cuenta y 'data[1]' es el monto.
    realizar_deposito(data[0], (float)data[1]);

    // Después de que se ha realizado la operación, liberamos la memoria que fue reservada para 'data'
    free(arg);
    return NULL;
}

void *operacion_transferencia(void *arg) {
    int *data = (int *)arg; //arg -> puntero
    realizar_transferencia(data[0], data[1], (float)data[2]);
    free(arg);
    return NULL;
}


void *operacion_consulta(void *arg) {
    int *data = (int *)arg;
    consultar_saldo(data[0]);
    free(arg);
    return NULL;
}

void menu_usuario() {
    int opcion;
    inicializar();

    while (1) {
        printf("\nMENU DE USUARIO - BACKSECURE\n");
        printf("1. Depósito\n2. Retiro\n3. Transferencia\n4. Consultar saldo\n5. Salir\n");
        printf("Seleccione una opción: ");
        scanf("%d", &opcion);

        switch(opcion) {
            case 1: {
                float *data = malloc(2 * sizeof(float));  // Usamos float para los montos
                if (data == NULL) {
                    perror("Error de memoria");
                    continue;
                }
                printf("Ingrese el número de cuenta: ");
                scanf("%d", (int*)&data[0]);
                printf("Ingrese el monto: ");
                scanf("%f", &data[1]);

                pthread_t hilo;
                pthread_create(&hilo, NULL, operacion_deposito, (void *)data);
                pthread_detach(hilo);
                break;
            }
            case 2: {
                float *data = malloc(2 * sizeof(float));
                if (data == NULL) {
                    perror("Error de memoria");
                    continue;
                }
                printf("Ingrese el número de cuenta: ");
                scanf("%d", (int*)&data[0]);
                printf("Ingrese el monto: ");
                scanf("%f", &data[1]);

                pthread_t hilo;
                pthread_create(&hilo, NULL, operacion_retiro, (void *)data);
                pthread_detach(hilo);
                break;
            }
            case 3: {
                float *data = malloc(3 * sizeof(float));
                if (data == NULL) {
                    perror("Error de memoria");
                    continue;
                }
                printf("Ingrese el número de cuenta ORIGEN: ");
                scanf("%d", (int*)&data[0]);
                printf("Ingrese el número de cuenta DESTINO: ");
                scanf("%d", (int*)&data[1]);
                printf("Ingrese el monto: ");
                scanf("%f", &data[2]);

                pthread_t hilo;
                pthread_create(&hilo, NULL, operacion_transferencia, (void *)data);
                pthread_detach(hilo);
                break;
            }
            case 4: {
                int *data = malloc(sizeof(int));
                if (data == NULL) {
                    perror("Error de memoria");
                    continue;
                }
                printf("Ingrese el número de cuenta: ");
                scanf("%d", data);

                pthread_t hilo;
                pthread_create(&hilo, NULL, operacion_consulta, (void *)data);
                pthread_detach(hilo);
                break;
            }
            case 5: { // Salir del programa correctamente
                printf("Saliendo del sistema...\n");
                sem_destroy(&semaforo); // Destruir semáforo
                pthread_mutex_destroy(&mutex); // Destruir mutex
                return; // Salir del bucle y finalizar el programa
            }
            default:
                printf("Opción inválida. Intente de nuevo.\n");
                break;
        }
    }
}


int main(int argc, char *argv[]) {
    
    if (argc == 2) {
        pipe_padre_hijo[1] = atoi(argv[1]);  // ✅ AÑADIDO: recupera fd del padre
    }

    menu_usuario();
    return 0;
}
