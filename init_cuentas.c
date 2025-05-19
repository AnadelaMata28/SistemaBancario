#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "init_cuentas.h"

int main() {
	FILE *file;

	// Creamos la estructura
	struct Cuenta cuenta[5] = {
		{0001, "Natalia Berbel", 3000.00, 0, 0},
		{0002, "Santiago Llanos", 10000.00, 0, 0},
		{0003, "María Fazio", 1500.00, 0, 0},
		{0004, "Paula González", 222.00, 0, 0},
		{0005, "Ana de la Mata", 2828.00, 0, 0},
	};
	
	// Creamos y escribimos el archivo binario "cuentas.dat"
	// wb+ crea un archivo binario vacío para la lectura y escritura, si el archivo existe el contenido se borrará salvo que sea un archivo lógico
	file = fopen("cuentas.dat", "wb+");
	if (!file) {
		perror("No se pudo crear cuentas.dat\n");
		return 1;
	}

	fwrite(&cuenta, sizeof(struct Cuenta), 5, file);

	fclose(file);

	printf("Archivo creado correctamente\n");

	return 0;
}

