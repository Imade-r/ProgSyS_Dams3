#ifndef COMMON_H
#define COMMON_H

// --- Paramètres du tampon ---
#define N 10            // Le tableau ne peut contenir que 10 messages max
#define NB_ITEMS 20     // On va produire et consommer 20 messages au total
#define TAILLE_MSG 64   // Taille maximale d'un message (char)

// --- Noms des ressources systèmes (VERSION 2) ---
// J'ai ajouté "_v2" pour ne pas entrer en conflit avec votre version 1
// si les deux tournent ou si la V1 n'a pas été nettoyée correctement.
#define SHM_NAME "/mon_shm_v2"             
#define SEM_PLACES_LIBRES "/sem_places_libres_v2" 
#define SEM_ITEMS_EXISTANTS "/sem_items_existants_v2"
#define SEM_MUTEX "/sem_mutex_v2"

// --- Structures de données ---

// 1. Structure enveloppe pour le message (Spécifique Version 2)
typedef struct {
    char texte[TAILLE_MSG];
} Donnee;

// 2. Le "moule" de la mémoire partagée
typedef struct {
    Donnee tab[N]; // Le tableau contient maintenant des structures Donnee (strings)
    int i;         // Index d'écriture
    int j;         // Index de lecture
} MemoirePartagee;

#endif