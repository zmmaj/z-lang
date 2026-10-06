#include "funkcije.h"
#include <string.h>
#include <stdio.h>

// Globalne tabele i stek, izolovani unutar ovog modula
static Z_Funkcija Tabela_Funkcija[MAX_FUNKCIJA];
static int Broj_Zauzetih_Funkcija = 0;

static uint32_t Pozivni_Stek[MAX_POZIVA];
static int Vrh_Steka = 0;

// Inicijalizacija i čišćenje bafera pre novog prevođenja
void Funkcije_Inicijalizuj(void) {
    Broj_Zauzetih_Funkcija = 0;
    Vrh_Steka = 0;
    memset(Tabela_Funkcija, 0, sizeof(Tabela_Funkcija));
    memset(Pozivni_Stek, 0, sizeof(Pozivni_Stek));
}

// Registracija nove funkcije i njene PC adrese
int Funkcije_Dodaj(const char* ime, int adresa_pc) {
    if (Broj_Zauzetih_Funkcija >= MAX_FUNKCIJA) {
        return -1;
    }
    // Proveravamo da li funkcija već postoji da izbegnemo dupliranje
    for (int i = 0; i < Broj_Zauzetih_Funkcija; i++) {
        if (strcmp(Tabela_Funkcija[i].ime, ime) == 0) {
            Tabela_Funkcija[i].adresa_pc = adresa_pc;
            return i;
        }
    }
    strncpy(Tabela_Funkcija[Broj_Zauzetih_Funkcija].ime, ime, MOJA_MAX_DUZINA_IMENA - 1);
    Tabela_Funkcija[Broj_Zauzetih_Funkcija].adresa_pc = adresa_pc;
    return Broj_Zauzetih_Funkcija++;
}

// Pronalaženje adrese na koju virtuelna mašina treba da skoči
int Funkcije_Nadji_Adresu(const char* ime) {
    for (int i = 0; i < Broj_Zauzetih_Funkcija; i++) {
        if (strcmp(Tabela_Funkcija[i].ime, ime) == 0) {
            return Tabela_Funkcija[i].adresa_pc;
        }
    }
    return -1; // Funkcija još nije definisana (skok unapred)
}

// 🌟 STEK OPERACIJA: Pamćenje povratne adrese (POZOVI)
void Stek_Potisni(uint32_t povratna_adresa) {
    if (Vrh_Steka < MAX_POZIVA) {
        Pozivni_Stek[Vrh_Steka++] = povratna_adresa;
    } else {
        printf("🛑 Kritična greška VM: Prelivanje pozivnog steka (Stack Overflow)!\n");
    }
}

// 🌟 STEK OPERACIJA: Vraćanje na prethodnu adresu (VRATI_SE)
uint32_t Stek_Popni(void) {
    if (Vrh_Steka > 0) {
        return Pozivni_Stek[--Vrh_Steka];
    }
    printf("🛑 Kritična greška VM: Pozivni stek je prazan (Stack Underflow)!\n");
    return 0; 
}
