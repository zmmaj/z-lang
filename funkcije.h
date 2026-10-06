#ifndef FUNKCIJE_H
#define FUNKCIJE_H

#include <stdint.h>
#include <stddef.h>

#define MAX_FUNKCIJA 64
#define MAX_POZIVA 32

// 🌟 POPRAVLJENO: Koristimo tvoju fabričku dužinu od 21 karakter da se ne sudara sa main.h
#define MOJA_MAX_DUZINA_IMENA 21 

// Struktura za tabelu definisanih funkcija u kompajleru
typedef struct {
    char ime[MOJA_MAX_DUZINA_IMENA]; // Koristi popravljenu dužinu
    int adresa_pc;
} Z_Funkcija;

// 🌟 PROTOTIPOVI ZA KOMPAJLER I VIRTUELNU MAŠINU
void Funkcije_Inicijalizuj(void);
int Funkcije_Dodaj(const char* ime, int adresa_pc);
int Funkcije_Nadji_Adresu(const char* ime);

// 🌟 POZIVNI STEK (Call Stack) ZA VIRTUELNU MAŠINU
void Stek_Potisni(uint32_t povratna_adresa);
uint32_t Stek_Popni(void);

#endif // FUNKCIJE_H
