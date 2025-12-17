#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include "1-common.h"

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
    // On demande au système : "Prends l'objet 'fd' et donne-moi un pointeur
    // vers lui dans ma mémoire vive locale."
    // Maintenant, quand on écrit dans 'partagee', on écrit dans la mémoire partagée.
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
    
    // SEM_PLACES_LIBRES : Compte combien de places sont vides.
    // Initialisé à N (10) car au début, tout le tableau est vide.
    sem_t *places_libres = sem_open(SEM_PLACES_LIBRES, O_CREAT, 0666, N);
    
    // SEM_ITEMS_EXISTANTS : Compte combien d'items sont prêts à être lus.
    // Initialisé à 0 car au début, il n'y a rien.
    sem_t *items_existants = sem_open(SEM_ITEMS_EXISTANTS, O_CREAT, 0666, 0);
    
    // SEM_MUTEX : Sert de verrou pour protéger les variables partagées (i, j, tab).
    // Initialisé à 1 (Clé disponible) pour laisser entrer le premier venu.
    // ATTENTION : Si mis à 0, personne ne rentre jamais (interblocage).
    sem_t *mutex = sem_open(SEM_MUTEX, O_CREAT, 0666, 1); 

    printf("--- Producteur Démarré ---\n");

    // =================================================================
    // 4. BOUCLE DE PRODUCTION
    // =================================================================
    for(int k = 0; k < NB_ITEMS; k++) {
        int item = k * 10; // On fabrique une donnée (0, 10, 20...)

        // --- ATTENTE (Protocole d'entrée) ---
        // 1. Y a-t-il de la place ?
        // Si places_libres > 0, on décrémente et on passe.
        // Si places_libres == 0, le processus DORT ici jusqu'à ce qu'une place se libère.
        sem_wait(places_libres);  

        // 2. Puis-je toucher à la mémoire ? (Exclusion Mutuelle)
        // On prend la clé unique. Si le consommateur l'a, on attend.
        sem_wait(mutex);          

        // --- SECTION CRITIQUE (On est seul ici) ---
        // On écrit la donnée dans le tableau
        partagee->tab[partagee->i] = item;
        printf("-> Producteur : ecrit %d (index %d)\n", item, partagee->i);
        
        // Gestion circulaire : Si on arrive à 9, le prochain est 0 (car 10%10 = 0)
        partagee->i = (partagee->i + 1) % N; 

        // --- LIBÉRATION (Protocole de sortie) ---
        // 1. On rend la clé pour que l'autre puisse travailler
        sem_post(mutex);          

        // 2. On signale qu'il y a un NOUVEL item disponible
        // (Incrémente items_existants, réveille le consommateur s'il dormait)
        sem_post(items_existants); 

        sleep(1); // Simule un temps de travail
    }

    printf("--- Fin production. Nettoyage... ---\n");

    // =================================================================
    // 5. NETTOYAGE FINAL (Ménage système)
    // =================================================================
    
    // 1. On détache la mémoire de notre programme
    munmap(partagee, sizeof(MemoirePartagee));
    
    // 2. On ferme l'accès aux sémaphores (comme fermer un fichier)
    sem_close(places_libres);
    sem_close(items_existants);
    sem_close(mutex);

    // 3. SUPPRESSION DÉFINITIVE (unlink)
    // C'est très important : on demande au système d'effacer les fichiers
    // /dev/shm/mon_shm et les sémaphores. Si on ne le fait pas, ils restent
    // en RAM jusqu'au redémarrage du PC !
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_PLACES_LIBRES);
    sem_unlink(SEM_ITEMS_EXISTANTS);
    sem_unlink(SEM_MUTEX);

    return 0;
}
