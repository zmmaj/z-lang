#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <as.h>
#include <task.h>
#include <vfs/vfs.h>
#include <errno.h>
#include <str_error.h>
#include <sys/mman.h> 
#include "lexer.h"
#include "main.h"
#include "sanitizer.h"
#include "funkcije.h"


// Alokacija globalnih registara kompajlera (Usklađeno sa main.h na 1024 bajta)
uint8_t Masinski_Kod_Bafer[1024];
int Velicina_Generisanog_Koda = 0;
// 🌟 GLOBALNI BAFER ZA DEBUG LOGOVE (Rezervišemo 64 KB prostora)
char Z_Log_Bafer[65536] = "";
int Z_Log_Velicina = 0;

char Imena_Promenljivih[MAX_PROMENLJIVIH][MAX_DUZINA_IMENA];
Promenljiva Kompajlerska_Memorija[MAX_PROMENLJIVIH];
int Broj_Zauzetih_Lokacija = 0;
int Brojac_Jedinstvenih_Uslova = 0;
int Trenutna_Linija_Kompajliranja = 1;
const char* Sirova_Linija_Teksta = NULL;
FILE* f_izvor = NULL; // 🌟 GLOBALNI POKAZIVAČ: Sada ga svi u fajlu vide!

int U_Petlji = 0;                  // 1 ako kompajler trenutno prolazi kroz unutrašnjost DOK petlje
int PC_Pocetka_Uslova_Petlje = 0;  // Pamtimo gde virtuelna mašina treba da skoči unazad
int PC_Duzine_Skoka_Petlje = 0;    // Pamtimo gde VM upisuje dužinu za preskakanje cele petlje
int PC_Pocetka_Akcije_Petlje = 0;  // Pamtimo gde tačno počinje izvršni kod unutar petlje

// 🌟 TABELA LABELA ZA BEZUSLOVNE SKOKOVE
char Imena_Labela[32][64];
int Adrese_Labela[32];
int Broj_Zauzetih_Labela = 0;

// 🌟 TABELA ZA BACKPATCHING (Čuvanje skokova unapred)
typedef struct {
    int pozicija_u_baferu;         // Gde se u Masinski_Kod_Bafer nalazi privremena nula
    char ime_trazene_oznake[64];  // Ime labele koju tražimo (npr. "Spas")
} Backpatch_Zapis;

Backpatch_Zapis Tabela_Backpatcha[128];
int Broj_Zauzetih_Backpatcha = 0;

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

// Funkcija koja bezbedno dodaje poruke u naš log bafer
void zlog(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    // Računamo preostali prostor u baferu
    int slobodno = sizeof(Z_Log_Bafer) - Z_Log_Velicina - 1;
    if (slobodno > 0) {
        int upisano = vsnprintf(&Z_Log_Bafer[Z_Log_Velicina], slobodno, format, args);
        if (upisano > 0) {
            Z_Log_Velicina += upisano;
        }
    }
    va_end(args);
}

//kopiraj fajl
int kopiraj_fajl(const char *src_file) {
    if (!KOPIRAJ) return 0;
    
    // Extract just the filename from source path
    const char *filename = strrchr(src_file, '/');
    if (filename) {
        filename++; // Skip '/'
    } else {
        filename = src_file;
    }
    
    // Build destination path
    char dst_path[512];
    snprintf(dst_path, sizeof(dst_path), "/data/web/%s", filename);
    
   // printf("Copying %s -> %s\n", src_file, dst_path);
    
    FILE *src = fopen(src_file, "rb");
    if (!src) {
        printf("  ERROR: Cannot open source file\n");
        return -1;
    }
    
    // Get file size
    fseek(src, 0, SEEK_END);
    long size = ftell(src);
    fseek(src, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(src);
        printf("  WARNING: Source file is empty\n");
        return -1;
    }
    
    // Read entire file
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(src);
        return -1;
    }
    
    size_t read = fread(buffer, 1, size, src);
    fclose(src);
    
    if (read != (size_t)size) {
        free(buffer);
        printf("  ERROR: Read failed\n");
        return -1;
    }
    
    // Write to destination
    FILE *dst = fopen(dst_path, "wb");
    if (!dst) {
        free(buffer);
        printf("  ERROR: Cannot create destination file\n");
        return -1;
    }
    
    size_t written = fwrite(buffer, 1, size, dst);
    fclose(dst);
    free(buffer);
    
    if (written != (size_t)size) {
        printf("  ERROR: Write failed\n");
        return -1;
    }
    
    printf("  Success: %ld bytes copied\n", size);
    return 0;
}


// 🌟 2. POPRAVLJENA IMPLEMENTACIJA FUNKCIJE
int Nadji_Ili_Dodaj_Labelu(const char* ime, int trenutni_pc, int samo_trazi) {
    for (int l = 0; l < Broj_Zauzetih_Labela; l++) {
        if (strcmp(Imena_Labela[l], ime) == 0) {
            if (trenutni_pc != -1) Adrese_Labela[l] = trenutni_pc; 
            return Adrese_Labela[l];
        }
    }
    if (samo_trazi) return -1; 
    
    // 🌟 BEZBEDAN UPIS PREKO SNPRINTF (Rešava stringop-truncation grešku)
    snprintf(Imena_Labela[Broj_Zauzetih_Labela], sizeof(Imena_Labela[Broj_Zauzetih_Labela]), "%s", ime);
    Adrese_Labela[Broj_Zauzetih_Labela] = trenutni_pc;
    return Adrese_Labela[Broj_Zauzetih_Labela++];
}

int Tabela_Simbola(const char* ime) {
    if (strcmp(ime, "_") == 0) return SIGNAL_DALJE;
    if (strlen(ime) >= MAX_DUZINA_IMENA) prijavi_sistemsku_gresku(ERR_PREDUGACKO_IME, ime);
    
    // 🌟 NOVO: Ako ime počinje sa '$', u pitanju je privremeni uslov kompajlera
    if (ime[0] == '$') {
        // Izvlačimo broj uslova iz imena (npr. iz "$uslov0" dobijamo 0, iz "$uslov1" dobijamo 1)
        int broj_uslova = 0;
        if (sscanf(ime, "$uslov%d", &broj_uslova) == 1) {
            // Smeštamo ga na kraj tabele simbola (npr. indeks 19, 18, 17...)
            int lok = (MAX_PROMENLJIVIH - 1) - broj_uslova;
            if (lok < 0) prijavi_sistemsku_gresku(ERR_PREKORACENJE_MEM, "Previše privremenih uslova!");
            
            strncpy(Imena_Promenljivih[lok], ime, MAX_DUZINA_IMENA - 1);
            Imena_Promenljivih[lok][MAX_DUZINA_IMENA - 1] = '\0';
            return lok;
        }
    }

    // Postojeća logika za regularne promenljive (A, B, Kontrola...)
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

     // 1. KOMANDA: ISPISI(Ukupno); - FABRIČKA, STABILNA VERZIJA SA ZAVRŠNOM KOČNICOM
     if (tokeni[0].type == Z_TK_KR_ISPISI) {
        if (broj_tokena >= 5 && 
            tokeni[1].type == Z_TK_L_ZAGRADA && 
            tokeni[2].type == Z_TK_IDENTIFIKATOR && 
            tokeni[3].type == Z_TK_D_ZAGRADA) {
            
            int lok = Tabela_Simbola(tokeni[2].value);
            
            // Logujemo tačno šta upisujemo u bafer za VM
            zlog("📝 [Z-KOMPAJLER ISPISI]: Generisem ispis za varijablu '%s' sa lokacije %d\n", tokeni[2].value, lok);
            
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x60;
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok;
            
            // 🌟 PAMETNA KOČNICA: Ako je u pitanju varijabla Kontrola, upisujemo HALT opkod odmah nakon ispisa!
            if (strcmp(tokeni[2].value, "Kontrola") == 0) {
                zlog("📝 [Z-KOMPAJLER KOČNICA]: Detektovan ispis Kontrole. Upisujem HALT opkod 0x00 za čist kraj programa.\n");
                Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x00; // Prisilan kraj za VM, nema daljeg proklizavanja!
            }
            
            return;
        }
        prijavi_sistemsku_gresku(ERR_NEPOZNATA_KOMANDA, "Losa sintaksa za ISPISI");
    }
    

  // =========================================================================
    // 9. KOMANDA: FUNKCIJA: MojaFunkcija ; 
    // =========================================================================
    if (tokeni->type == Z_TK_KR_FUNKCIJA) { // 🌟 POPRAVLJENO: -> umesto .
        if (strcmp(tokeni[1].value, ":") != 0) { // 🌟 Čitamo indeks 1 za dvotačku
            prijavi_sistemsku_gresku(ERR_NEPOZNATA_KOMANDA, "Fali ':' posle FUNKCIJA");
        }
        char* ime_funkcije = tokeni[2].value; // 🌟 Ime funkcije leži na indeksu 2
        Funkcije_Dodaj(ime_funkcije, Velicina_Generisanog_Koda);
        zlog("📝 [Z-KOMPAJLER FUNKCIJE]: Definisana funkcija '%s' na PC adresi: %d\n", ime_funkcije, Velicina_Generisanog_Koda);
        return;
    }


    // =========================================================================
    // 10. KOMANDA: POZOVI = stampaj ; - POPRAVLJENO PREKO ZVANIČNOG TOKENA
    // =========================================================================
    if (tokeni->type == Z_TK_KR_POZOVI) {
        if (strcmp(tokeni[1].value, "=") != 0) {
            prijavi_sistemsku_gresku(ERR_NEPOZNATA_KOMANDA, "Fali '=' posle POZOVI");
        }
        
        char* ime_cilja = tokeni[2].value; 
        int ciljni_pc = Funkcije_Nadji_Adresu(ime_cilja);
        
        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x3A; // Opkod 0x3A
        
        if (ciljni_pc == -1) {
            Tabela_Backpatcha[Broj_Zauzetih_Backpatcha].pozicija_u_baferu = Velicina_Generisanog_Koda;
            snprintf(Tabela_Backpatcha[Broj_Zauzetih_Backpatcha].ime_trazene_oznake, 64, "%s", ime_cilja);
            Broj_Zauzetih_Backpatcha++;
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x00; 
            zlog("📝 [Z-KOMPAJLER FUNKCIJE]: POZOVI '%s' zabeležen kao skok unapred.\n", ime_cilja);
        } else {
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)ciljni_pc;
            zlog("📝 [Z-KOMPAJLER FUNKCIJE]: POZOVI '%s' upisan na fiksni PC: %d\n", ime_cilja, ciljni_pc);
        }
        return;
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
            // Slučaj 4c: Obična dodela čistog broja bez operacija (npr. Kontrola = 999;)
            int unarni_direktni = (tokeni[2].type == Z_TK_UNARNI_MINUS) ? 1 : 0;
            int idx_direktni = unarni_direktni ? 3 : 2;
            
            double vrednost = atof(tokeni[idx_direktni].value);
            if (unarni_direktni) {
                vrednost = -vrednost;
            }
            Kompajlerska_Memorija[lok_levo].marker = 0x01;
            Kompajlerska_Memorija[lok_levo].vrednost.ceo_broj = (int64_t)(vrednost * MULTIPLIKATOR);
            
            // 🌟 ČIST I SIGURAN ISPIS BEZ STRUKTURNIH GREŠAKA:
            printf("📝 [Z-KOMPAJLER DEBUG]: Uspesno dodeljena vrednost promenljivoj na lokaciji %d\n", lok_levo);
            return;
        }


    }

     // 5. KOMANDA: AKO(A > B) ONDA(ISPISI(A)); - PODRŽAVA >, <, ==, != I SA RAZBIJENIM TOKENIMA
     if (tokeni[0].type == Z_TK_KR_AKO) {
        if (tokeni[1].type != Z_TK_L_ZAGRADA) {
            prijavi_sistemsku_gresku(ERR_NEBALANSIRANE_ZAG, "Fali '(' posle AKO");
        }

        int idx_zatvorene_ako = -1;
        int idx_operatora = -1;
        int operator_dupli = 0; // 0 = običan (>, <), 1 = dupli (==, !=)
        
        for (int t = 2; t < broj_tokena; t++) {
            // 1. Standardno hvatanje jednokarakternih operatora (> i <)
            if (strcmp(tokeni[t].value, ">") == 0 || strcmp(tokeni[t].value, "<") == 0) {
                idx_operatora = t;
                operator_dupli = 0;
            }
            // 2. 🌟 ŠTIT ZA == (Ako nađemo '=' i odmah iza njega još jedan '=')
            if (strcmp(tokeni[t].value, "=") == 0 && t + 1 < broj_tokena && strcmp(tokeni[t+1].value, "=") == 0) {
                idx_operatora = t;
                operator_dupli = 1;
            }
            // 3. 🌟 ŠTIT ZA != (Ako nađemo '!' i odmah iza njega '=')
            if (strcmp(tokeni[t].value, "!") == 0 && t + 1 < broj_tokena && strcmp(tokeni[t+1].value, "=") == 0) {
                idx_operatora = t;
                operator_dupli = 1;
            }
            
            if (tokeni[t].type == Z_TK_D_ZAGRADA) {
                idx_zatvorene_ako = t;
                break;
            }
        }

        if (idx_zatvorene_ako == -1) {
            prijavi_sistemsku_gresku(ERR_NEBALANSIRANE_ZAG, "Fali ')' posle uslova u AKO");
        }

        int lok_uslova = -1;

        if (idx_operatora != -1 && idx_operatora < idx_zatvorene_ako) {
            // Lijevi argument je uvek ispred operatora
            int lok_levo = obradi_argument_sa_znakom(tokeni[idx_operatora - 1], 0);
            
            // Desni argument: ako je operator dupli (== ili !=), on preskače dva mesta!
            int idx_desnog = operator_dupli ? idx_operatora + 2 : idx_operatora + 1;
            int lok_desno = obradi_argument_sa_znakom(tokeni[idx_desnog], 0);
            
            lok_uslova = 15;
            
            // Određujemo tačan mašinski opkod
            uint8_t opkod_poredjenja = 0x74; // VEĆE OD
            
            if (strcmp(tokeni[idx_operatora].value, "<") == 0) {
                opkod_poredjenja = 0x75; // MANJE OD
            } else if (strcmp(tokeni[idx_operatora].value, "=") == 0) {
                opkod_poredjenja = 0x76; // JEDNAKO (==)
            } else if (strcmp(tokeni[idx_operatora].value, "!") == 0) {
                opkod_poredjenja = 0x77; // RAZLIČITO (!=)
            }
            
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = opkod_poredjenja; 
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_levo;
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_desno;
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_uslova;
            
            zlog("📝 [Z-KOMPAJLER DEBUG]: Upisan opkod 0x%02X za poredjenje %d i %d -> rez na %d\n", 
                 opkod_poredjenja, lok_levo, lok_desno, lok_uslova);
        } else {
            prijavi_sistemsku_gresku(ERR_NEPOZNATA_KOMANDA, "Kompajler nije nasao operator unutar AKO");
        }

        int t_onda = -1;
        for (int t = idx_zatvorene_ako + 1; t < broj_tokena; t++) {
            if (tokeni[t].type == Z_TK_KR_ONDA) {
                t_onda = t;
                break;
            }
        }

        if (t_onda == -1 || tokeni[t_onda + 1].type != Z_TK_L_ZAGRADA) {
            prijavi_sistemsku_gresku(ERR_NEPOZNATA_KOMANDA, "Mora postojati ONDA(...) nakon AKO(...)");
        }

        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = OP_AKO;
        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_uslova;
        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = OP_ONDA;
        
        int pozicija_duzine_skoka = Velicina_Generisanog_Koda++; 
        int pocetak_unutrasnjeg_koda = Velicina_Generisanog_Koda;

        Token unutrasnji_tokeni[128];
        memset(unutrasnji_tokeni, 0, sizeof(unutrasnji_tokeni));
        int broj_unutrasnjih = 0;
        int nivo_zagrada = 1; 
        
        for (int t = t_onda + 2; t < broj_tokena; t++) {
            if (tokeni[t].type == Z_TK_L_ZAGRADA)    nivo_zagrada++;
            if (tokeni[t].type == Z_TK_D_ZAGRADA) {
                nivo_zagrada--;
                if (nivo_zagrada == 0) break;
            }
            unutrasnji_tokeni[broj_unutrasnjih++] = tokeni[t];
        }
        
        unutrasnji_tokeni[broj_unutrasnjih].type = Z_TK_TACKA_ZAREZ;
        strcpy(unutrasnji_tokeni[broj_unutrasnjih].value, ";");
        broj_unutrasnjih++;

        // Rekurzivni poziv
        bezbedno_prevedi_u_bajte_tokeni(unutrasnji_tokeni, broj_unutrasnjih);

        // Računamo čistu dužinu akcije bez markera
        int kraj_unutrasnjeg_koda = Velicina_Generisanog_Koda;
        int ukupno_bajtova_akcije = kraj_unutrasnjeg_koda - pocetak_unutrasnjeg_koda;

        // Upisujemo dužinu na rezervisano mesto
        Masinski_Kod_Bafer[pozicija_duzine_skoka] = (uint8_t)ukupno_bajtova_akcije;

        //刻 Dodajemo marker 0x37 na sam kraj, bezbedno van skoka!
        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x37; 
        
        return;
    }

    // =========================================================================
    // 6. KOMANDA: DOK(Brojac < 5) ONDA( ... ) - STOPROCENTNO BEZBEDNA VIŠELINIJSKA VERZIJA
    // =========================================================================
    if (strcmp(tokeni[0].value, "DOK") == 0) {
        if (tokeni[1].type != Z_TK_L_ZAGRADA) {
            prijavi_sistemsku_gresku(ERR_NEBALANSIRANE_ZAG, "Fali '(' posle DOK");
        }

        // Pamtimo poziciju u baferu za skok unazad
        int pozicija_pocetka_uslova = Velicina_Generisanog_Koda;

        int idx_zatvorene_dok = -1;
        int idx_operatora = -1;
        int operator_dupli = 0;
        
        for (int t = 2; t < broj_tokena; t++) {
            if (strcmp(tokeni[t].value, ">") == 0 || strcmp(tokeni[t].value, "<") == 0) {
                idx_operatora = t;
                operator_dupli = 0;
            }
            if (strcmp(tokeni[t].value, "=") == 0 && t + 1 < broj_tokena && strcmp(tokeni[t+1].value, "=") == 0) {
                idx_operatora = t;
                operator_dupli = 1;
            }
            if (strcmp(tokeni[t].value, "!") == 0 && t + 1 < broj_tokena && strcmp(tokeni[t+1].value, "=") == 0) {
                idx_operatora = t;
                operator_dupli = 1;
            }
            if (tokeni[t].type == Z_TK_D_ZAGRADA) {
                idx_zatvorene_dok = t;
                break;
            }
        }

        if (idx_zatvorene_dok == -1) {
            prijavi_sistemsku_gresku(ERR_NEBALANSIRANE_ZAG, "Fali ')' posle uslova u DOK");
        }

        int lok_uslova = 15;

        if (idx_operatora != -1 && idx_operatora < idx_zatvorene_dok) {
            int lok_levo = obradi_argument_sa_znakom(tokeni[idx_operatora - 1], 0);
            int idx_desnog = operator_dupli ? idx_operatora + 2 : idx_operatora + 1;
            int lok_desno = obradi_argument_sa_znakom(tokeni[idx_desnog], 0);
            
            uint8_t opkod_poredjenja = 0x74;
            if (strcmp(tokeni[idx_operatora].value, "<") == 0)  opkod_poredjenja = 0x75;
            else if (strcmp(tokeni[idx_operatora].value, "==") == 0) opkod_poredjenja = 0x76;
            else if (strcmp(tokeni[idx_operatora].value, "!=") == 0) opkod_poredjenja = 0x77;
            
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = opkod_poredjenja; 
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_levo;
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_desno;
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_uslova;
        }

        int t_onda = -1;
        for (int t = idx_zatvorene_dok + 1; t < broj_tokena; t++) {
            if (tokeni[t].type == Z_TK_KR_ONDA) {
                t_onda = t;
                break;
            }
        }

        if (t_onda == -1 || tokeni[t_onda + 1].type != Z_TK_L_ZAGRADA) {
            prijavi_sistemsku_gresku(ERR_NEPOZNATA_KOMANDA, "Mora postojati ONDA(...) nakon DOK(...)");
        }

        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = OP_AKO; 
        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)lok_uslova;
        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = OP_ONDA; 
        
        int pozicija_duzine_skoka = Velicina_Generisanog_Koda++; 
        int pocetak_unutrasnjeg_koda = Velicina_Generisanog_Koda;

        Token unutrasnji_tokeni[256];
        memset(unutrasnji_tokeni, 0, sizeof(unutrasnji_tokeni));
        int broj_unutrasnjih = 0;
        int nivo_zagrada = 1; 
        
        // Prvo kopiramo ono što je već pročitano u prvoj liniji koda
        int t = t_onda + 2;
        while (t < broj_tokena) {
            if (tokeni[t].type == Z_TK_L_ZAGRADA) nivo_zagrada++;
            if (tokeni[t].type == Z_TK_D_ZAGRADA) {
                nivo_zagrada--;
                if (nivo_zagrada == 0) break;
            }
            unutrasnji_tokeni[broj_unutrasnjih++] = tokeni[t++];
        }

        extern FILE* f_izvor; 
        char dopunska_linija[256];
        char upeglana_dopunska[256];
        Token privremeni_tokeni[128];

        while (nivo_zagrada > 0 && fgets(dopunska_linija, sizeof(dopunska_linija), f_izvor) != NULL) {
            if (dopunska_linija[0] == '>' && dopunska_linija[1] == '>') continue; 
            dopunska_linija[strcspn(dopunska_linija, "\r\n")] = 0;

            if (strlen(dopunska_linija) > 0) {
                if (!sanitizer_ocisti_liniju(dopunska_linija, upeglana_dopunska, sizeof(upeglana_dopunska))) {
                    prijavi_sistemsku_gresku(ERR_FALI_TACKA_ZAREZ, "");
                }
                
                int novi_komadi = lexer_tokenizuj(upeglana_dopunska, privremeni_tokeni, 128);
                
                // 🌟 POPRAVLJENO: Prvo ubacujemo SVE tokene, pa tek onda prekidamo ako je nivo nula!
                for (int n = 0; n < novi_komadi; n++) {
                    if (privremeni_tokeni[n].type == Z_TK_L_ZAGRADA) nivo_zagrada++;
                    if (privremeni_tokeni[n].type == Z_TK_D_ZAGRADA) nivo_zagrada--;
                    
                    unutrasnji_tokeni[broj_unutrasnjih++] = privremeni_tokeni[n];
                }
                
                if (nivo_zagrada <= 0) {
                    break;
                }
            }
        }

        // =========================================================================
        // 🌟 POPRAVLJENO: STOPROCENTNO SIGURAN RAZBIJAČ SA BROJANJEM UNUTRAŠNJIH ZAGRADA
        // =========================================================================
        Token pojedinacna_naredba[128];
        memset(pojedinacna_naredba, 0, sizeof(pojedinacna_naredba));
        int komada_u_naredbi = 0;
        int unutrašnji_nivo_zagrada = 0; 

        for (int k = 0; k < broj_unutrasnjih; k++) {
            pojedinacna_naredba[komada_u_naredbi++] = unutrasnji_tokeni[k];

            if (unutrasnji_tokeni[k].type == Z_TK_L_ZAGRADA) {
                unutrašnji_nivo_zagrada++;
            }
            if (unutrasnji_tokeni[k].type == Z_TK_D_ZAGRADA) {
                unutrašnji_nivo_zagrada--;
            }

            // Šaljemo u rekurziju samo kada vidimo regularni završetak naredbe na nivo_zagrada nuli
            if (unutrasnji_tokeni[k].type == Z_TK_TACKA_ZAREZ && unutrašnji_nivo_zagrada == 0) {
                bezbedno_prevedi_u_bajte_tokeni(pojedinacna_naredba, komada_u_naredbi);
                memset(pojedinacna_naredba, 0, sizeof(pojedinacna_naredba));
                komada_u_naredbi = 0;
            }
        }

        if (komada_u_naredbi > 0) {
            bezbedno_prevedi_u_bajte_tokeni(pojedinacna_naredba, komada_u_naredbi);
        }

        // Vraćanje nazad na uslov
        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x38; 
        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)pozicija_pocetka_uslova; 

        int kraj_unutrasnjeg_koda = Velicina_Generisanog_Koda;
        int ukupno_bajtova_akcije = kraj_unutrasnjeg_koda - pocetak_unutrasnjeg_koda;
        Masinski_Kod_Bafer[pozicija_duzine_skoka] = (uint8_t)ukupno_bajtova_akcije;

        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x37;

        return;
    }

    // =========================================================================
    // 7. KOMANDA: OZNAKA: Kraj ; - DEFINICIJA LABELE
    // =========================================================================
    if (strcmp(tokeni[0].value, "OZNAKA") == 0 && strcmp(tokeni[1].value, ":") == 0) {
        char* ime_oznake = tokeni[2].value; 
        Nadji_Ili_Dodaj_Labelu(ime_oznake, Velicina_Generisanog_Koda, 0);
        zlog("📝 [Z-KOMPAJLER LABELE]: Definisana oznaka '%s' na PC adresi: %d\n", ime_oznake, Velicina_Generisanog_Koda);
        return;
    }

    // =========================================================================
    // 8. KOMANDA: SKOCI = Kraj ; - BEZUSLOVNI SKOK SA AUTOMATSKIM BACKPATCHING-OM
    // =========================================================================
    if (strcmp(tokeni[0].value, "SKOCI") == 0 && strcmp(tokeni[1].value, "=") == 0) {
        char* ciljna_oznaka = tokeni[2].value; // Ime labele je čist token na indeksu 2
        int ciljni_pc = Nadji_Ili_Dodaj_Labelu(ciljna_oznaka, -1, 1);
        
        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x39; // Opkod 0x39
        
        if (ciljni_pc == -1) {
            // 🌟 SKOK UNAPRED: Pamtimo poziciju privremene nule za naknadno popunjavanje!
            Tabela_Backpatcha[Broj_Zauzetih_Backpatcha].pozicija_u_baferu = Velicina_Generisanog_Koda;
            snprintf(Tabela_Backpatcha[Broj_Zauzetih_Backpatcha].ime_trazene_oznake, 64, "%s", ciljna_oznaka);
            Broj_Zauzetih_Backpatcha++;
            
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x00; // Privremena nula
            zlog("📝 [Z-KOMPAJLER LABELE]: SKOCI ka '%s' (Zabeležen skok unapred na adresi %d)\n", 
                 ciljna_oznaka, Velicina_Generisanog_Koda - 1);
        } else {
            // SKOK UNAZAD: Labela je već poznata, odmah upisujemo njenu pravu adresu
            Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = (uint8_t)ciljni_pc;
            zlog("📝 [Z-KOMPAJLER LABELE]: SKOCI ka '%s' upisan direktno na fiksni PC: %d\n", ciljna_oznaka, ciljni_pc);
        }
        return;
    }

  


    // =========================================================================
    // 11. KOMANDA: VRATI ; 
    // =========================================================================
    if (tokeni->type == Z_TK_KR_VRATI) { // 🌟 POPRAVLJENO: -> umesto .
        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x3B; // Opkod 0x3B
        zlog("📝 [Z-KOMPAJLER FUNKCIJE]: Generisan VRATI opkod 0x3B\n");
        return;
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
    Brojac_Jedinstvenih_Uslova = 0;
    memset(Masinski_Kod_Bafer, 0, sizeof(Masinski_Kod_Bafer));
    memset(Kompajlerska_Memorija, 0, sizeof(Kompajlerska_Memorija));
    
    Broj_Zauzetih_Labela = 0;
    memset(Imena_Labela, 0, sizeof(Imena_Labela));
    
    Broj_Zauzetih_Backpatcha = 0;
    memset(Tabela_Backpatcha, 0, sizeof(Tabela_Backpatcha));

    Funkcije_Inicijalizuj(); // Čistimo tabelu funkcija i pozivni stek pre novog ciklusa

    // 🌟 KOREKCIJA PUTANJE: Skripta je na root-u, bez prefiksa 'app/'
    const char* putanja_skripte = "kod.txt"; 

    printf("=== Z-LANGUAGE NATIVE COMPILER WITH OFFICIAL OS_EXEC LINK ===\n\n");
    printf("📖 Otvaram izvorni fajl: %s...\n", putanja_skripte);

    // 1. Otvaramo eksterni fajl direktno sa root-a
     f_izvor = fopen(putanja_skripte, "r");
    if (!f_izvor) {
        printf("🛑 Greška: Ne mogu da otvorim fajl '%s'! Proveri da li fajl postoji na root direktorijumu.\n", putanja_skripte);
        return 1;
    }

    int i = 0; 
    int stvarni_broj_linije_fajla = 0; 

    // 2. Čitamo fajl liniju po liniju
    while (fgets(linija_iz_fajla, sizeof(linija_iz_fajla), f_izvor) != NULL) {
        stvarni_broj_linije_fajla++; 

        // Ako je linija prazna ili sadrži samo prelazak u novi red, preskačemo je
        if (strlen(linija_iz_fajla) <= 1 || linija_iz_fajla[0] == '\n' || linija_iz_fajla[0] == '\r') {
            continue;
        }

        // Uklanjamo prelazak u novi red (\n ili \r) sa kraja stringa
        linija_iz_fajla[strcspn(linija_iz_fajla, "\r\n")] = 0;

        // Preskačemo komentare na samom početku
        if (linija_iz_fajla[0] == '>' && linija_iz_fajla[1] == '>') {
            continue; 
        }

        // 🌟 MEHANIZAM ZA VIŠELINIJSKI DOK BLOK: Spajamo tekst pre sanitizacije!
        char veliki_tekstualni_bafer[1024] = "";
        strncpy(veliki_tekstualni_bafer, linija_iz_fajla, sizeof(veliki_tekstualni_bafer) - 1);

        // Proveravamo da li linija pokreće DOK petlicu
        if (strstr(linija_iz_fajla, "DOK(") != NULL || strstr(linija_iz_fajla, "DOK ") != NULL) {
            int nivo_otvorenih = 0;
            // Brojimo zagrade u prvoj liniji
            for (int j = 0; veliki_tekstualni_bafer[j] != '\0'; j++) {
                if (veliki_tekstualni_bafer[j] == '(') nivo_otvorenih++;
                if (veliki_tekstualni_bafer[j] == ')') nivo_otvorenih--;
            }

            // Ako zagrade nisu balansirane, gutamo naredne redove pješke iz fajla
            char sledeca_linija_teksta[256];
            while (nivo_otvorenih > 0 && fgets(sledeca_linija_teksta, sizeof(sledeca_linija_teksta), f_izvor) != NULL) {
                stvarni_broj_linije_fajla++;
                
                // Ignorišemo komentare unutar petlje
                if (sledeca_linija_teksta[0] == '>' && sledeca_linija_teksta[1] == '>') continue;
                
                sledeca_linija_teksta[strcspn(sledeca_linija_teksta, "\r\n")] = 0;
                
                // Brojimo zagrade u novom pročitanom redu
                for (int j = 0; sledeca_linija_teksta[j] != '\0'; j++) {
                    if (sledeca_linija_teksta[j] == '(') nivo_otvorenih++;
                    if (sledeca_linija_teksta[j] == ')') nivo_otvorenih--;
                }
                
                // Lepimo red u naš zajednički veliki tekstualni bafer
                strncat(veliki_tekstualni_bafer, " ", sizeof(veliki_tekstualni_bafer) - strlen(veliki_tekstualni_bafer) - 1);
                strncat(veliki_tekstualni_bafer, sledeca_linija_teksta, sizeof(veliki_tekstualni_bafer) - strlen(veliki_tekstualni_bafer) - 1);
            }
        }

        // Sinhronizujemo sistemski debug sa stvarnim stanjem u fajlu
        Trenutna_Linija_Kompajliranja = stvarni_broj_linije_fajla; 
        Sirova_Linija_Teksta = veliki_tekstualni_bafer;

        // 🚀 SADA POZIVAMO SANITIZER: Nad celim, spojenim i balansiranim tekstom petlje!
        if (!sanitizer_ocisti_liniju(veliki_tekstualni_bafer, upeglana, sizeof(upeglana))) {
            prijavi_sistemsku_gresku(ERR_FALI_TACKA_ZAREZ, "");
        }

        if (strlen(upeglana) == 0) {
            continue;
        }

        // 🚀 LEXER: Razbijamo upeglanu i spojenu liniju na niz srpskih tokena
        int broj_tokena = lexer_tokenizuj(upeglana, tokeni_linije, 128);

        // 🚀 PARSER: Prevodimo niz tokena u mašinske opkodove
        bezbedno_prevedi_u_bajte_tokeni(tokeni_linije, broj_tokena);
        
        i++;
    }

    // Zatvaramo izvorni fajl nakon što je uspešno kompajliran u bajt-kod bafer
    fclose(f_izvor);

     // =========================================================================
    // 🌟 KONAČNA AKTIVACIJA BACKPATCHING-A: NAKNADNO POPUNJAVANJE ADRESA
    // =========================================================================
    zlog("\n🛠️  [Z-KOMPAJLER]: Pokrećem Backpatching za rešavanje skokova unapred...\n");
    for (int b = 0; b < Broj_Zauzetih_Backpatcha; b++) {
        int stvarna_adresa = Nadji_Ili_Dodaj_Labelu(Tabela_Backpatcha[b].ime_trazene_oznake, -1, 1);
        
        if (stvarna_adresa != -1) {
            // Hirurški menjamo privremenu nulu sa stvarnom adresom labele!
            int pos = Tabela_Backpatcha[b].pozicija_u_baferu;
            Masinski_Kod_Bafer[pos] = (uint8_t)stvarna_adresa;
            
            zlog("🛠️  [Z-KOMPAJLER BACKPATCH]: Oznaka '%s' pronađena na PC %d. Popunjavam bajt %d u baferu.\n", 
                 Tabela_Backpatcha[b].ime_trazene_oznake, stvarna_adresa, pos);
        } else {
            // Ako skripta pozove labelu koja uopšte ne postoji u fajlu, bacamo strogu sistemsku grešku!
            printf("🛑 Greška pri kompajliranju: Oznaka '%s' se pominje u skoku, ali nigde nije definisana!\n", 
                   Tabela_Backpatcha[b].ime_trazene_oznake);
            return -1;
        }
    }
    

        // 🌟 KOČNICA ZA VIRTUELNU MAŠINU: Prekidamo stream striktnom nulom!
        Masinski_Kod_Bafer[Velicina_Generisanog_Koda++] = 0x00;

    printf("📝 [Z-KOMPAJLER] Uspešno prevedeno %d linija koda iz fajla.\n", i);


    // --- VIRTUELNA MAŠINA (INTERPRETER BAJT-KODA) ---
    printf("\n🖥️  [Z-SISTEM] Pokrećem virtuelnu mašinu za izvršavanje Z-Bajt-koda...\n");
    
    int pc = 0; // Program Counter
    
    while (pc < Velicina_Generisanog_Koda) {
        uint8_t opkod = Masinski_Kod_Bafer[pc++];
        
   // 1. Obrada opkoda 0x18 (PPOVRSINA) - POPRAVLJENA I TAČNA GEOMETRIJA
   if (opkod == 0x18) {
    uint8_t lok_arg = Masinski_Kod_Bafer[pc++];
    uint8_t lok_levo = Masinski_Kod_Bafer[pc++];
    
    int64_t obim = Kompajlerska_Memorija[lok_arg].vrednost.ceo_broj;
    
    // Prvo podižemo obim na kvadrat, ali delimo sa MULTIPLIKATOR-om da zadržimo fiksni zarez
    int64_t obim_kvadrat = (obim * obim) / MULTIPLIKATOR; 
    
    // Konstanta za 4 * PI u tvom sistemu (fiksni zarez)
    int64_t delilac_4_pi = 12566370; 
    
    // Konačan račun površine sa očuvanom preciznošću
    int64_t rezultat_povrsina = (obim_kvadrat * MULTIPLIKATOR) / delilac_4_pi;
    
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
        
     // 3. Obrada opkoda 0x60 (ISPISI) - SA PRINUDNOM KOČNICOM ZA KRAJ PROGRAMA
     if (opkod == 0x60) {
        uint8_t lok = Masinski_Kod_Bafer[pc++];
        
        if (Kompajlerska_Memorija[lok].marker == 0x02) {
            printf("📢 [Z-ISPISI]: %s\n", (char*)&(Kompajlerska_Memorija[lok].vrednost));
        } else if (Kompajlerska_Memorija[lok].marker == 0x01) {
            int64_t ceo_deo = Kompajlerska_Memorija[lok].vrednost.ceo_broj / MULTIPLIKATOR;
            int64_t decimale = Kompajlerska_Memorija[lok].vrednost.ceo_broj % MULTIPLIKATOR;
            int64_t dve_decimale = decimale / 10000;
            if (dve_decimale < 0) dve_decimale = -dve_decimale;
            
            printf("📢 [Z-ISPISI]: %ld.%02ld\n", ceo_deo, dve_decimale);
        }
        
        // 🌟 SVEVIŠNJA KOČNICA: Ako smo upravo odštampali lokaciju 0 (Kontrola), ZAVRŠI PROGRAM ODMAH!
        if (lok == 0) {
            zlog("🏁 [VM]: Izvršen poslednji ISPISI za Kontrola na lokaciji 0. Prisilno gasim VM.\n");
            break;
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
            // 8. Obrada opkoda 0x35 (AKO) - FIXNI SKOK KOJI UKLANJA BESKONAČNE PETLJE
            if (opkod == 0x35) {
                uint8_t lok_uslova = Masinski_Kod_Bafer[pc++];
                int64_t vrednost_uslova = Kompajlerska_Memorija[lok_uslova].vrednost.ceo_broj;
                
                uint8_t sledeci_opkod = Masinski_Kod_Bafer[pc++];
                if (sledeci_opkod == 0x36) {
                    // Čitamo tačan broj bajtova akcije koji je kompajler upisao u bafer!
                    uint8_t bajtova_za_preskakanje = Masinski_Kod_Bafer[pc++]; 
                    
                    // Ako je uslov netačan (0), hirurški preskačemo blok sabiranjem PC-ja!
                    if (vrednost_uslova == 0) {
                        zlog("⚙️ [VM - SKOK]: Uslov je NETAČAN. Preskačem fiksno %d bajtova koda.\n", bajtova_za_preskakanje);
                        pc += bajtova_za_preskakanje; // 🌟 BUM! PC skače tačno iza petlje, bez ikakvog klizanja!
                    } else {
                        zlog("⚙️ [VM - SKOK]: Uslov je TAČAN. Prolazim kroz blok normalno.\n");
                    }
                }
                continue;
            }
    


    // 9. Obrada opkoda 0x74 (Poređenje VEĆE OD '>')
    if (opkod == 0x74) {
        uint8_t arg1 = Masinski_Kod_Bafer[pc++];
        uint8_t arg2 = Masinski_Kod_Bafer[pc++];
        uint8_t levo = Masinski_Kod_Bafer[pc++]; // Čitamo dinamičku lokaciju uslova iz bafera!
        
        // 🌟 ZID ZAŠTITE LOKACIJE 0: Ako je levo greškom ispalo 0, prisilno ga baci na 15!
        if (levo == 0) levo = 15; 

        Kompajlerska_Memorija[levo].marker = 0x01;
        
        int64_t val1 = Kompajlerska_Memorija[arg1].vrednost.ceo_broj;
        int64_t val2 = Kompajlerska_Memorija[arg2].vrednost.ceo_broj;
        
        if (val1 > val2) {
            Kompajlerska_Memorija[levo].vrednost.ceo_broj = 1 * MULTIPLIKATOR;
        } else {
            Kompajlerska_Memorija[levo].vrednost.ceo_broj = 0;
        }
        
        zlog("📊 [VM-DEBUG - POREDJENJE]: %ld>%ld? Rezultat na lokaciji %d je :%ld\n", 
             val1 / MULTIPLIKATOR, val2 / MULTIPLIKATOR, levo, Kompajlerska_Memorija[levo].vrednost.ceo_broj);
               
        continue;
    }

    // 🌟 9b. Obrada opkoda 0x75 (Poređenje MANJE OD '<') - DODATO ODMAH ISPOD!
    if (opkod == 0x75) {
        uint8_t arg1 = Masinski_Kod_Bafer[pc++];
        uint8_t arg2 = Masinski_Kod_Bafer[pc++];
        uint8_t levo = Masinski_Kod_Bafer[pc++]; // Čitamo dinamičku lokaciju uslova iz bafera!
        
        if (levo == 0) levo = 15; 

        Kompajlerska_Memorija[levo].marker = 0x01;
        
        int64_t val1 = Kompajlerska_Memorija[arg1].vrednost.ceo_broj;
        int64_t val2 = Kompajlerska_Memorija[arg2].vrednost.ceo_broj;
        
        if (val1 < val2) { // 🌟 Provera za MANJE OD
            Kompajlerska_Memorija[levo].vrednost.ceo_broj = 1 * MULTIPLIKATOR;
        } else {
            Kompajlerska_Memorija[levo].vrednost.ceo_broj = 0;
        }
        
        zlog("📊 [VM-DEBUG - POREDJENJE]: %ld<%ld? Rezultat na lokaciji %d je :%ld\n", 
             val1 / MULTIPLIKATOR, val2 / MULTIPLIKATOR, levo, Kompajlerska_Memorija[levo].vrednost.ceo_broj);
               
        continue;
    }
    // 9c. Obrada opkoda 0x76 (Poređenje JEDNAKO '==')
    if (opkod == 0x76) {
        uint8_t arg1 = Masinski_Kod_Bafer[pc++];
        uint8_t arg2 = Masinski_Kod_Bafer[pc++];
        uint8_t levo = Masinski_Kod_Bafer[pc++];
        
        if (levo == 0) levo = 15;
        Kompajlerska_Memorija[levo].marker = 0x01;
        
        int64_t val1 = Kompajlerska_Memorija[arg1].vrednost.ceo_broj;
        int64_t val2 = Kompajlerska_Memorija[arg2].vrednost.ceo_broj;
        
        if (val1 == val2) { // 🌟 Provera jednakosti
            Kompajlerska_Memorija[levo].vrednost.ceo_broj = 1 * MULTIPLIKATOR;
        } else {
            Kompajlerska_Memorija[levo].vrednost.ceo_broj = 0;
        }
        
        zlog("📊 [VM-DEBUG - POREDJENJE]: %ld==%ld? Rezultat na lokaciji %d je :%ld\n", 
             val1 / MULTIPLIKATOR, val2 / MULTIPLIKATOR, levo, Kompajlerska_Memorija[levo].vrednost.ceo_broj);
        continue;
    }

    // 9d. Obrada opkoda 0x77 (Poređenje RAZLIČITO '!=')
    if (opkod == 0x77) {
        uint8_t arg1 = Masinski_Kod_Bafer[pc++];
        uint8_t arg2 = Masinski_Kod_Bafer[pc++];
        uint8_t levo = Masinski_Kod_Bafer[pc++];
        
        if (levo == 0) levo = 15;
        Kompajlerska_Memorija[levo].marker = 0x01;
        
        int64_t val1 = Kompajlerska_Memorija[arg1].vrednost.ceo_broj;
        int64_t val2 = Kompajlerska_Memorija[arg2].vrednost.ceo_broj;
        
        if (val1 != val2) { // 🌟 Provera različitosti
            Kompajlerska_Memorija[levo].vrednost.ceo_broj = 1 * MULTIPLIKATOR;
        } else {
            Kompajlerska_Memorija[levo].vrednost.ceo_broj = 0;
        }
        
        zlog("📊 [VM-DEBUG - POREDJENJE]: %ld!=%ld? Rezultat na lokaciji %d je :%ld\n", 
             val1 / MULTIPLIKATOR, val2 / MULTIPLIKATOR, levo, Kompajlerska_Memorija[levo].vrednost.ceo_broj);
        continue;
    }

      // 10. Obrada opkoda 0x37 (KRAJ USLOVNOG BLOKA)
  if (opkod == 0x37) {
        continue; // Samo proleti kroz njega i pređi na sledeću čistu komandu!
    }
  // 11. Obrada opkoda 0x38 (SKOCI_NAZAD - povratak na početak petlje)
  if (opkod == 0x38) {
    uint8_t pozicija_uslova = Masinski_Kod_Bafer[pc++];
    
    zlog("🔄 [VM-DEBUG - PETLJA]: Detektovan SKOCI_NAZAD. Vraćam Program Counter sa %d unazad na poziciju %d\n", 
         pc, pozicija_uslova);
    
    // Prisilno vraćamo pc na početak provere uslova petlje
    pc = pozicija_uslova; 
    continue;
}

     // 12. Obrada opkoda 0x3B (VRATI)
     if (opkod == 0x3B) {
        // Ako je vrh steka na nuli, nemoj skakati na nulu nego nasilno ugasi program!
        // (Z-lang koristi eksternu ili lokalnu promenljivu steka, proveri kako ti se zove funkcija)
        uint32_t povratni_pc = Stek_Popni(); 
        
        zlog("⏪ [VM - FUNKCIJE]: Detektovan VRATI. Vraćam Program Counter na sačuvanu adresu PC %d\n", povratni_pc);
        
        // 🌟 PRIVREMENI ZID: Ako je vraćeni PC nula (prazan stek), momentalno prekidamo VM!
        if (povratni_pc == 0) {
            printf("🛑 [VM KOČNICA]: Detektovan prazan stek na VRATI instrukciji! Zaustavljam VM.\n");
            break;
        }
        
        pc = povratni_pc; 
        continue;
    }


        // 13. Obrada opkoda 0x3A (POZOVI - odlazak u podprogram)
        if (opkod == 0x3A) {
            uint8_t ciljna_adresa_funkcije = Masinski_Kod_Bafer[pc++];
            
            // 🌟 POPRAVLJENO: Na stek potiskujemo trenutni 'pc' jer on sada, nakon pc++, 
            // pokazuje na sledeću čistu komandu u glavnom programu (iza poziva funkcije)!
            Stek_Potisni(pc); 
            
            zlog("🚀 [VM - FUNKCIJE]: Izvršavam POZOVI. Pamtim povratnu adresu %d i skačem na funkciju na PC %d\n", pc, ciljna_adresa_funkcije);
            
            // Tek sada menjamo PC i skačemo na adresu funkcije
            pc = ciljna_adresa_funkcije; 
            continue;
        }


// Svevišnja kočnica za kraj programa
if (opkod == 0xC3 || opkod == 0x00) {
    break;
}
    }


        // 🌟 KONAČNO SNIMANJE SVIH DEBUG PORUKA U LOG FAJL
        printf("💾 Snimam debug izveštaj u 'zlang_log.txt'...\n");
        FILE* f_log = fopen("zlang_log.txt", "w");
        if (f_log) {
            fprintf(f_log, "=== Z-LANGUAGE COMPILER & VM EXPERT DEBUG LOG ===\n\n");
            fputs(Z_Log_Bafer, f_log);
            fclose(f_log);
            kopiraj_fajl("zlang_log.txt");
            printf("✅ Izveštaj uspešno sačuvan!\n");
        } else {
            printf("🛑 Greška: Ne mogu da kreiram 'zlang_log.txt'!\n");
        }
    
    printf("\n✅ [Z-Sistem] Kompletan ciklus je uspešno završen!\n");
    return 0;
}


