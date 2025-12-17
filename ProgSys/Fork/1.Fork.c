#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>   // Pour mmap (gestion mémoire partagée)
#include <semaphore.h>  // Pour les sémaphores
#include <sys/wait.h>   // Pour wait()
#include <fcntl.h>      // Pour les constantes O_CREAT etc.

// --- PARAMÈTRES DU TAMPON ---
#define N 10            // Taille du tampon circulaire
#define NB_ITEMS 20     // Nombre total d'items à produire/consommer

// --- STRUCTURE DE MÉMOIRE PARTAGÉE ---
// Cette structure regroupe toutes les données qui doivent être visibles
// par le processus père et le processus fils.
typedef struct {
    int tab[N];             // Le tampon de données (buffer circulaire)
    int i;                  // Index d'écriture (utilisé par le Producteur)
    int j;                  // Index de lecture (utilisé par le Consommateur)
    
    // Les sémaphores doivent impérativement être stockés en mémoire partagée
    // pour que les opérations wait/post agissent sur les mêmes compteurs.
    sem_t places_libres;    
    sem_t items_existants;
    sem_t mutex;
} Memoire_partagee;

int main() {
    printf("--- Démarrage (Version Processus/Fork) ---\n");

    // =================================================================
    // 1. ALLOCATION DE LA MÉMOIRE PARTAGÉE
    // =================================================================
    // mmap crée un mappage en mémoire virtuelle.
    // MAP_ANONYMOUS : La mémoire n'est pas adossée à un fichier.
    // MAP_SHARED : Les mises à jour sont visibles par les autres processus mappant cette zone.
    Memoire_partagee* partagee = mmap(NULL, sizeof(Memoire_partagee), 
                                      PROT_READ | PROT_WRITE, 
                                      MAP_SHARED | MAP_ANONYMOUS, -1, 0); 

    if (partagee == MAP_FAILED) {
        perror("Erreur mmap");
        exit(1);
    }

    // =================================================================
    // 2. INITIALISATION DES RESSOURCES
    // =================================================================
    partagee->i = 0;
    partagee->j = 0;

    // Initialisation des sémaphores POSIX non-nommés.
    // Le 2ème argument (pshared) est mis à 1.
    // Cela indique que le sémaphore est partagé entre processus (et non juste entre threads).
    sem_init(&partagee->places_libres, 1, N);   // Compteur de places vides (init à N)
    sem_init(&partagee->items_existants, 1, 0); // Compteur d'items prêts (init à 0)
    sem_init(&partagee->mutex, 1, 1);           // Exclusion mutuelle (init à 1 = libre)

    // =================================================================
    // 3. CRÉATION DU PROCESSUS FILS
    // =================================================================
    // fork() duplique le processus courant.
    // Le père reçoit le PID du fils, le fils reçoit 0.
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("Erreur fork");
        exit(1);
    }

    // =================================================================
    // 4. CODE DU PROCESSUS FILS (CONSOMMATEUR)
    // =================================================================
    if (pid == 0) {
        printf("[Fils] Processus Consommateur démarré (PID %d)\n", getpid());

        for(int k = 0; k < NB_ITEMS; k++) {
            int item;
        
            // 1. Attente passive si aucun item n'est disponible
            sem_wait(&partagee->items_existants); 
            
            // 2. Entrée en section critique (Exclusion Mutuelle)
            sem_wait(&partagee->mutex); 

            // --- DÉBUT SECTION CRITIQUE ---
            // Accès aux variables partagées tab et j
            item = partagee->tab[partagee->j]; 
            printf("<- [Fils] Lecture : %d (index %d)\n", item, partagee->j);
            partagee->j = (partagee->j + 1) % N; // Gestion circulaire
            // --- FIN SECTION CRITIQUE ---
        
            // 3. Sortie de section critique
            sem_post(&partagee->mutex); 
        
            // 4. Signalement qu'une place s'est libérée
            sem_post(&partagee->places_libres); 

            // Simulation du temps de traitement
            sleep(1); 
        }
        
        printf("[Fils] Fin du processus consommateur.\n");
        exit(0); // Terminaison du processus fils
    }
    
    // =================================================================
    // 5. CODE DU PROCESSUS PÈRE (PRODUCTEUR)
    // =================================================================
    else {
        printf("[Père] Processus Producteur démarré (PID %d)\n", getpid());

        for(int k = 0; k < NB_ITEMS; k++) {
            int item = k * 10;  // Production de la donnée
            
            // 1. Attente passive si le tampon est plein
            sem_wait(&partagee->places_libres); 
            
            // 2. Entrée en section critique
            sem_wait(&partagee->mutex);

            // --- DÉBUT SECTION CRITIQUE ---
            // Accès aux variables partagées tab et i
            partagee->tab[partagee->i] = item;
            printf("-> [Père] Écriture : %d (index %d)\n", item, partagee->i);
            partagee->i = (partagee->i + 1) % N; // Gestion circulaire
            // --- FIN SECTION CRITIQUE ---

            // 3. Sortie de section critique
            sem_post(&partagee->mutex);
            
            // 4. Signalement qu'un nouvel item est disponible
            sem_post(&partagee->items_existants);
            
            sleep(1); // Simulation du temps de production
        }

        // =================================================================
        // 6. SYNCHRONISATION TERMINALE
        // =================================================================
        
        // Le père attend la terminaison du fils pour éviter un processus zombie.
        wait(NULL); 
        
        printf("--- Fin du traitement. Nettoyage des ressources. ---\n");

        // Destruction des sémaphores
        sem_destroy(&partagee->places_libres);
        sem_destroy(&partagee->items_existants);
        sem_destroy(&partagee->mutex);
        
        // Libération de la mémoire partagée
        munmap(partagee, sizeof(Memoire_partagee));
    }

    return 0;
}