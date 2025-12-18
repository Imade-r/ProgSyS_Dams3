#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>   
#include <semaphore.h>  
#include <sys/wait.h>   
#include <fcntl.h>      
#include <string.h>     
#include <sys/stat.h>   // Pour mkfifo
#include <errno.h>      // Pour gérer les erreurs 

// --- CONSTANTES ---
#define N 10            
#define TAILLE_MSG 64   

//Noms des tubes 
#define FIFO_P "/tmp/fifo_producteur"
#define FIFO_C "/tmp/fifo_consommateur"

typedef struct {
    char texte[TAILLE_MSG];
} Donnee;

typedef struct {
    Donnee tab[N];          
    int i;                  
    int j;                  
    sem_t places_libres;    
    sem_t items_existants;
    sem_t mutex;
} Memoire_partagee;

int main() {
    printf("--- Démarrage (Version Fork V2 + Communicant) ---\n");

    // Création des tubes nommés
    // On le fait ici pour être sûr qu'ils existent avant que quiconque n'essaie de les ouvrir
    mkfifo(FIFO_P, 0644);
    mkfifo(FIFO_C, 0644);

    // 1. ALLOCATION MÉMOIRE PARTAGÉE
    Memoire_partagee* partagee = mmap(NULL, sizeof(Memoire_partagee), 
                                      PROT_READ | PROT_WRITE, 
                                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (partagee == MAP_FAILED) { perror("mmap"); exit(1); }

    // 2. INITIALISATION
    partagee->i = 0;
    partagee->j = 0;
    sem_init(&partagee->places_libres, 1, N);   
    sem_init(&partagee->items_existants, 1, 0); 
    sem_init(&partagee->mutex, 1, 1);           

    // Variable de contrôle d'arrêt (Locale à chaque processus après le fork)
    int stop = 0;

    // 3. FORK
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }

    // =================================================================
    // 4. CONSOMMATEUR (FILS)
    // =================================================================
    if (pid == 0) {
        //Avec O_NONBLOCK, open dit : "Oouvre le tube, et si personne 
        //n'écrit dedans pour l'instant, ce n'est pas grave, continue l'exécution tout de suite."
        int fd_fifo = open(FIFO_C, O_RDONLY | O_NONBLOCK);
        
        // On boucle tant que stop est faux (piloté par le communicant)
        while (!stop) { 
            
            // --- A. Écoute du Communicant ---
            if (fd_fifo != -1) {
                char buffer[128];
                int n = read(fd_fifo, buffer, 127); // Lecture non-bloquante
                //Si il a lu quelquechose
                if (n > 0) {
                    //read ne met pas le \0 , il faut le mettre pour strcmp
                    buffer[n] = '\0';

                    //si les 4 premières lettres sont "stop".
                    if (strncmp(buffer, "stop", 4) == 0) {
                        printf("! [Fils] Ordre STOP reçu.\n");
                        stop = 1; // On sort de la boucle
                    } 
                    
                    else {
                        printf("! [Fils] Message ADMIN : %s\n", buffer);
                    }
                }
            }
            
            // --- B. Consommation (Version Non-Bloquante) ---
            // On utilise sem_trywait au lieu de sem_wait.
            // Si le tampon est vide, sem_wait bloquerait tout le processus,
            // et on ne pourrait plus lire le tube pour recevoir l'ordre "stop".
            if (!stop && sem_trywait(&partagee->items_existants) == 0) {
                
                sem_wait(&partagee->mutex); // Accès exclusif

                Donnee item_recu = partagee->tab[partagee->j]; 
                printf("<- [Fils] Lecture : '%s' (idx %d)\n", item_recu.texte, partagee->j);
                partagee->j = (partagee->j + 1) % N; 

                sem_post(&partagee->mutex);
                sem_post(&partagee->places_libres);

                sleep(1); 
            } else {
                //Pour pas surcharger le CPU (100ms)
                usleep(100000); 
            }
        }
        
        // Nettoyage fils
        if (fd_fifo != -1) close(fd_fifo);
        exit(0); 
    }
    
    // =================================================================
    // 5. PRODUCTEUR (PÈRE)
    // =================================================================
    else {
        //Ouverture du tube producteur
        int fd_fifo = open(FIFO_P, O_RDONLY | O_NONBLOCK);
        
        char message_actuel[TAILLE_MSG] = "Colis defaut"; // Message par défaut
        int k = 0;

        while (!stop) {
            
            // --- A. Écoute du Communicant ---
            if (fd_fifo != -1) {
                char buffer[128];
                int n = read(fd_fifo, buffer, 127);
                if (n > 0) {
                    buffer[n] = '\0';
                    if (strncmp(buffer, "stop", 4) == 0) {
                        printf("! [Père] Ordre STOP reçu.\n");
                        stop = 1; 
                    } else {
                        // On change le message produit
                        printf("! [Père] Changement production -> '%s'\n", buffer);
                        strncpy(message_actuel, buffer, TAILLE_MSG - 1);
                        message_actuel[TAILLE_MSG - 1] = '\0';
                    }
                }
            }

            // --- B. Production (Version Non-Bloquante) ---
            if (!stop && sem_trywait(&partagee->places_libres) == 0) {
                
                sem_wait(&partagee->mutex);

                // On écrit le message actuel (qui a pu être changé par le communicant)
                snprintf(partagee->tab[partagee->i].texte, TAILLE_MSG, "%s-%d", message_actuel, k++);
                
                printf("-> [Père] Écriture : '%s' (idx %d)\n", partagee->tab[partagee->i].texte, partagee->i);
                partagee->i = (partagee->i + 1) % N;

                sem_post(&partagee->mutex);
                sem_post(&partagee->items_existants); 
                
                sleep(1);
            } else {
                usleep(100000); 
            }
        }

        // =================================================================
        // 6. FIN ET NETTOYAGE
        // =================================================================
        
        if (fd_fifo != -1) close(fd_fifo);
        
        // On attend la fin du fils (qui a dû recevoir son propre stop ou qu'on doit tuer)
        // Ici, le communicant envoie stop aux deux manuellement ou on peut tuer le fils :
        kill(pid, SIGTERM); 
        wait(NULL); 
        
        printf("--- Fin du traitement. Nettoyage... ---\n");

        sem_destroy(&partagee->places_libres);
        sem_destroy(&partagee->items_existants);
        sem_destroy(&partagee->mutex);
        
        munmap(partagee, sizeof(Memoire_partagee));

        // Suppression des tubes
        unlink(FIFO_P);
        unlink(FIFO_C);
    }

    return 0;
}
