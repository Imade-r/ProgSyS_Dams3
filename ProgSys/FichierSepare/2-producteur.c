#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h> // Nécessaire pour snprintf dans la V2
#include "2-common.h"

int main() {
    // =================================================================
    // 1. CRÉATION DE LA MÉMOIRE PARTAGÉE (L'entrepôt)
    // =================================================================
    
    // shm_open : Crée un objet de mémoire partagée POSIX.
    // O_CREAT : Crée l'objet s'il n'existe pas.
    // O_RDWR  : Ouvre en lecture et écriture.
    // 0666    : Permissions (rw-rw-rw-) pour que tout le monde puisse y accéder.
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1) { 
        perror("Erreur shm_open"); 
        exit(1); 
    }

    // ftruncate : Par défaut, l'objet créé a une taille de 0.
    // On force sa taille à correspondre exactement à notre structure.
    ftruncate(fd, sizeof(MemoirePartagee));
    
    // mmap : "Projection" en mémoire.
    MemoirePartagee* partagee = mmap(NULL, sizeof(MemoirePartagee), 
                                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    // On peut fermer le descripteur de fichier, le mapping est fait.
    close(fd);

    // =================================================================
    // 2. INITIALISATION (Fait uniquement par le créateur)
    // =================================================================
    partagee->i = 0; // On commence à écrire à la case 0
    partagee->j = 0; // Le consommateur commencera à lire à la case 0

    // =================================================================
    // 3. CRÉATION DES SÉMAPHORES (Les gardiens)
    // =================================================================
    
    // SEM_PLACES_LIBRES : Initialisé à N (10) car tout est vide.
    sem_t *places_libres = sem_open(SEM_PLACES_LIBRES, O_CREAT, 0666, N);
    
    // SEM_ITEMS_EXISTANTS : Initialisé à 0 car il n'y a rien.
    sem_t *items_existants = sem_open(SEM_ITEMS_EXISTANTS, O_CREAT, 0666, 0);
    
    // SEM_MUTEX : Initialisé à 1 (Clé disponible).
    sem_t *mutex = sem_open(SEM_MUTEX, O_CREAT, 0666, 1); 

    printf("--- Producteur V2 (Strings) Démarré ---\n");

    // =================================================================
    // 4. BOUCLE DE PRODUCTION
    // =================================================================
    for(int k = 0; k < NB_ITEMS; k++) {
        Donnee item;
        // Création du message complexe (Spécifique V2)
        snprintf(item.texte, TAILLE_MSG, "Message P%d", k);

        // --- ATTENTE (Protocole d'entrée) ---
        // 1. Y a-t-il de la place ?
        sem_wait(places_libres);  

        // 2. Puis-je toucher à la mémoire ? (Exclusion Mutuelle)
        sem_wait(mutex);          

        // --- SECTION CRITIQUE (On est seul ici) ---
        // On copie la structure entière dans le tableau partagé
        partagee->tab[partagee->i] = item;
        printf("-> Producteur : ecrit '%s' (index %d)\n", item.texte, partagee->i);
        
        // Gestion circulaire
        partagee->i = (partagee->i + 1) % N; 

        // --- LIBÉRATION (Protocole de sortie) ---
        // 1. On rend la clé
        sem_post(mutex);          

        // 2. On signale qu'il y a un NOUVEL item disponible
        sem_post(items_existants); 

        sleep(1); // Simule un temps de travail
    }

    printf("--- Fin production. Nettoyage... ---\n");

    // =================================================================
    // 5. NETTOYAGE FINAL (Ménage système)
    // =================================================================
    
    // 1. On détache la mémoire
    munmap(partagee, sizeof(MemoirePartagee));
    
    // 2. On ferme l'accès aux sémaphores
    sem_close(places_libres);
    sem_close(items_existants);
    sem_close(mutex);

    // 3. SUPPRESSION DÉFINITIVE (unlink)
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_PLACES_LIBRES);
    sem_unlink(SEM_ITEMS_EXISTANTS);
    sem_unlink(SEM_MUTEX);

    return 0;
}