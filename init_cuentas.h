#ifndef NCUENTAS
    #define NCUENTAS

// Definimos la estructura de cuentas para que cualquier archivo pueda acceder a ella
struct Cuenta {
    int numero_cuenta;
    char titular[50];
    float saldo;
    int num_transacciones;
};

#endif
