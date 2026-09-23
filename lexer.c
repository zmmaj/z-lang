#include "lexer.h"
#include <string.h>
#include <ctype.h>

int lexer_tokenizuj(const char* ulaz, Token* izlaz_tokeni, int max_tokena) {
    int i = 0;
    int t_idx = 0;
    token_type_t last_type = Z_TK_KRAJ_LINIJE; // Pratimo prethodni token zbog unarnog minusa

    while (ulaz[i] != '\0' && t_idx < max_tokena) {
        // Preskačemo razmake van stringova
        if (isspace((unsigned char)ulaz[i])) {
            i++;
            continue;
        }

        Token* t = &izlaz_tokeni[t_idx];
        memset(t, 0, sizeof(Token));

        // 1. Zagrade i jednostavni interpunkcijski znakovi
        if (ulaz[i] == '(') { t->type = Z_TK_L_ZAGRADA;   strcpy(t->value, "("); i++; last_type = Z_TK_L_ZAGRADA;   t_idx++; continue; }
        if (ulaz[i] == ')') { t->type = Z_TK_D_ZAGRADA;   strcpy(t->value, ")"); i++; last_type = Z_TK_D_ZAGRADA;   t_idx++; continue; }
        if (ulaz[i] == '{') { t->type = Z_TK_L_VITICASTA; strcpy(t->value, "{"); i++; last_type = Z_TK_L_VITICASTA; t_idx++; continue; }
        if (ulaz[i] == '}') { t->type = Z_TK_D_VITICASTA; strcpy(t->value, "}"); i++; last_type = Z_TK_D_VITICASTA; t_idx++; continue; }
        if (ulaz[i] == ',') { t->type = Z_TK_ZAREZ;       strcpy(t->value, ","); i++; last_type = Z_TK_ZAREZ;       t_idx++; continue; }
        if (ulaz[i] == ';') { t->type = Z_TK_TACKA_ZAREZ; strcpy(t->value, ";"); i++; last_type = Z_TK_TACKA_ZAREZ; t_idx++; continue; }
        if (ulaz[i] == '=') { t->type = Z_TK_DODELA;      strcpy(t->value, "="); i++; last_type = Z_TK_DODELA;      t_idx++; continue; }
        if (ulaz[i] == '+') { t->type = Z_TK_PLUS;        strcpy(t->value, "+"); i++; last_type = Z_TK_PLUS;        t_idx++; continue; }
        if (ulaz[i] == '*') { t->type = Z_TK_MNOZENJE;    strcpy(t->value, "*"); i++; last_type = Z_TK_MNOZENJE;    t_idx++; continue; }
        if (ulaz[i] == '/') { t->type = Z_TK_DELJENJE;    strcpy(t->value, "/"); i++; last_type = Z_TK_DELJENJE;    t_idx++; continue; }
        
        // Specijalni SIGNAL_DALJE za tvoju ODREDI funkciju
        if (ulaz[i] == '_') { t->type = Z_TK_SIGNAL_DALJE; strcpy(t->value, "_"); i++; last_type = Z_TK_SIGNAL_DALJE; t_idx++; continue; }

        // 2. 🛡️ ŠTIT ZA MINUS (Zvanična C logika prilagođena srpskim tokenima)
        if (ulaz[i] == '-') {
            // Ako je minus na početku koda, ili iza znaka dodele, otvorenih zagrada ili zareza...
            if (last_type == Z_TK_KRAJ_LINIJE || last_type == Z_TK_DODELA || 
                last_type == Z_TK_PLUS || last_type == Z_TK_MINUS || 
                last_type == Z_TK_MNOZENJE || last_type == Z_TK_DELJENJE || 
                last_type == Z_TK_L_ZAGRADA || last_type == Z_TK_L_VITICASTA || 
                last_type == Z_TK_ZAREZ) {
                
                t->type = Z_TK_UNARNI_MINUS; // Označavamo ga kao PREDZNAK broja
            } else {
                t->type = Z_TK_MINUS;        // Između dve varijable/broja, pa je to ODUZIMANJE
            }
            strcpy(t->value, "-");
            i++;
            last_type = t->type;
            t_idx++;
            continue;
        }

        // 3. Reči, brojevi i komande (Identifikatori)
        if (isalnum((unsigned char)ulaz[i])) {
            int v_idx = 0;
            while (isalnum((unsigned char)ulaz[i]) && v_idx < 63) {
                t->value[v_idx++] = ulaz[i++];
            }
            t->value[v_idx] = '\0';

            // Prepoznavanje ključnih reči našeg jezika
            if (strcmp(t->value, "ODREDI") == 0) {
                t->type = Z_TK_KR_ODREDI;
            } else if (strcmp(t->value, "ISPISI") == 0) {
                t->type = Z_TK_KR_ISPISI;
            } else {
                t->type = Z_TK_IDENTIFIKATOR;
            }

            last_type = t->type;
            t_idx++;
            continue;
        }

        // Nepoznat karakter (Sistemska greška)
        t->type = Z_TK_GRESKA;
        t->value[0] = ulaz[i];
        t->value[1] = '\0';
        i++;
        t_idx++;
    }

    return t_idx; // Vraćamo ukupan broj prepoznatih tokena u liniji
}
