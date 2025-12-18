#ifndef COMMON_H
#define COMMON_H

// --- PARAMÈTRES ---
#define N 10            
#define TAILLE_MSG 64   

// --- NOMS DES TUBES (FIFOs) ---
// IMPORTANT : Les chemins doivent correspondre à ceux ouverts dans Fork.c
// Si votre Fork.c utilise "/tmp/fifo_producteur", on garde ça ici.
#define FIFO_PROD "/tmp/fifo_producteur"
#define FIFO_CONSO "/tmp/fifo_consommateur"

// On garde aussi les alias courts au cas où d'autres fichiers les utilisent
#define FIFO_P FIFO_PROD
#define FIFO_C FIFO_CONSO

// --- NOMS RESSOURCES PARTAGÉES (V3) ---
#define SHM_NAME "/mon_shm_v3"
#define SEM_PLACES_LIBRES "/sem_places_libres_v3"
#define SEM_ITEMS_EXISTANTS "/sem_items_existants_v3"
#define SEM_MUTEX "/sem_mutex_v3"

// --- STRUCTURES ---
typedef struct {
    char texte[TAILLE_MSG];
} Donnee;

typedef struct {
    Donnee tab[N]; 
    int i;         
    int j;         
} MemoirePartagee;

#endif