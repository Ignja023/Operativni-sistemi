#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#define BROJ_BLOKOVA 5
#define DUZINA_NAZIVA 24
#define DUZINA_TEKSTA 80

typedef struct Zahtev {
    int broj;
    int vrsta; /* 1 citanje, 2 upis, 3 brisanje */
    char naziv[DUZINA_NAZIVA];
    char tekst[DUZINA_TEKSTA];
    struct Zahtev *sledeci;
} Zahtev;

typedef struct Blok {
    char naziv[DUZINA_NAZIVA]; /* Prazan naziv oznacava slobodan blok. */
    char tekst[DUZINA_TEKSTA];
} Blok;

static Blok disk[BROJ_BLOKOVA];
static Zahtev *glava = NULL;
static Zahtev *rep = NULL;
static int sledeci_broj = 1;
static int samo_citanje = 0;
static int kvar_uredjaja = 0;

/* 1 uspesno, 0 kraj ulaza, -1 predugacak unos. */
static int ucitaj_red(FILE *tok, char *tekst, int kapacitet) {
    int znak;
    if (!fgets(tekst, kapacitet, tok)) return 0;
    if (!strchr(tekst, '\n') && !feof(tok)) {
        znak = fgetc(tok);
        if (znak != '\n' && znak != EOF) {
            while ((znak = fgetc(tok)) != '\n' && znak != EOF) { }
            return -1;
        }
    }
    tekst[strcspn(tekst, "\r\n")] = '\0';
    return 1;
}

static int unesi_broj(const char *poruka, int najmanji, int najveci) {
    char red[64], *kraj;
    long vrednost;
    int stanje;
    for (;;) {
        printf("%s", poruka);
        stanje = ucitaj_red(stdin, red, sizeof(red));
        if (stanje == 0) return -1;
        if (stanje < 0) { puts("Unos je predugacak."); continue; }
        errno = 0;
        vrednost = strtol(red, &kraj, 10);
        if (kraj == red) { puts("Unesite ceo broj."); continue; }
        while (isspace((unsigned char)*kraj)) kraj++;
        if (errno || *kraj || vrednost < najmanji || vrednost > najveci) {
            puts("Broj nije u dozvoljenom opsegu."); continue;
        }
        return (int)vrednost;
    }
}

static int ispravan_naziv(const char *naziv) {
    size_t mesto;
    if (!naziv[0] || strlen(naziv) >= DUZINA_NAZIVA) return 0;
    for (mesto = 0; naziv[mesto]; mesto++) {
        unsigned char znak = (unsigned char)naziv[mesto];
        if (!((znak >= 'A' && znak <= 'Z') || (znak >= 'a' && znak <= 'z') ||
              (znak >= '0' && znak <= '9') || znak == '_' || znak == '.' || znak == '-'))
            return 0;
    }
    return 1;
}

static int unesi_tekst(const char *poruka, char *tekst, int kapacitet, int naziv) {
    int stanje;
    for (;;) {
        printf("%s", poruka);
        stanje = ucitaj_red(stdin, tekst, kapacitet);
        if (!stanje) return 0;
        if (stanje < 0) { puts("Unos je predugacak. Pokusajte ponovo."); continue; }
        if (!tekst[0] || strchr(tekst, '|') || (naziv && !ispravan_naziv(tekst))) {
            puts(naziv ? "Naziv: slova A-Z/a-z, cifre, _, - i tacka; do 23 znaka."
                       : "Tekst ne sme biti prazan niti sadrzati znak |.");
            continue;
        }
        return 1;
    }
}

static const char *naziv_operacije(int vrsta) {
    return vrsta == 1 ? "CITANJE" : vrsta == 2 ? "UPIS" : "BRISANJE";
}

/* Red FIFO realizovan jednostruko spregnutom listom. */
static void dodaj_zahtev(void) {
    Zahtev *novi;
    int vrsta = unesi_broj("Vrsta (1 citanje, 2 upis, 3 brisanje): ", 1, 3);
    if (vrsta < 0) return;
    novi = calloc(1, sizeof(*novi));
    if (!novi) { puts("Nema dovoljno memorije."); return; }
    novi->vrsta = vrsta;
    if (!unesi_tekst("Naziv datoteke: ", novi->naziv, sizeof(novi->naziv), 1) ||
        (vrsta == 2 && !unesi_tekst("Sadrzaj (do 79 znakova): ", novi->tekst, sizeof(novi->tekst), 0))) {
        free(novi); return;
    }
    novi->broj = sledeci_broj++;
    if (rep) rep->sledeci = novi;
    else glava = novi;
    rep = novi;
    printf("Zahtev %d dodat u red.\n", novi->broj);
}

static void prikazi_red(void) {
    const Zahtev *tekuci;
    puts("\n--- RED ZAHTEVA ---");
    if (!glava) puts("Red je prazan.");
    for (tekuci = glava; tekuci; tekuci = tekuci->sledeci)
        printf("%d | %-8s | %s\n", tekuci->broj, naziv_operacije(tekuci->vrsta), tekuci->naziv);
}

/* Sloj 4: model diska. Samo ovde se izvrsvaju operacije nad blokom. */
static int sloj_diska(const Zahtev *zahtev, int blok) {
    if (blok < 0 || blok >= BROJ_BLOKOVA) return 0;
    printf("  [4 Disk] Pristup bloku %d.\n", blok);
    switch (zahtev->vrsta) {
        case 1:
            printf("  [4 Disk] Procitano: %s\n", disk[blok].tekst);
            break;
        case 2:
            strcpy(disk[blok].naziv, zahtev->naziv);
            strcpy(disk[blok].tekst, zahtev->tekst);
            puts("  [4 Disk] Podaci su upisani.");
            break;
        case 3:
            memset(&disk[blok], 0, sizeof(disk[blok]));
            puts("  [4 Disk] Blok je oslobodjen.");
            break;
        default: return 0;
    }
    return 1;
}

/* Sloj 3: drajver proverava dostupnost uredjaja. */
static int sloj_drajvera(const Zahtev *zahtev, int blok) {
    int uspeh;
    puts("  [3 Drajver] Provera uredjaja.");
    if (kvar_uredjaja) {
        puts("  [3 Drajver] GRESKA: uredjaj nije dostupan.");
        return 0;
    }
    uspeh = sloj_diska(zahtev, blok);
    printf("  [3 Drajver] Povratak: %s.\n", uspeh ? "uspeh" : "greska");
    return uspeh;
}

/* Sloj 2: dozvole i prevod naziva datoteke u broj bloka. */
static int sloj_sistemskih_usluga(const Zahtev *zahtev) {
    int mesto, blok = -1, slobodan = -1, uspeh;
    puts("  [2 Sistemske usluge] Provera dozvole i pronalazenje datoteke.");
    if (samo_citanje && zahtev->vrsta != 1) {
        puts("  [2 Sistemske usluge] GRESKA: dozvoljeno je samo citanje.");
        return 0;
    }
    for (mesto = 0; mesto < BROJ_BLOKOVA; mesto++) {
        if (strcmp(disk[mesto].naziv, zahtev->naziv) == 0) blok = mesto;
        if (!disk[mesto].naziv[0] && slobodan == -1) slobodan = mesto;
    }
    if (blok == -1 && zahtev->vrsta == 2) blok = slobodan;
    if (blok == -1) {
        puts(zahtev->vrsta == 2 ? "  [2 Sistemske usluge] GRESKA: disk je pun."
                               : "  [2 Sistemske usluge] GRESKA: datoteka ne postoji.");
        return 0;
    }
    printf("  [2 Sistemske usluge] %s -> blok %d.\n", zahtev->naziv, blok);
    uspeh = sloj_drajvera(zahtev, blok);
    printf("  [2 Sistemske usluge] Povratak: %s.\n", uspeh ? "uspeh" : "greska");
    return uspeh;
}

/* Sloj 1: korisnik dobija ishod zahteva. */
static void sloj_korisnickog_interfejsa(const Zahtev *zahtev) {
    int uspeh;
    printf("\n[1 Interfejs] Zahtev %d: %s %s\n", zahtev->broj,
           naziv_operacije(zahtev->vrsta), zahtev->naziv);
    uspeh = sloj_sistemskih_usluga(zahtev);
    printf("[1 Interfejs] Ishod: %s.\n", uspeh ? "USPESNO" : "NEUSPESNO");
}

static void obradi_red(void) {
    Zahtev *tekuci;
    if (!glava) { puts("Nema zahteva za obradu."); return; }
    while (glava) {
        tekuci = glava;
        sloj_korisnickog_interfejsa(tekuci);
        glava = tekuci->sledeci;
        free(tekuci);
    }
    rep = NULL;
    puts("Red je obradjen i ispraznjen. Neuspesan zahtev se ponovo unosi.");
}

static void prikazi_disk(void) {
    int mesto, zauzeto = 0;
    puts("\n--- VIRTUELNI DISK ---");
    for (mesto = 0; mesto < BROJ_BLOKOVA; mesto++) {
        if (disk[mesto].naziv[0]) {
            printf("Blok %d | %s | %s\n", mesto, disk[mesto].naziv, disk[mesto].tekst);
            zauzeto++;
        } else printf("Blok %d | slobodan\n", mesto);
    }
    printf("Zauzeto: %d/%d blokova.\n", zauzeto, BROJ_BLOKOVA);
}

/* Administrativno cuvanje slike simulacije, odvojeno od simuliranih zahteva. */
static void sacuvaj_disk(void) {
    int mesto, uspeh = 1;
    FILE *datoteka = fopen("virtuelni_disk.txt", "w");
    if (!datoteka) { puts("GRESKA: ne mogu da otvorim virtuelni_disk.txt za upis."); return; }
    if (fprintf(datoteka, "SLOJEVITI_DISK_1\n") < 0) uspeh = 0;
    for (mesto = 0; mesto < BROJ_BLOKOVA; mesto++)
        if (disk[mesto].naziv[0] && fprintf(datoteka, "%d|%s|%s\n", mesto,
            disk[mesto].naziv, disk[mesto].tekst) < 0) uspeh = 0;
    if (fclose(datoteka) != 0) uspeh = 0;
    puts(uspeh ? "Disk je sacuvan u virtuelni_disk.txt." : "GRESKA pri cuvanju diska.");
}

static void ucitaj_disk(void) {
    Blok novi_disk[BROJ_BLOKOVA] = {0};
    char red[160], *naziv, *tekst, *kraj;
    long blok;
    int stanje, mesto, ispravno = 1;
    FILE *datoteka;
    if (glava) { puts("Prvo obradite red zahteva, pa ucitajte disk."); return; }
    datoteka = fopen("virtuelni_disk.txt", "r");
    if (!datoteka) { puts("GRESKA: virtuelni_disk.txt ne postoji ili nije dostupan."); return; }
    if (ucitaj_red(datoteka, red, sizeof(red)) != 1 || strcmp(red, "SLOJEVITI_DISK_1"))
        ispravno = 0;
    while (ispravno && (stanje = ucitaj_red(datoteka, red, sizeof(red))) != 0) {
        if (stanje < 0) { ispravno = 0; break; }
        naziv = strchr(red, '|');
        if (!naziv) { ispravno = 0; break; }
        *naziv++ = '\0';
        tekst = strchr(naziv, '|');
        if (!tekst) { ispravno = 0; break; }
        *tekst++ = '\0';
        errno = 0;
        blok = strtol(red, &kraj, 10);
        if (errno || kraj == red || *kraj || blok < 0 || blok >= BROJ_BLOKOVA ||
            !ispravan_naziv(naziv) || !tekst[0] || strlen(tekst) >= DUZINA_TEKSTA ||
            strchr(tekst, '|')) { ispravno = 0; break; }
        if (novi_disk[blok].naziv[0]) { ispravno = 0; break; }
        for (mesto = 0; mesto < BROJ_BLOKOVA; mesto++)
            if (!strcmp(novi_disk[mesto].naziv, naziv)) ispravno = 0;
        if (!ispravno) break;
        strcpy(novi_disk[blok].naziv, naziv);
        strcpy(novi_disk[blok].tekst, tekst);
    }
    if (ferror(datoteka)) ispravno = 0;
    fclose(datoteka);
    if (ispravno) {
        memcpy(disk, novi_disk, sizeof(disk));
        puts("Disk je ucitan. Prethodno stanje u memoriji je zamenjeno.");
    } else puts("GRESKA: neispravna datoteka. Prethodno stanje diska je sacuvano.");
}

int main(void) {
    int izbor;
    puts("SIMULACIJA SLOJEVITE ARHITEKTURE OPERATIVNOG SISTEMA");
    puts("Virtuelni disk: 5 blokova; jedna datoteka zauzima jedan blok.");
    do {
        puts("\n1 Dodaj zahtev     2 Prikazi red       3 Obradi red");
        puts("4 Prikazi disk     5 Sacuvaj disk      6 Ucitaj disk");
        puts("7 Rezim pristupa   8 Stanje uredjaja   0 Izlaz");
        printf("Rezim: %s | Uredjaj: %s\n", samo_citanje ? "samo citanje" : "citanje i upis",
               kvar_uredjaja ? "kvar" : "ispravan");
        izbor = unesi_broj("Izbor: ", 0, 8);
        switch (izbor) {
            case 1: dodaj_zahtev(); break;
            case 2: prikazi_red(); break;
            case 3: obradi_red(); break;
            case 4: prikazi_disk(); break;
            case 5: sacuvaj_disk(); break;
            case 6: ucitaj_disk(); break;
            case 7: samo_citanje = !samo_citanje; break;
            case 8: kvar_uredjaja = !kvar_uredjaja; break;
            default: break;
        }
    } while (izbor != 0 && izbor != -1);
    while (glava) {
        Zahtev *sledeci = glava->sledeci;
        free(glava);
        glava = sledeci;
    }
    puts("Kraj programa. Stanje diska se cuva samo opcijom 5.");
    return 0;
}
