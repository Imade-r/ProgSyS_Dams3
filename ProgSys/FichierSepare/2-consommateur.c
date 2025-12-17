#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include "2-common.h"

int main() {
    // =================================================================
    // 1. CONNEXION A LA MÉMOIRE (L'entrepôt)
    // =================================================================
    
    // Notez l'absence de O_CREAT : On veut ouvrir l'existant.
    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd == -1) { 
        perror("Erreur : Lancez le producteur d'abord !"); 
        exit(1); 
    }

    // On récupère le pointeur vers la mémoire partagée
    MemoirePartagee* partagee = mmap(NULL, sizeof(MemoirePartagee), 
                                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    // !!! IMPORTANT !!!
    // On NE remet PAS i et j à 0 ici.

    // =================================================================
    // 2. CONNEXION AUX SÉMAPHORES
    // =================================================================
    // Le '0' final signifie : "Ne change pas la valeur, prends celle en cours"
    sem_t *places_libres = sem_open(SEM_PLACES_LIBRES, 0); 
    sem_t *items_existants = sem_open(SEM_ITEMS_EXISTANTS, 0);
    sem_t *mutex = sem_open(SEM_MUTEX, 0); 

    printf("--- Consommateur V2 (Strings) Démarré ---\n");

    // =================================================================
    // 3. BOUCLE DE CONSOMMATION
    // =================================================================
    for (int k = 0; k < NB_ITEMS; k++) {
        Donnee item;

        // --- ATTENTE ---
        // 1. Y a-t-il quelque chose à lire ?
        sem_wait(items_existants); 

        // 2. Puis-je toucher à la mémoire ?
        sem_wait(mutex);           

        // --- SECTION CRITIQUE ---
        // Lecture de la structure Donnee (chaine de caractères)
        item = partagee->tab[partagee->j];
        printf("<- Consommateur : lu '%s' (index %d)\n", item.texte, partagee->j);

        // Avancée de l'index de lecture (circulaire)
        partagee->j = (partagee->j + 1) % N;

        // --- LIBÉRATION ---
        // 1. On rend la clé
        sem_post(mutex);           
        
        // 2. On signale qu'une place s'est LIBÉRÉE
        sem_post(places_libres);   

        sleep(1);
    }

    printf("--- Fin consommation ---\n");

    // =================================================================
    // 4. FERMETURE LOCALE
    // =================================================================
    // On ferme juste notre porte. On ne détruit pas le bâtiment.
    munmap(partagee, sizeof(MemoirePartagee));
    sem_close(places_libres);
    sem_close(items_existants);
    sem_close(mutex);
    
    return 0;
}