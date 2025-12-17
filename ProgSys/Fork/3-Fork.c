#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>   // Gestion de la mémoire partagée
#include <semaphore.h>  // Sémaphores POSIX
#include <sys/wait.h>   // Gestion des processus (wait)
#include <fcntl.h>
#include <string.h>
#include <signal.h>     // Pour sigaction
#include <errno.h>      // Pour EINTR

#define N 10 
#define TAILLE_MSG 64

// --- VARIABLE GLOBALE (Non-partagée entre processus) ---
// Contrairement aux threads, cette variable est DUPLIQUÉE lors du fork.
// Le Père aura sa copie, le Fils aura la sienne.
// Lors d'un Ctrl+C, le signal est envoyé au GROUPE de processus :
// le Père et le Fils le reçoivent tous les deux et modifient leur propre variable.
int stop = 0; 

// Gestionnaire de signal
void handler_signal(int sig) {
    // Write est "async-signal-safe", contrairement à printf.
    // On l'utilise pour éviter des blocages rares mais possibles dans le handler.
    if (stop == 0) {
        write(STDOUT_FILENO, "\n[SYSTEM] Signal reçu (Arrêt demandé)\n", 38);
    }
    stop = 1; 
}

// --- STRUCTURE DE DONNÉES ---
typedef struct {
    char texte[TAILLE_MSG];
} Donnee;

// --- MÉMOIRE PARTAGÉE (Partagée entre processus) ---
typedef struct {
    Donnee tab[N];          
    int i;                  
    int j;                  
    sem_t places_libres;    
    sem_t items_existants;  
    sem_t mutex;            
} MemoirePartagee;

int main() {
    // =================================================================
    // 1. CONFIGURATION DU SIGNAL (sigaction)
    // =================================================================
    // Cette configuration est faite AVANT le fork, donc le Père et le Fils
    // hériteront du même comportement face au signal SIGINT.
    struct sigaction sa; 
    sa.sa_handler = handler_signal;
    sa.sa_flags = 0; // Pas de comportement spécial (comme SA_RESTART)
    // On vide le masque (pas de signaux bloqués pendant l'exécution du handler)
    sigemptyset(&sa.sa_mask); 

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Erreur sigaction");
        exit(1);
    }

    // =================================================================
    // 2. ALLOCATION MÉMOIRE PARTAGÉE
    // =================================================================
    MemoirePartagee* partage = mmap(NULL, sizeof(MemoirePartagee), 
                                    PROT_READ | PROT_WRITE, 
                                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (partage == MAP_FAILED) { perror("mmap"); exit(1); }

    // Initialisation
    partage->i = 0; 
    partage->j = 0;

    // =================================================================
    // 3. INITIALISATION SÉMAPHORES (Inter-processus)
    // =================================================================
    // pshared = 1 est INDISPENSABLE ici pour que les sémaphores fonctionnent
    // entre deux processus distincts (mémoire mappée).
    sem_init(&partage->places_libres, 1, N); 
    sem_init(&partage->items_existants, 1, 0); 
    sem_init(&partage->mutex, 1, 1); 

    printf("--- Démarrage Fork V3 (Signal) - PID Père: %d ---\n", getpid());

    // =================================================================
    // 4. DUPLICATION (FORK)
    // =================================================================
    pid_t pid = fork();

    if (pid < 0) { perror("fork"); exit(1); }

    // --- CODE DU FILS (CONSOMMATEUR) ---
    if (pid == 0) {
        while (!stop) { 
            Donnee item_recu;
            
            // A. Attente d'un item (Interruptible par signal)
            if (sem_wait(&partage->items_existants) == -1) {
                // Si l'erreur est EINTR, c'est que le signal a interrompu l'appel.
                // On boucle pour revérifier la condition 'while(!stop)'.
                if (errno == EINTR) continue; 
                perror("sem_wait fils"); break;
            }

            // Vérification post-réveil : si stop est mis, on n'entre pas en section critique
            if (stop) {
                sem_post(&partage->items_existants); // On "rend" le jeton si on ne consomme pas
                break;
            }

            // B. Accès Exclusif
            sem_wait(&partage->mutex);

            // --- Section Critique ---
            item_recu = partage->tab[partage->j];
            partage->j = (partage->j + 1) % N;
            // ------------------------

            sem_post(&partage->mutex);

            // C. Libération place
            sem_post(&partage->places_libres);

            printf("   <- [Fils] Lu : '%s'\n", item_recu.texte);
            sleep(1);
        }
        
        printf("   [Fils] Fin du processus (PID %d).\n", getpid());
        exit(0); 
    } 
    
    // --- CODE DU PÈRE (PRODUCTEUR) ---
    else {
        int k = 0;
        while (!stop) {
            Donnee item_a_envoyer;
            snprintf(item_a_envoyer.texte, TAILLE_MSG, "Msg n°%d", k++);
            
            // A. Attente d'une place (Interruptible)
            if (sem_wait(&partage->places_libres) == -1) {
                if (errno == EINTR) continue; // Interruption signal
                perror("sem_wait père"); break;
            }

            if (stop) {
                sem_post(&partage->places_libres);
                break;
            }

            // B. Accès Exclusif
            sem_wait(&partage->mutex);

            // --- Section Critique ---
            partage->tab[partage->i] = item_a_envoyer;
            partage->i = (partage->i + 1) % N;
            // ------------------------

            sem_post(&partage->mutex);

            // C. Nouvel item disponible
            sem_post(&partage->items_existants); 
            
            printf("-> [Père] Écrit : '%s'\n", item_a_envoyer.texte);
            sleep(1);
        }

        // =================================================================
        // 5. TERMINAISON PROPRE
        // =================================================================
        printf("\n[Père] Attente de la fin du fils...\n");
        
        // Le père a reçu le signal et est sorti de la boucle.
        // Il doit attendre que le fils (qui a aussi reçu le signal) termine.
        wait(NULL); 
        
        printf("[Père] Nettoyage des ressources partagées.\n");
        sem_destroy(&partage->places_libres);
        sem_destroy(&partage->items_existants);
        sem_destroy(&partage->mutex);
        munmap(partage, sizeof(MemoirePartagee));
        
        printf("[Père] Fin du programme.\n");
    }
    return 0;
}