#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>   //hilos
#include <semaphore.h> //Semaforos
#include <unistd.h>    //Para el manejo de archivo
#include <fcntl.h>
#include <string.h>
#include <sys/ipc.h>   // Poder manejar IPC
#include <sys/msg.h>   // Colas de mensajes
#include <sys/types.h> // key y pid
#include "init_cuentas.h"
#include "estructura.h"
#include "memoria.h"
#include "ficheros.h"
#include "entrada_salida.h"

struct msgbuf mensaje; // Mensaje
sem_t *sem_usuarios;
sem_t *sem_banco; //Semáforo para controlar el acceso a las cuentas de los usuarios
//Tenemos que crear un semaforo porque nos pide regular la concurrencia si varios usuarios
//intentan acceder al mismo log tenemos que garantizar la integridad del archivo
sem_t *sem_transacciones;

int pipe_padre_hijo[2],cola_mensajes;
char titular[50],archivo_log[50],archivo_cuentas[50];
int limite_retiro, limite_transferencia;
key_t key; // Clave que identifica a la cola de mensajes 
char alerta[100];
char plt_mensaje[12] = "%d %d %f %d"; // Creamos una plantilla para utilizarla en los mensajes
struct Cuenta cuenta_origen;
long pos_origen = -1;
int num_cuenta_ant = 0; // Para comprobar el umbral de transferencias
int alertaTrans = 0; // Si es 0 no se ha recibido alerta
TablaCuentas* tabla;
int indCuenta;

void inicializar()
{
    // Se abre el semáforo
    sem_banco = sem_open("/sem_banco", O_CREAT,0644, 1);
    if(sem_banco == SEM_FAILED){
        perror("Error al crear el semáforo");
        exit(1);
    }

    // Abrimos el semáforo creado en banco.c para controlar los usuarios
    sem_usuarios = sem_open("sem_usuarios", O_RDWR);
    if (sem_usuarios == SEM_FAILED)
    {
        perror("Error al crear el semáforo");
        exit(1);
    }

    // Iniciar semáforo de transacciones
    sem_transacciones = sem_open("/sem_transacciones", O_CREAT, 0644, 1);
    if (sem_transacciones == SEM_FAILED) 
    {
        perror("Error al inicializar semáforo");
        exit(1);
	}

    return;
}

void enviar_operacion(const char *tipo, int origen, int destino, float monto, char *mensaje)
{
    Operacion op;
    strcpy(op.tipo_operacion, tipo);
    op.cuenta_origen = origen;
    op.cuenta_destino = destino;
    op.monto = monto;
    strcpy(op.mensaje, mensaje);
    write(pipe_padre_hijo[1], &op, sizeof(op)); // Enviamos al padre
    return;
}

int pos_tabla(int numCuenta)
{
    int devol = -1, i = 0;
    for (i = 0; i < tabla->num_cuentas; i++)
    {
        if (numCuenta == tabla->cuentas[i].numero_cuenta)
        {
            devol = i;
            break;
        }
    }
    if (devol == -1)
    {
        printf("Cuenta de destino no encontrada en la tabla\n");
    }
    return devol;
}

int comprobar_saldo_f(int numCuenta)
{
    struct Cuenta cuenta;
    sem_wait(sem_banco);
    FILE *archivo = fopen(archivo_cuentas, "rb+");
    if (archivo == NULL)
    {
        perror("Error al abrir el archivo de cuentas");
        sem_post(sem_banco);
        return -1;
    }

    // Buscar cuenta de destino
    // Usamos cuenta_temp como una estructura auxiliar en vez de hacer las lecturas directamente en "cuenta_origen" y los separamos para asegurarnos de no sobreescribirlo y que cada uno realmente pertenece al que corresponde.
    while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo))
    {
        if (cuenta.numero_cuenta == numCuenta)
        {
            sem_post(sem_banco);
            return cuenta.saldo;
        }
    }
    sem_post(sem_banco);
}

void *realizar_deposito()
{
    float monto;
    char esvalido;
    
    printf("Ingrese el monto: ");
    scanf("%f", &monto);
    
    // Actualizamos el saldo de la cuenta
    cuenta_origen.saldo += monto;

    agregar_a_buffer(cuenta_origen);
    tabla->cuentas[indCuenta] = cuenta_origen;
    
    printf("Deposito realizado en la cuenta %d. Nuevo saldo: %.2f\n", cuenta_origen.numero_cuenta, cuenta_origen.saldo);
    enviar_operacion("deposito", cuenta_origen.numero_cuenta, 0, monto, "Correcto");
    registro_transacciones(tabla->cuentas[indCuenta].numero_cuenta, "Depósito", monto, "Correcto");

    return NULL;
}

void *realizar_retiro()
{
    float monto;
    char esvalido;

    struct Cuenta cuenta_destino = cuenta_origen;

    printf("Ingrese el monto: ");
    scanf("%f", &monto);

    if (monto > limite_retiro)
    {
        printf("ERROR: Retiro excede el límite permitido (%d)\n",limite_retiro);
        return NULL;
    }
    // Escribimos la operación en texto
    sprintf(mensaje.texto, plt_mensaje, cuenta_origen.numero_cuenta, cuenta_destino.numero_cuenta, monto, cuenta_origen.num_retiros);
    // El tipo 2 corresponde a retiro
    mensaje.tipo = 2;
    // Enviamos el mensaje a monitor para detectar anomalías
    msgsnd(cola_mensajes, &mensaje, sizeof(mensaje), IPC_NOWAIT);
    // Hay que leer la respuesta de monitor para ver si continuamos y guardamos los datos
    // Recibimos la respuesta de monitor
    // Recibimos de la cola de mensajes desde monitor pero solo leemos los que sean de tipo 2 (retiro)
    if (msgrcv(cola_mensajes, &mensaje, sizeof(mensaje), 2, 0) == -1)
    {
        perror("Error al leer el mensaje"); // Mostramos el error                                  
        return NULL;
    }
    // Comprobamos que sea del tipo 2
    if (mensaje.tipo == 2)
    {
        // El primer caracter de la respuesta nos indica si se puede hacer la operación
        esvalido = mensaje.texto[0];
    }

    // Como se permite la operación, la realizamos
    if (esvalido == '1')
    {
        // Comprobamos que el saldo sea suficiente
        if (cuenta_origen.saldo >= monto)
        { 
            cuenta_origen.saldo -= monto;
            cuenta_origen.num_retiros++;
           
            agregar_a_buffer(cuenta_origen);
            tabla->cuentas[indCuenta] = cuenta_origen;
            printf("Retiro realizado en la cuenta %d. Nuevo saldo: %.2f\n", cuenta_origen.numero_cuenta, cuenta_origen.saldo);
            enviar_operacion("retiro", cuenta_origen.numero_cuenta, 0, monto, &mensaje.texto[2]);
            registro_transacciones(tabla->cuentas[indCuenta].numero_cuenta, "Retiro", monto, &mensaje.texto[2]);
        }
        else
        {
            // Si el saldo no es suficiente, mostramos un mensaje al usuario
            printf("Saldo insuficiente en la cuenta %d.\n", cuenta_origen.numero_cuenta);
        }
    }
    else 
    {
        agregar_a_buffer(cuenta_origen);
        tabla->cuentas[indCuenta] = cuenta_origen;
        printf("\n%s\n", &mensaje.texto[2]);
        enviar_operacion("retiro", cuenta_origen.numero_cuenta, 0, monto, &mensaje.texto[2]);
        registro_transacciones(tabla->cuentas[indCuenta].numero_cuenta, "Retiro", monto, &mensaje.texto[2]);
    }

    return NULL;
}

void *realizar_transferencia()
{
    int num_cuenta_destino;
    float monto;
    struct Cuenta cuenta_destino;
    struct Cuenta cuenta_temp; // Solución problema de transferencias, se sobrescribia una cuenta encima de otra y provocaba que la de cuenta de origen "desapareciese".
    int encontrado_destino = 0;
    long pos_destino = -1;
    char esvalido;

    printf("Ingrese el número de cuenta destino: ");
    scanf("%d", &num_cuenta_destino);
    printf("Ingrese el monto: ");
    scanf("%f", &monto);

    if (monto > limite_transferencia)
    {
        printf("ERROR: Transferencia excede el límite permitido (%d)\n",limite_transferencia);
        return NULL;
    }

    sem_wait(sem_banco);
    FILE *archivo = fopen(archivo_cuentas, "rb+");
    if (archivo == NULL)
    {
        perror("Error al abrir el archivo de cuentas");
        sem_post(sem_banco);
        return NULL;
    }

    // Buscar cuenta de destino
    // Usamos cuenta_temp como una estructura auxiliar en vez de hacer las lecturas directamente en "cuenta_origen" y los separamos para asegurarnos de no sobreescribirlo y que cada uno realmente pertenece al que corresponde.
    while (fread(&cuenta_temp, sizeof(struct Cuenta), 1, archivo))
    {
        if (cuenta_temp.numero_cuenta == num_cuenta_destino)
        {
            cuenta_destino = cuenta_temp;
            encontrado_destino = 1;
            pos_destino = ftell(archivo) - sizeof(struct Cuenta);
            break;
        }
    }

    if (!encontrado_destino)
    {
        printf("Cuenta destino no encontrada.\n");
        fclose(archivo);
        sem_post(sem_banco);
        return NULL;
    }

    if (cuenta_origen.saldo < monto)
    {
        printf("Saldo insuficiente en la cuenta origen.\n");
        fclose(archivo);
        sem_post(sem_banco);
        return NULL;
    }

    if (num_cuenta_ant == num_cuenta_destino) 
    {
        if (alertaTrans == 1)
        {
            cuenta_origen.num_transferencias = 5;
        }
        cuenta_origen.num_transferencias++;
    }
    else 
    {
        cuenta_origen.num_transferencias = 1;
        num_cuenta_ant = num_cuenta_destino;
    }
    sprintf(mensaje.texto, plt_mensaje, cuenta_origen.numero_cuenta, num_cuenta_destino, monto, cuenta_origen.num_transferencias);
    mensaje.tipo = 3;
    msgsnd(cola_mensajes, &mensaje, sizeof(mensaje), IPC_NOWAIT);
    // Hay que leer la respuesta de monitor para ver si continuamos y guardamos los datos
    // Recibimos la respuesta de monitor
    // Recibimos de la cola de mensajes desde monitor pero solo leemos los que sean de tipo 3 (transferencia)
    if (msgrcv(cola_mensajes, &mensaje, sizeof(mensaje), 3, 0) == -1)
    {
        perror("Error al leer el mensaje"); // Mostramos el error
        fclose(archivo);     // Cerramos el archivo
        sem_post(sem_banco);
        return NULL;
    }
    // Comprobamos que sea del tipo 3
    if (mensaje.tipo == 3)
    {
        // El primer caracter de la respuesta nos indica si se puede hacer la operación
        esvalido = mensaje.texto[0];
    }

    // Como se permite la operación, la realizamos
    if (esvalido == '1')
    {
        // Realizar la transferencia
        cuenta_origen.saldo -= monto;
        cuenta_destino.saldo += monto;
        agregar_a_buffer(cuenta_origen);
        tabla->cuentas[indCuenta] = cuenta_origen;
        int indCuentaDest = pos_tabla(cuenta_destino.numero_cuenta);
        if(indCuentaDest != -1) 
        {
            agregar_a_buffer(cuenta_destino);
            tabla->cuentas[indCuentaDest] = cuenta_destino;
        }
        
        printf("Transferencia realizada correctamente.\n");
        printf("Nuevo saldo de la cuenta %d: %.2f\n", cuenta_origen.numero_cuenta, cuenta_origen.saldo);
        printf("Nuevo saldo de la cuenta %d: %.2f\n", cuenta_destino.numero_cuenta, cuenta_destino.saldo);

        char smensaje[100];
        sprintf(smensaje, "Transferencia a cuenta %d", cuenta_destino.numero_cuenta);
        enviar_operacion("transferencia", cuenta_origen.numero_cuenta, cuenta_destino.numero_cuenta, monto, &mensaje.texto[2]);
        registro_transacciones(tabla->cuentas[indCuenta].numero_cuenta, smensaje, monto, &mensaje.texto[2]);
    }
    else 
    {
        printf("\n%s\n", &mensaje.texto[3]);
        char smensaje[100];
        sprintf(smensaje, "Transferencia a cuenta %d", cuenta_destino.numero_cuenta);
        enviar_operacion("transferencia", cuenta_origen.numero_cuenta, cuenta_destino.numero_cuenta, monto, &mensaje.texto[2]);
        registro_transacciones(tabla->cuentas[indCuenta].numero_cuenta, smensaje, monto, &mensaje.texto[2]);
        cuenta_origen.num_transferencias = 0;
        alertaTrans = 1;
    }
    fclose(archivo);     // Cerramos el archivo
    sem_post(sem_banco);
    return NULL;
}

void *consultar_saldo()
{
    float saldoF = comprobar_saldo_f(tabla->cuentas[indCuenta].numero_cuenta);
    if (saldoF != tabla->cuentas[indCuenta].saldo) 
    {
        printf("El saldo de la tabla y el saldo de 'cuentas.dat' no coinciden\n");
    }
    printf("Titular: %s\n", tabla->cuentas[indCuenta].titular);
    printf("Saldo actual de la cuenta %d: %.2f\n", tabla->cuentas[indCuenta].numero_cuenta, tabla->cuentas[indCuenta].saldo);
    enviar_operacion("consulta", tabla->cuentas[indCuenta].numero_cuenta, 0, 0.0f, "Correcto"); // Enviamos al padre la info
    registro_transacciones(tabla->cuentas[indCuenta].numero_cuenta, "Consulta", tabla->cuentas[indCuenta].saldo, "Correcto");
    return NULL;
}

void menu_usuario()
{
    int opcion = 0, leidos = 0;

    inicializar();
    while (1)
    {
        printf("\nMENU DE USUARIO - BACKSECURE\n");
        printf("1. Depósito\n2. Retiro\n3. Transferencia\n4. Consultar saldo\n5. Salir\n");
        printf("Seleccione una opción: ");
        fflush(stdin);
        // Comprobamos que lo que introduzca el usuario sea del tipo int
        leidos = scanf("%d", &opcion);
        // Si leídos es igual a 0, no ha podido convertir lo tecleado por el usuario y por tanto vamos a vaciar stdin
        if (leidos == 0)
        {
            char k;
            // Vaciamos el buffer
            do
                k = getc(stdin);
            while (k >= 0 && k != '\n');
            // Ponemos la opción a 0 para que nos diga el sistema opción no válida
            opcion = 0;
        }

        switch (opcion)
        {
        case 1:
        {
            pthread_t hilo;
            pthread_create(&hilo, NULL, realizar_deposito, NULL);
            pthread_join(hilo, NULL);
            break;
        }
        case 2:
        {
            pthread_t hilo;
            pthread_create(&hilo, NULL, realizar_retiro, NULL);
            pthread_join(hilo, NULL);
            break;
        }
        case 3:
        {
            pthread_t hilo;
            pthread_create(&hilo, NULL, realizar_transferencia, NULL);
            pthread_join(hilo, NULL);
            break;
        }
        case 4:
        {
            pthread_t hilo;
            pthread_create(&hilo, NULL, consultar_saldo, NULL);
            pthread_join(hilo, NULL);
            break;
        }
        case 5:
        { 
            // Salir del programa correctamente
            printf("Saliendo del sistema...\n");
            return;
        }
        default:
            printf("Opción inválida. Intente de nuevo.\n");
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    int nCuenta, shm_id, i=0;
    if (argc == 7)
    {
        pipe_padre_hijo[1] = atoi(argv[1]); // recupera fd del padre
        nCuenta = atoi(argv[2]);
        limite_retiro = atoi(argv[3]);
        limite_transferencia = atoi(argv[4]);
        strcpy(archivo_cuentas, argv[5]);
        shm_id = atoi(argv[6]);
    }
    pthread_t hiloES;
    pthread_create(&hiloES, NULL, gestionar_entrada_salida, NULL);
    //pthread_join(hiloES, NULL);

    key = ftok("banco.c", 65);                     // Genera la clave de comunicación entre procesos
    cola_mensajes = msgget(key, 0666 | IPC_CREAT); // Devuelve identificador

    tabla = asociar_memcompartida(shm_id);

    for (i = 0; i < tabla->num_cuentas; i++) 
    {
        if (tabla->cuentas[i].numero_cuenta == nCuenta)
        {
            indCuenta = i;
            printf("Titular: %s\n", tabla->cuentas[indCuenta].titular);
            printf("Número de cuenta: %d\n", tabla->cuentas[indCuenta].numero_cuenta);
            crear_directorio(tabla->cuentas[indCuenta].numero_cuenta);
            cuenta_origen = tabla->cuentas[indCuenta];
            break;
        }
    }

    menu_usuario();
    if (alertaTrans == 1)
    {
        cuenta_origen.num_transferencias = 5;
    }

    sincronizar_buffer_final(NULL);
    // Se cierra la escritura del proceso hijo
    close(pipe_padre_hijo[1]);
    sem_post(sem_usuarios);

    sem_close(sem_transacciones);
	sem_unlink("/sem_transacciones");

    exit(0);
}