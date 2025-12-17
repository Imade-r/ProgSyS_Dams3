#ifndef COMMON_H
#define COMMON_H

// --- Paramètres du tampon ---
#define N 10            // Le tableau ne peut contenir que 10 entiers max
#define NB_ITEMS 20     // On va produire et consommer 20 nombres au total
                        // (Donc on fera 2 fois le tour du tableau)

// --- Noms des ressources systèmes ---
// Ces chaînes servent d'identifiants uniques (comme des noms de fichiers)
// pour que les deux processus retrouvent la MÊME mémoire et les MÊMES sémaphores.
#define SHM_NAME "/mon_shm"             // Nom de la mémoire partagée
#define SEM_PLACES_LIBRES "/sem_places_libres" 
#define SEM_ITEMS_EXISTANTS "/sem_items_existants"
#define SEM_MUTEX "/sem_mutex"

// --- Structure de données ---
// C'est le "moule" qu'on va appliquer sur la zone de mémoire brute.
typedef struct {
    int tab[N]; // Le tampon circulaire (là où on stocke les données)
    int i;      // Index d'écriture : Où le producteur doit écrire le prochain item
    int j;      // Index de lecture : Où le consommateur doit lire le prochain item
} MemoirePartagee;

#endif