#ifndef ESTRUCTURA_H
#define ESTRUCTURA_H

typedef struct {
    char tipo_operacion[20];     // "deposito", "retiro", etc.
    int cuenta_origen;
    int cuenta_destino;          // solo en transferencias
    float monto;
} Operacion;

#endif