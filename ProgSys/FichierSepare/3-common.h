#ifndef COMMON_H
#define COMMON_H
// ^-- HEADER GUARD : Empêche l'inclusion multiple de ce fichier

// --- PARAMÈTRES DU TAMPON ---
#define N 10            // Nombre de places dans le tampon circulaire
#define TAILLE_MSG 64   // Taille fixe en octets d'un message

// --- IDENTIFIANTS DES RESSOURCES PARTAGÉES (IPC POSIX) ---
// Ces chaînes de caractères servent de clés uniques pour le noyau (Kernel).
// Elles permettent à des processus indépendants de se connecter aux mêmes ressources.
// Sous Linux, elles sont souvent visibles dans /dev/shm/.

#define SHM_NAME "/mon_shm_v3"             // Nom de la zone de mémoire partagée
#define SEM_PLACES_LIBRES "/sem_places_libres_v3" // Sémaphore : places vides restantes
#define SEM_ITEMS_EXISTANTS "/sem_items_existants_v3" // Sémaphore : items prêts à lire
#define SEM_MUTEX "/sem_mutex_v3"          // Sémaphore : exclusion mutuelle (verrou)

// --- IDENTIFIANTS DES TUBES NOMMÉS (FIFOs) ---
// Ce sont des fichiers spéciaux créés dans le système de fichiers (ici /tmp).
// Ils servent de "boîtes aux lettres" pour recevoir les ordres du programme 'communicant'.
#define FIFO_PROD "/tmp/fifo_prod_v3"      // Boîte aux lettres du Producteur
#define FIFO_CONSO "/tmp/fifo_conso_v3"    // Boîte aux lettres du Consommateur

// --- STRUCTURE DE DONNÉES (Payload) ---
// Structure simple encapsulant le message.
// Permet la copie par affectation (=) au lieu de strcpy().
typedef struct {
    char texte[TAILLE_MSG];
} Donnee;

// --- STRUCTURE DE LA MÉMOIRE PARTAGÉE (Layout) ---
// Cette structure définit comment les octets de la mémoire partagée sont organisés.
// Elle sera projetée (mappée) sur la zone mémoire brute.
typedef struct {
    Donnee tab[N]; // Le tampon circulaire de données
    int i;         // Index d'écriture (Producteur)
    int j;         // Index de lecture (Consommateur)
} MemoirePartagee;

#endif