#include "sanitizer.h"
#include <ctype.h>
#include <string.h>

// Pratimo stanje komentara kroz višelinijske blokove koda ako zatreba
static int unutar_komentara = 0;

bool sanitizer_ocisti_liniju(const char* ulaz, char* izlaz, int max_duzina) {
    if (!ulaz || !izlaz || max_duzina <= 0) {
        return false;
    }

    int unutar_navodnika = 0;
    int i = 0;
    int j = 0;
    int in_len = (int)strlen(ulaz);

    // 1. Preskačemo inicijalna prazna mesta na samom početku linije
    while (i < in_len && isspace((unsigned char)ulaz[i])) {
        i++;
    }

    // Prolazimo kroz celu liniju karakter po karakter
    while (ulaz[i] != '\0') {
        // Osiguranje od prelivanja bafera (ostavljamo mesto za ';' i '\0')
        if (j >= max_duzina - 2) {
            break; 
        }

        // --- LOGIKA ZA KOMENTARE ---
        if (!unutar_navodnika) {
            // Početak komentara >>
            if (!unutar_komentara && ulaz[i] == '>' && ulaz[i + 1] == '>') {
                unutar_komentara = 1;
                i += 2;
                continue;
            }
            // Kraj komentara <<
            if (unutar_komentara && ulaz[i] == '<' && ulaz[i + 1] == '<') {
                unutar_komentara = 0;
                i += 2;
                continue;
            }
        }

        // Ako smo unutar komentara, preskačemo trenutni karakter
        if (unutar_komentara) {
            i++;
            continue;
        }

        // --- LOGIKA ZA STRINGOVE (ŠTIT) ---
        if (ulaz[i] == '"') {
            unutar_navodnika = !unutar_navodnika;
            izlaz[j++] = ulaz[i++];
            continue;
        }

        // Ako smo unutar navodnika, prepisujemo sve bajt po bajt bez ikakvih izmena
        if (unutar_navodnika) {
            izlaz[j++] = ulaz[i++];
            continue;
        }

        // --- LOGIKA ZA NORMALIZACIJU RAZMAKA VAN STRINGOVA ---
        if (isspace((unsigned char)ulaz[i])) {
            // Ako je sledeći karakter takođe prazno mesto, preskačemo ga (sabijamo dupli space)
            // Takođe ne dodajemo razmak ako je bafer još uvek prazan
            if (j > 0 && izlaz[j - 1] != ' ' && !isspace((unsigned char)ulaz[i + 1]) && ulaz[i + 1] != '\0') {
                izlaz[j++] = ' ';
            }
            i++;
            continue;
        }

        // Ako nije ni string, ni komentar, ni višak razmaka, prepisujemo regularan karakter
        izlaz[j++] = ulaz[i++];
    }

    izlaz[j] = '\0';

    // 2. Trimovanje potencijalnog praznog mesta na samom kraju pre provere ';'
    if (j > 0 && izlaz[j - 1] == ' ') {
        izlaz[--j] = '\0';
    }

    // 3. STROGA SINTAKSNA PROVERA: Da li linija ispravno završava sa ';'
    // Preskačemo proveru samo ako je cela linija bila prazna (npr. samo komentar unutar nje)
    if (j > 0) {
        if (izlaz[j - 1] != ';') {
            return false; // Javlja grešku glavnom programu da fali tačka-zarez
        }
    }

    return true;
}
