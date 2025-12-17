#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>   // Mémoire Partagée
#include <sys/stat.h>   // Pour mkfifo
#include <fcntl.h>      // Constantes O_*
#include <semaphore.h>  // Sémaphores
#include <signal.h>     // Gestion signaux
#include <string.h>
#include <errno.h>      // Gestion erreurs (EINTR, EAGAIN)
#include "3-common.h"

// Variable globale modifiée par le handler de signal (interruption).
// 'volatile' empêche le compilateur d'optimiser les lectures de cette variable.
volatile int stop = 0;

// Handler exécuté lors de la réception de SIGINT (Ctrl+C)
void handler(int sig) {
    stop = 1;
}

int main() {
    // =================================================================
    // 1. CONFIGURATION DES SIGNAUX
    // =================================================================
    struct sigaction psa;
    psa.sa_handler = handler;  // Fonction à appeler
    sigemptyset(&psa.sa_mask); // Pas de masquage supplémentaire
    psa.sa_flags = 0;          // Comportement par défaut
    
    // Interception de Ctrl+C
    sigaction(SIGINT, &psa, NULL);

    // =================================================================
    // 2. INITIALISATION MÉMOIRE PARTAGÉE (COTE CRÉATEUR)
    // =================================================================
    // O_CREAT : Crée l'objet mémoire s'il n'existe pas.
    // O_RDWR : Lecture et écriture.
    // 0666 : Droits d'accès (rw-rw-rw-).
    int fd_shm = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    
    // ftruncate : Définit la taille physique de l'objet mémoire.
    // Indispensable après un shm_open avec O_CREAT.
    ftruncate(fd_shm, sizeof(MemoirePartagee));
    
    // mmap : Projette l'objet mémoire dans l'espace d'adressage du processus.
    // On obtient un pointeur utilisable comme n'importe quel pointeur C.
    MemoirePartagee* partagee = mmap(NULL, sizeof(MemoirePartagee), 
                                     PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
    close(fd_shm); // Le descripteur n'est plus utile après le mmap.

    // Initialisation des index (Seul le créateur le fait)
    partagee->i = 0;
    partagee->j = 0;

    // Création des sémaphores nommés
    sem_t *places_libres = sem_open(SEM_PLACES_LIBRES, O_CREAT, 0666, N);
    sem_t *items_existants = sem_open(SEM_ITEMS_EXISTANTS, O_CREAT, 0666, 0);
    sem_t *mutex = sem_open(SEM_MUTEX, O_CREAT, 0666, 1);

    // =================================================================
    // 3. MISE EN PLACE DU TUBE NOMMÉ (FIFO)
    // =================================================================
    // mkfifo : Crée un fichier spécial de type FIFO (First In, First Out)
    // qui servira de canal de communication unidirectionnel.
    mkfifo(FIFO_PROD, 0666);

    // OUVERTURE NON-BLOQUANTE (CRITIQUE)
    // O_NONBLOCK : Permet d'ouvrir le tube même si aucun écrivain n'est connecté.
    // De plus, les futures lectures (read) retourneront immédiatement s'il n'y a rien à lire,
    // au lieu de bloquer le programme.
    int fd_fifo = open(FIFO_PROD, O_RDONLY | O_NONBLOCK);
    if (fd_fifo == -1) {
        perror("Avertissement : Erreur ouverture FIFO");
    }

    printf("--- Producteur V3 (Pilotable) Démarré ---\n");

    int k = 0;
    char message_actuel[TAILLE_MSG];
    snprintf(message_actuel, TAILLE_MSG, "Defaut"); // Message de base

    // BOUCLE PRINCIPALE
    while (!stop) {
        // A. LECTURE NON-BLOQUANTE DU TUBE
        // On vérifie s'il y a un message du communicant.
        char buffer_cmd[128];
        ssize_t octets_lus = read(fd_fifo, buffer_cmd, sizeof(buffer_cmd));
        
        if (octets_lus > 0) {
            // Lecture réussie !
            printf("\n[COMMANDE REÇUE] : '%s'\n", buffer_cmd);
            
            if (strcmp(buffer_cmd, "stop") == 0) {
                printf("Ordre d'arrêt reçu via le tube.\n");
                stop = 1;
                break; // On sort de la boucle immédiatement
            } else {
                // Mise à jour du message à produire
                strncpy(message_actuel, buffer_cmd, TAILLE_MSG);
                message_actuel[TAILLE_MSG - 1] = '\0'; // Sécurité débordement
            }
        }
        // Si read retourne -1 avec errno==EAGAIN, c'est normal (tube vide), on continue.

        // B. PRODUCTION NORMALE
        Donnee item;
        snprintf(item.texte, TAILLE_MSG, "%s-%d", message_actuel, k++);

        // Attente d'une place libre
        if (sem_wait(places_libres) == -1) {
            // Si interrompu par Ctrl+C, on arrête
            if (stop) break;
            // Si interrompu par un autre signal, on recommence
            if (errno == EINTR) continue;
        }

        // Section Critique
        sem_wait(mutex);
        partagee->tab[partagee->i] = item;
        printf("-> Prod : Ecrit '%s' (idx %d)\n", item.texte, partagee->i);
        partagee->i = (partagee->i + 1) % N;
        sem_post(mutex);
        
        // Signalement nouvel item
        sem_post(items_existants);

        sleep(1);
    }

    // =================================================================
    // 4. NETTOYAGE COMPLET (Rôle du Créateur)
    // =================================================================
    printf("\n[Producteur] Fin. Nettoyage des ressources système.\n");

    munmap(partagee, sizeof(MemoirePartagee));
    sem_close(places_libres);
    sem_close(items_existants);
    sem_close(mutex);
    
    // Destruction des objets systèmes (unlink)
    // Cela supprime les fichiers dans /dev/shm et /tmp
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_PLACES_LIBRES);
    sem_unlink(SEM_ITEMS_EXISTANTS);
    sem_unlink(SEM_MUTEX);
    
    // Fermeture et destruction du tube
    close(fd_fifo);
    unlink(FIFO_PROD); 

    return 0;
}