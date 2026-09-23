#ifndef SANITIZER_H
#define SANITIZER_H

#include <stdbool.h>

// Glavna funkcija sanitizera koju pozivamo u main.c
// Vraća true ako je linija uspešno očišćena, false ako postoji fatalna greška (npr. fali ';')
bool sanitizer_ocisti_liniju(const char* ulaz, char* izlaz, int max_duzina);

#endif // SANITIZER_H
