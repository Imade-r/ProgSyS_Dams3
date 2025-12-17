#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include "3-common.h"

volatile int stop = 0;

void handler(int sig) {
    stop = 1;
}

int main() {
    // 1. CONFIGURATION DU SIGNAL
    struct sigaction psa;
    psa.sa_handler = handler;
    sigemptyset(&psa.sa_mask);
    psa.sa_flags = 0;
    sigaction(SIGINT, &psa, NULL);

    // 2. CONNEXION MÉMOIRE PARTAGÉE
    int fd_shm = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd_shm == -1) {
        perror("Lancez le producteur avant");
        exit(1);
    }
    MemoirePartagee* partagee = mmap(NULL, sizeof(MemoirePartagee), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
    close(fd_shm);

    sem_t *places_libres = sem_open(SEM_PLACES_LIBRES, 0);
    sem_t *items_existants = sem_open(SEM_ITEMS_EXISTANTS, 0);
    sem_t *mutex = sem_open(SEM_MUTEX, 0);

    // 3. MISE EN PLACE DU TUBE (FIFO)
    mkfifo(FIFO_CONSO, 0666);
    // Ouverture non-bloquante pour ne pas figer le programme
    int fd_fifo = open(FIFO_CONSO, O_RDONLY | O_NONBLOCK);
    
    printf("--- Consommateur V3 (Pilotable) Démarré ---\n");

    while (!stop) {
        // A. LECTURE DU TUBE (Prioritaire)
        char buffer_cmd[128];
        // On nettoie le buffer avant de lire pour éviter les résidus "bonjour"
        memset(buffer_cmd, 0, sizeof(buffer_cmd));
        
        ssize_t octets_lus = read(fd_fifo, buffer_cmd, sizeof(buffer_cmd) - 1);
        
        if (octets_lus > 0) {
            // Sécurité : On force la fin de chaîne
            buffer_cmd[octets_lus] = '\0';
            
            if (strcmp(buffer_cmd, "stop") == 0) {
                printf("\n[SYSTEM] Ordre d'arrêt reçu via le tube.\n");
                stop = 1;
                break;
            } else {
                // AFFICHAGE TRES VISIBLE POUR DISTINGUER
                printf("\n**************************************************\n");
                printf("   MESSAGE EXTERNE REÇU : %s\n", buffer_cmd);
                printf("**************************************************\n\n");
            }
        }

        // B. CONSOMMATION NORMALE (Flux du producteur)
        Donnee item;
        
        if (sem_wait(items_existants) == -1) {
            if (stop) break;
            if (errno == EINTR) continue;
        }

        sem_wait(mutex);
        item = partagee->tab[partagee->j];
        
        // Affichage standard du flux
        printf("<- Conso : Lu '%s' (idx %d)\n", item.texte, partagee->j);
        
        partagee->j = (partagee->j + 1) % N;
        sem_post(mutex);
        sem_post(places_libres);

        sleep(1);
    }

    printf("\n[Consommateur] Fin.\n");

    munmap(partagee, sizeof(MemoirePartagee));
    sem_close(places_libres);
    sem_close(items_existants);
    sem_close(mutex);

    close(fd_fifo);
    unlink(FIFO_CONSO);

    return 0;
}