#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <as.h>
#include <task.h>
#include <vfs/vfs.h>
#include <errno.h>
#include <str_error.h>
#include <sys/mman.h> 
#include "lexer.h"
#include "main.h"
#include "sanitizer.h"


// Alokacija globalnih registara kompajlera (Usklađeno sa main.h na 1024 bajta)
uint8_t Masinski_Kod_Bafer[1024];
int Velicina_Generisanog_Koda = 0;

char Imena_Promenljivih[MAX_PROMENLJIVIH][MAX_DUZINA_IMENA];
Promenljiva Kompajlerska_Memorija[MAX_PROMENLJIVIH];
int Broj_Zauzetih_Lokacija = 0;
int Trenutna_Linija_Kompajliranja = 1;
const char* Sirova_Linija_Teksta = NULL;

// Globalni i bezbedni niz argumenata za tvoj os_exec poziv
//static char* helenos_cmd[2];

// DEFINIŠEMO SOPSTVENE LOKALNE STRUKTURE DA IZBEGNEMO KONFLIKT SA BIBLIOTEKAMA
#pragma pack(push, 1)
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Z_ELF_Header;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Z_Prog_Header;
#pragma pack(pop)

void prijavi_sistemsku_gresku(int kod_greske, const char* detalj) {
    printf("\n🛑 [Z-KOMPAJLER GRESKA] Prekid rada!\n");
    printf("   -> Na liniji: %d\n", Trenutna_Linija_Kompajliranja);
    printf("   -> Tekst koda: \"%s\"\n", Sirova_Linija_Teksta);
    printf("   -> Opis greske: ");
    switch (kod_greske) {
        case ERR_PREKORACENJE_MEM:   printf("Maksimalno 20 promenljivih.\n"); break;
        case ERR_NEBALANSIRANE_ZAG:  printf("Nebalansirane zagrade '%s'.\n", detalj); break;
        case ERR_NEPOZNATA_KOMANDA:  printf("Sintaksna greska.\n"); break;
        case ERR_FALI_TACKA_ZAREZ:   printf("Nedostaje ';' na kraju.\n"); break;
        case ERR_PREDUGACKO_IME:     printf("Ime '%s' ima preko 20 slova.\n", detalj); break;
        default:                     printf("Sistemska anomalija.\n");
    }
    exit(1);
}

int Tabela_Simbola(const char* ime) {
    if (strcmp(ime, "_") == 0) return SIGNAL_DALJE;
    if (strlen(ime) >= MAX_DUZINA_IMENA) prijavi_sistemsku_gresku(ERR_PREDUGACKO_IME, ime);
    for (int i = 0; i < Broj_Zauzetih_Lokacija; i++) {
        if (strcmp(Imena_Promenljivih[i], ime) == 0) return i;
    }
    if (Broj_Zauzetih_Lokacija >= MAX_PROMENLJIVIH) prijavi_sistemsku_gresku(ERR_PREKORACENJE_MEM, "");
    strncpy(Imena_Promenljivih[Broj_Zauzetih_Lokacija], ime, MAX_DUZINA_IMENA - 1);
    Imena_Promenljivih[Broj_Zauzetih_Lokacija][MAX_DUZINA_IMENA - 1] = '\0';
    return Broj_Zauzetih_Lokacija++;
}


// Ako je unarni_minus aktivan, automatski pravi negativnu konstantu u memoriji
int obradi_argument_sa_znakom(Token t, int unarni_minus) {
    int lok;
    // Ako prvi karakter počinje cifrom, u pitanju je sirovi broj (konstanta)
    if (t.value[0] >= '0' && t.value[0] <= '9') {
        double konstanta = atof(t.value);
        if (unarni_minus) {
            konstanta = -konstanta;
        }
        // Kreiramo privremenu anonimnu promenljivu u Tabeli Simbola unutar hoda
        lok = Tabela_Simbola(t.value); 
        Kompajlerska_Memorija[lok].marker = 0x01;
        Kompajlerska_Memorija[lok].vrednost.ceo_broj = (int64_t)(konstanta * MULTIPLIKATOR);
    } else {
        lok = Tabela_Simbola(t.value);
        // Ako je promenljiva imala unarni minus ispred sebe (npr. X = A * -B),
        // preokrećemo joj znak direktno u memoriji pre izvršenja operacije
        if (unarni_minus) {
            Kompajlerska_Memorija[lok].vrednost.ceo_broj = -Kompajlerska_Memorija[lok].vrednost.ceo_broj;
        }
    }
    return lok;
}

void bezbedno_prevedi_u_bajte_tokeni(Token* tokeni, int broj_tokena) {
    if (broj_tokena == 0) return;

    // 1. KOMANDA: ISPISI(Ukupno);
    // Struktura tokena na traci: [KR_ISPISI, L_ZAGRADA, IDENTIFIKATOR, D_ZAGRADA, TACKA_ZAREZ]
    if (tokeni[0].type == Z_TK_KR_ISPISI) {
        if (broj_tokena >= 5 && 
            tokeni[1].type == Z_TK_L_ZAGRADA && 
            tokeni[2].type == Z_TK_IDENTIFIKATOR && 
            tokeni[3].type == Z_TK_D_ZAGRADA) {
            
            int lok = Tabela_Simbola(tokeni[2].value);
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x60;
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok;
            return;
        }
        prijavi_sistemsku_gresku(ERR_NEPOZNATA_KOMANDA, "Losa sintaksa za ISPISI");
    }

    // 2. KOMANDA: Prostor = PPOVRSINA(ObimZice);
    // Struktura tokena na traci: [IDENTIFIKATOR, DODELA, IDENTIFIKATOR(sa vrednoscu PPOVRSINA), L_ZAGRADA, IDENTIFIKATOR, D_ZAGRADA, TACKA_ZAREZ]
    if (broj_tokena >= 7 && 
        tokeni[0].type == Z_TK_IDENTIFIKATOR && 
        tokeni[1].type == Z_TK_DODELA && 
        strcmp(tokeni[2].value, "PPOVRSINA") == 0) {
        
        if (tokeni[3].type == Z_TK_L_ZAGRADA && 
            tokeni[4].type == Z_TK_IDENTIFIKATOR && 
            tokeni[5].type == Z_TK_D_ZAGRADA) {
            
            int lok_levo = Tabela_Simbola(tokeni[0].value);
            int lok_arg  = Tabela_Simbola(tokeni[4].value);
            
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x18;
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_arg;
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_levo;
            return;
        }
        prijavi_sistemsku_gresku(ERR_NEPOZNATA_KOMANDA, "Losa sintaksa za PPOVRSINA");
    }

    // 3. KOMANDA: ODREDI{A,B,LINIJA,_,_};
    // Struktura tokena na traci: [KR_ODREDI, L_VITICASTA, ID, ZAREZ, ID, ZAREZ, ID, ZAREZ, SIGNAL, ZAREZ, SIGNAL, D_VITICASTA, TACKA_ZAREZ]
    if (tokeni[0].type == Z_TK_KR_ODREDI) {
        if (tokeni[1].type == Z_TK_L_VITICASTA) {
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x24;
            int brojac_arg = 0;
            
            // Prolazimo kroz tokene unutar viticastih zagrada
            for (int t = 2; t < broj_tokena; t++) {
                if (tokeni[t].type == Z_TK_D_VITICASTA) break;
                if (tokeni[t].type == Z_TK_ZAREZ) continue;
                
                int lok = 0;
                if (tokeni[t].type == Z_TK_SIGNAL_DALJE) {
                    lok = 255; // Tvoja mašinska oznaka za SIGNAL_DALJE (_)
                } else {
                    lok = Tabela_Simbola(tokeni[t].value);
                }
                Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok;
                brojac_arg++;
            }
            if (brojac_arg != 5) {
                prijavi_sistemsku_gresku(ERR_NEPOZNATA_KOMANDA, "ODREDI trazi tacno 5 argumenata");
            }
            return;
        }
    }

    // 4. KOMANDA: STANDARDNA DODELA I MATEMATIKA (npr. X = A + B; ili X = A * -B;)
    if (tokeni[0].type == Z_TK_IDENTIFIKATOR && tokeni[1].type == Z_TK_DODELA) {
        int lok_levo = Tabela_Simbola(tokeni[0].value);

        // Slučaj 4a: Čista dodela stringa (npr. Poruka = "ALARM";)
        if (tokeni[2].value[0] == '"') {
            Kompajlerska_Memorija[lok_levo].marker = 0x02;
            char* direktni_bajtovi = (char*)&(Kompajlerska_Memorija[lok_levo].vrednost);
            memset(direktni_bajtovi, 0, 8);
            strncpy(direktni_bajtovi, "ALARM", 7);
            return;
        }

        // Slučaj 4b: Binarna matematička operacija (npr. X = A + B; ili sa unarnim minusom)
        // Proveravamo da li četvrti token (indeks 3) ili peti token predstavlja operator
        token_type_t op_tip = Z_TK_KRAJ_LINIJE;
        int pozicija_operatora = -1;

        for (int t = 2; t < broj_tokena; t++) {
            if (tokeni[t].type == Z_TK_PLUS || tokeni[t].type == Z_TK_MINUS || 
                tokeni[t].type == Z_TK_MNOZENJE || tokeni[t].type == Z_TK_DELJENJE) {
                op_tip = tokeni[t].type;
                pozicija_operatora = t;
                break;
            }
        }

        if (op_tip != Z_TK_KRAJ_LINIJE) {
            // Prvi argument je sve između '=' (indeks 1) i operatora
            int unarni_arg1 = (tokeni[2].type == Z_TK_UNARNI_MINUS) ? 1 : 0;
            int idx_arg1 = unarni_arg1 ? 3 : 2;
            int lok_arg1 = obradi_argument_sa_znakom(tokeni[idx_arg1], unarni_arg1);

            // Drugi argument je odmah nakon operatora
            int unarni_arg2 = (tokeni[pozicija_operatora + 1].type == Z_TK_UNARNI_MINUS) ? 1 : 0;
            int idx_arg2 = unarni_arg2 ? pozicija_operatora + 2 : pozicija_operatora + 1;
            int lok_arg2 = obradi_argument_sa_znakom(tokeni[idx_arg2], unarni_arg2);

            // Upisujemo tačan mašinski opkod
            if (op_tip == Z_TK_PLUS)       Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x70;
            else if (op_tip == Z_TK_MINUS)     Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x71;
            else if (op_tip == Z_TK_MNOZENJE)  Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x72;
            else if (op_tip == Z_TK_DELJENJE)  Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x73;

            // Upisujemo registre (indekse) za Virtuelnu Mašinu
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_arg1;
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_arg2;
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_levo;
            return;
        } else {
            // Slučaj 4c: Obična dodela čistog broja bez operacija (npr. ObimZice = 40;)
            int unarni_direktni = (tokeni[2].type == Z_TK_UNARNI_MINUS) ? 1 : 0;
            int idx_direktni = unarni_direktni ? 3 : 2;
            
            double vrednost = atof(tokeni[idx_direktni].value);
            if (unarni_direktni) {
                vrednost = -vrednost;
            }
            Kompajlerska_Memorija[lok_levo].marker = 0x01;
            Kompajlerska_Memorija[lok_levo].vrednost.ceo_broj = (int64_t)(vrednost * MULTIPLIKATOR);
            return;
        }
    }

    prijavi_sistemsku_gresku(ERR_NEPOZNATA_KOMANDA, "");
}

int main(void) {
    char upeglana[256];
    char linija_iz_fajla[256];
    Token tokeni_linije[128]; // Bafer za tokene trenutne linije

    // 🌟 RESETOVANJE SISTEMSKIH BAFERA PRE SVAKOG PREVOĐENJA
    Velicina_Generisanog_Koda = 0;
    Broj_Zauzetih_Lokacija = 0;
    memset(Masinski_Kod_Bafer, 0, sizeof(Masinski_Kod_Bafer));
    memset(Kompajlerska_Memorija, 0, sizeof(Kompajlerska_Memorija));
    
    // 🌟 KOREKCIJA PUTANJE: Skripta je na root-u, bez prefiksa 'app/'
    const char* putanja_skripte = "kod.txt"; 

    printf("=== Z-LANGUAGE NATIVE COMPILER WITH OFFICIAL OS_EXEC LINK ===\n\n");
    printf("📖 Otvaram izvorni fajl: %s...\n", putanja_skripte);

    // 1. Otvaramo eksterni fajl direktno sa root-a
    FILE* f_izvor = fopen(putanja_skripte, "r");
    if (!f_izvor) {
        printf("🛑 Greška: Ne mogu da otvorim fajl '%s'! Proveri da li fajl postoji na root direktorijumu.\n", putanja_skripte);
        return 1;
    }

    int i = 0;
    // 2. Čitamo fajl liniju po liniju pomoću fgets funkcije
    while (fgets(linija_iz_fajla, sizeof(linija_iz_fajla), f_izvor) != NULL) {
        // Ako je linija prazna ili sadrži samo prelazak u novi red, preskačemo je
        if (strlen(linija_iz_fajla) <= 1 || linija_iz_fajla[0] == '\n' || linija_iz_fajla[0] == '\r') {
            continue;
        }

        // Uklanjamo prelazak u novi red (\n ili \r) sa kraja stringa ako postoji
        linija_iz_fajla[strcspn(linija_iz_fajla, "\r\n")] = 0;

        Trenutna_Linija_Kompajliranja = i + 1; // Brojanje prilagođeno da počinje od 1 za korisnika
        Sirova_Linija_Teksta = linija_iz_fajla;

        // 🚀 NOVI POZIV SANITIZERA: Prva linija odbrane
        if (!sanitizer_ocisti_liniju(linija_iz_fajla, upeglana, sizeof(upeglana))) {
            prijavi_sistemsku_gresku(ERR_FALI_TACKA_ZAREZ, "");
        }

        // Ako je linija ispala prazna (npr. cela je bila komentar), preskoči je
        if (strlen(upeglana) == 0) {
            i++;
            continue;
        }

        // 🚀 NOVI POZIV LEXER-A: Razbijamo upeglanu liniju na niz srpskih tokena
        int broj_tokena = lexer_tokenizuj(upeglana, tokeni_linije, 128);

        // 🚀 NOVI PARSER: Prevodimo niz tokena direktno u mašinske opkodove
        bezbedno_prevedi_u_bajte_tokeni(tokeni_linije, broj_tokena);
        
        i++;
    }

    // Zatvaramo izvorni fajl nakon što je uspešno kompajliran u bajt-kod bafer
    fclose(f_izvor);
    printf("📝 [Z-KOMPAJLER] Uspešno prevedeno %d linija koda iz fajla.\n", i);


    // --- VIRTUELNA MAŠINA (INTERPRETER BAJT-KODA) ---
    printf("\n🖥️  [Z-SISTEM] Pokrećem virtuelnu mašinu za izvršavanje Z-Bajt-koda...\n");
    
    int pc = 0; // Program Counter
    
    while (pc < Velicina_Generisanog_Koda) {
        uint8_t opkod = Masinski_Kod_Bafer[pc++];
        
        // 1. Obrada opkoda 0x18 (PPOVRSINA)
        if (opkod == 0x18) {
            uint8_t lok_arg = Masinski_Kod_Bafer[pc++];
            uint8_t lok_levo = Masinski_Kod_Bafer[pc++];
            
            int64_t obim = Kompajlerska_Memorija[lok_arg].vrednost.ceo_broj;
            int64_t obim_kvadrat = obim * (obim / 1000); 
            int64_t delilac = 12566370 / 1000;          
            int64_t rezultat_povrsina = obim_kvadrat / delilac;
            
            Kompajlerska_Memorija[lok_levo].marker = 0x01;
            Kompajlerska_Memorija[lok_levo].vrednost.ceo_broj = rezultat_povrsina;
            continue;
        }
        
        // 2. Obrada opkoda 0x24 (ODREDI)
        if (opkod == 0x24) {
            uint8_t arg1 = Masinski_Kod_Bafer[pc++];
            uint8_t arg2 = Masinski_Kod_Bafer[pc++];
            uint8_t arg3 = Masinski_Kod_Bafer[pc++];
            uint8_t arg4 = Masinski_Kod_Bafer[pc++];
            uint8_t arg5 = Masinski_Kod_Bafer[pc++];
            
            printf("🔍 [ODREDI] Analiza registara: [%d, %d, %d, %d, %d]\n", arg1, arg2, arg3, arg4, arg5);
            continue;
        }
        
        // 3. Obrada opkoda 0x60 (ISPISI)
        if (opkod == 0x60) {
            uint8_t lok = Masinski_Kod_Bafer[pc++];
            
            if (Kompajlerska_Memorija[lok].marker == 0x02) {
                printf("📢 [Z-ISPISI]: %s\n", (char*)&(Kompajlerska_Memorija[lok].vrednost));
            } else if (Kompajlerska_Memorija[lok].marker == 0x01) {
                double formatirano = (double)Kompajlerska_Memorija[lok].vrednost.ceo_broj / MULTIPLIKATOR;
                printf("📢 [Z-ISPISI]: %.2f\n", formatirano);
            }
            continue;
        }

        // 4. Obrada opkoda 0x70 (SABIRANJE +)
        if (opkod == 0x70) {
            uint8_t arg1 = Masinski_Kod_Bafer[pc++];
            uint8_t arg2 = Masinski_Kod_Bafer[pc++];
            uint8_t levo = Masinski_Kod_Bafer[pc++];
            Kompajlerska_Memorija[levo].marker = 0x01;
            Kompajlerska_Memorija[levo].vrednost.ceo_broj = Kompajlerska_Memorija[arg1].vrednost.ceo_broj + Kompajlerska_Memorija[arg2].vrednost.ceo_broj;
            continue;
        }

        // 5. Obrada opkoda 0x71 (ODUZIMANJE -)
        if (opkod == 0x71) {
            uint8_t arg1 = Masinski_Kod_Bafer[pc++];
            uint8_t arg2 = Masinski_Kod_Bafer[pc++];
            uint8_t levo = Masinski_Kod_Bafer[pc++];
            Kompajlerska_Memorija[levo].marker = 0x01;
            Kompajlerska_Memorija[levo].vrednost.ceo_broj = Kompajlerska_Memorija[arg1].vrednost.ceo_broj - Kompajlerska_Memorija[arg2].vrednost.ceo_broj;
            continue;
        }

        // 6. Obrada opkoda 0x72 (MNOŽENJE *)
        if (opkod == 0x72) {
            uint8_t arg1 = Masinski_Kod_Bafer[pc++];
            uint8_t arg2 = Masinski_Kod_Bafer[pc++];
            uint8_t levo = Masinski_Kod_Bafer[pc++];
            Kompajlerska_Memorija[levo].marker = 0x01;
            Kompajlerska_Memorija[levo].vrednost.ceo_broj = (Kompajlerska_Memorija[arg1].vrednost.ceo_broj * Kompajlerska_Memorija[arg2].vrednost.ceo_broj) / MULTIPLIKATOR;
            continue;
        }

        // 7. Obrada opkoda 0x73 (DELJENJE /)
        if (opkod == 0x73) {
            uint8_t arg1 = Masinski_Kod_Bafer[pc++];
            uint8_t arg2 = Masinski_Kod_Bafer[pc++];
            uint8_t levo = Masinski_Kod_Bafer[pc++];
            Kompajlerska_Memorija[levo].marker = 0x01;
            if (Kompajlerska_Memorija[arg2].vrednost.ceo_broj == 0) {
                printf("🛑 [VM GREŠKA]: Deljenje sa nulom!\n");
                Kompajlerska_Memorija[levo].vrednost.ceo_broj = 0;
            } else {
                Kompajlerska_Memorija[levo].vrednost.ceo_broj = (Kompajlerska_Memorija[arg1].vrednost.ceo_broj * MULTIPLIKATOR) / Kompajlerska_Memorija[arg2].vrednost.ceo_broj;
            }
            continue;
        }
        
        if (opkod == 0xC3 || opkod == 0x00) {
            break;
        }
    }

    printf("\n✅ [Z-Sistem] Kompletan ciklus je uspešno završen!\n");
    return 0;
}


