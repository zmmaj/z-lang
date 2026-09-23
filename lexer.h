#ifndef LEXER_H
#define LEXER_H

#include <stdint.h>

// Tokeni usklađeni sa zvaničnom C gramatikom, prevedeni na srpski
typedef enum {
    Z_TK_IDENTIFIKATOR,   // Promenljive, brojevi, funkcije (ObimZice, 40, PPOVRSINA)
    Z_TK_DODELA,          // =
    Z_TK_PLUS,            // +
    Z_TK_MINUS,           // Binarni minus (oduzimanje)
    Z_TK_UNARNI_MINUS,    // Unarni minus (PREDZNAK - naš štit!)
    Z_TK_MNOZENJE,        // *
    Z_TK_DELJENJE,        // /
    Z_TK_L_ZAGRADA,       // (
    Z_TK_D_ZAGRADA,       // )
    Z_TK_L_VITICASTA,     // {
    Z_TK_D_VITICASTA,     // }
    Z_TK_ZAREZ,           // ,
    Z_TK_TACKA_ZAREZ,     // ;
    Z_TK_KR_ODREDI,       // Ključna reč ODREDI
    Z_TK_KR_ISPISI,       // Ključna reč ISPISI
    Z_TK_SIGNAL_DALJE,    // Donja crta _
    Z_TK_GRESKA,          // Nepoznat karakter
    Z_TK_KRAJ_LINIJE      // Kraj obrade stringa
} token_type_t;

typedef struct {
    token_type_t type;
    char value[64];       // Tekstualni sadržaj tokena
} Token;

// Funkcija koja upeglanu liniju razbija na niz srpskih tokena
int lexer_tokenizuj(const char* ulaz, Token* izlaz_tokeni, int max_tokena);

#endif // LEXER_H
