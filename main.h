#ifndef Z_LANG_MAIN_H
#define Z_LANG_MAIN_H

#include <stdint.h>
#include <task.h>
#include <errno.h>
#include <elf/elf_mod.h>  // Javna HelenOS userspace ELF zaglavlja

#define MAX_PROMENLJIVIH 20
#define MAX_DUZINA_IMENA 21
#define MULTIPLIKATOR 1000000LL
#define SIGNAL_DALJE -1

// Kodovi sistemskih grešaka
#define ERR_NONE               0
#define ERR_PREKORACENJE_MEM   1
#define ERR_NEBALANSIRANE_ZAG  2
#define ERR_NEPOZNATA_KOMANDA  3
#define ERR_FALI_TACKA_ZAREZ   4
#define ERR_PREDUGACKO_IME     5

// Naša univerzalna unija od 16 bajtova ostaje nepromenjena
#pragma pack(push, 1)
typedef struct {
    uint16_t marker;        // 2 bajta
    union {
        int64_t ceo_broj;   // 8 bajtova
        const char* string_ptr;
    } vrednost;             // unija zauzima 8 bajtova
    uint8_t padding[6];     // FIKS: Dodajemo 6 praznih bajtova da dopunimo do 16!
} Promenljiva;
#pragma pack(pop)

extern char Imena_Promenljivih[MAX_PROMENLJIVIH][MAX_DUZINA_IMENA];
extern Promenljiva Kompajlerska_Memorija[MAX_PROMENLJIVIH];
extern int Broj_Zauzetih_Lokacija;
extern uint8_t Masinski_Kod_Bafer[1024];
extern int Velicina_Generisanog_Koda;
extern int Trenutna_Linija_Kompajliranja;
extern const char* Sirova_Linija_Teksta;

void prijavi_sistemsku_gresku(int kod_greske, const char* detalj);
int Tabela_Simbola(const char* ime);
int obradi_argument_sa_znakom(Token t, int unarni_minus);
void bezbedno_prevedi_u_bajte_tokeni(Token* tokeni, int broj_tokena);
#endif // Z_LANG_MAIN_H
