#ifndef NCUENTAS
    #define NCUENTAS

// Definimos la estructura de cuentas para que cualquier archivo pueda acceder a ella
struct Cuenta {
    int numero_cuenta;
    char titular[50];
    float saldo;
    int num_retiros;
    int num_transferencias;
    int bloqueado; // 1 si la cuenta está bloqueada, 0 si está activa
};

typedef struct {
    struct Cuenta cuentas[100];
    int num_cuentas;
} TablaCuentas;

#endif
