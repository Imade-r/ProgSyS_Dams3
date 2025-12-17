
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
    // 1. CONNEXION A LA MÉMOIRE (L'entrepôt)
    // =================================================================
    
    // Notez l'absence de O_CREAT : On veut ouvrir l'existant.
    // Si le producteur n'est pas lancé, shm_open renverra -1.
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
    // On NE remet PAS i et j à 0 ici. On utilise les valeurs
    // qui sont actuellement dans la mémoire, modifiées par le producteur.

    // =================================================================
    // 2. CONNEXION AUX SÉMAPHORES
    // =================================================================
    // Le '0' final signifie : "Ne change pas la valeur, prends celle en cours"
    sem_t *places_libres = sem_open(SEM_PLACES_LIBRES, 0); 
    sem_t *items_existants = sem_open(SEM_ITEMS_EXISTANTS, 0);
    sem_t *mutex = sem_open(SEM_MUTEX, 0); 

    printf("--- Consommateur Démarré ---\n");

    // =================================================================
    // 3. BOUCLE DE CONSOMMATION
    // =================================================================
    for (int k = 0; k < NB_ITEMS; k++) {
        int item;

        // --- ATTENTE ---
        // 1. Y a-t-il quelque chose à lire ?
        // Si items_existants == 0 (tampon vide), le consommateur DORT ici.
        sem_wait(items_existants); 

        // 2. Puis-je toucher à la mémoire ?
        sem_wait(mutex);           

        // --- SECTION CRITIQUE ---
        // Lecture de la donnée
        item = partagee->tab[partagee->j];
        printf("<- Consommateur : lu %d (index %d)\n", item, partagee->j);

        // Avancée de l'index de lecture (circulaire)
        partagee->j = (partagee->j + 1) % N;

        // --- LIBÉRATION ---
        // 1. On rend la clé
        sem_post(mutex);           
        
        // 2. On signale qu'une place s'est LIBÉRÉE
        // (Incrémente places_libres, réveille le producteur s'il attendait une place)
        sem_post(places_libres);   

        sleep(1);
    }

    printf("--- Fin consommation ---\n");

    // =================================================================
    // 4. FERMETURE LOCALE
    // =================================================================
    // On ferme juste notre porte. On ne détruit pas le bâtiment (pas de unlink).
    // Si le consommateur finit avant le producteur, le producteur pourra
    // continuer jusqu'à la fin sans erreur.
    munmap(partagee, sizeof(MemoirePartagee));
    sem_close(places_libres);
    sem_close(items_existants);
    sem_close(mutex);
    
    return 0;
}
