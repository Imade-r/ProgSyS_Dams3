#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>   // Gestion de la mémoire partagée (mmap)
#include <semaphore.h>  // Sémaphores POSIX
#include <sys/wait.h>   // Gestion des processus (wait)
#include <fcntl.h>      // Constantes (O_CREAT, etc.)
#include <string.h>     // Manipulation de chaînes (snprintf)

// --- CONSTANTES ---
#define N 10            // Taille du tampon circulaire
#define NB_ITEMS 20     // Nombre total d'items à produire
#define TAILLE_MSG 64   // Taille fixe pour la chaîne de caractères

// --- STRUCTURE DE DONNÉES (VERSION 2) ---
// Structure enveloppe pour échanger des données complexes (ici une chaîne).
typedef struct {
    char texte[TAILLE_MSG];
} Donnee;

// --- MÉMOIRE PARTAGÉE ---
// Cette structure contient TOUT ce qui doit être visible par le père et le fils.
typedef struct {
    Donnee tab[N];          // Le tampon contient maintenant des structures Donnee
    int i;                  // Index d'écriture (Producteur)
    int j;                  // Index de lecture (Consommateur)
    
    // Les sémaphores sont placés ici pour être partagés entre processus.
    sem_t places_libres;    
    sem_t items_existants;
    sem_t mutex;
} Memoire_partagee;

int main() {
    printf("--- Démarrage (Version Fork V2 - Structures) ---\n");

    // =================================================================
    // 1. ALLOCATION MÉMOIRE PARTAGÉE ANONYME
    // =================================================================
    // On utilise mmap pour créer une zone de mémoire partagée sans fichier associé.
    // - PROT_READ | PROT_WRITE : La zone est accessible en lecture/écriture.
    // - MAP_SHARED : Les modifications sont visibles par les autres processus mappant cette zone.
    // - MAP_ANONYMOUS : Pas de fichier physique (descripteur -1).
    Memoire_partagee* partagee = mmap(NULL, sizeof(Memoire_partagee), 
                                      PROT_READ | PROT_WRITE, 
                                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (partagee == MAP_FAILED) {
        perror("Erreur critique mmap");
        exit(1);
    }

    // =================================================================
    // 2. INITIALISATION DES RESSOURCES
    // =================================================================
    partagee->i = 0;
    partagee->j = 0;

    // Initialisation des sémaphores pour processus (pshared = 1).
    // Si pshared valait 0, le sémaphore ne fonctionnerait qu'entre threads d'un même processus.
    sem_init(&partagee->places_libres, 1, N);   // Au départ, N places sont libres
    sem_init(&partagee->items_existants, 1, 0); // Au départ, 0 item présent
    sem_init(&partagee->mutex, 1, 1);           // Mutex libre (valeur 1)

    // =================================================================
    // 3. DUPLICATION DU PROCESSUS (FORK)
    // =================================================================
    // Le fils hérite du mappage mémoire du père (car créé avant le fork).
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("Erreur fork");
        exit(1);
    }

    // =================================================================
    // 4. CODE DU PROCESSUS FILS (CONSOMMATEUR)
    // =================================================================
    if (pid == 0) {
        // Le fils exécute ce bloc
        
        for(int k = 0; k < NB_ITEMS; k++) {
            Donnee item_recu; // Variable locale au fils
        
            // A. Attente passive si le tampon est vide
            sem_wait(&partagee->items_existants); 
            
            // B. Demande d'accès exclusif à la mémoire partagée
            sem_wait(&partagee->mutex);

            // --- SECTION CRITIQUE ---
            // Copie de la structure depuis la mémoire partagée vers la variable locale.
            // C'est ici que l'échange de données a lieu.
            item_recu = partagee->tab[partagee->j]; 
            
            printf("<- [Fils] Lecture : '%s' (index %d)\n", item_recu.texte, partagee->j);
            
            // Mise à jour de l'index de lecture (circulaire)
            partagee->j = (partagee->j + 1) % N; 
            // --- FIN SECTION CRITIQUE ---
        
            // C. Libération de l'accès exclusif
            sem_post(&partagee->mutex);
            
            // D. Signalement qu'une place s'est libérée dans le tampon
            sem_post(&partagee->places_libres);

            sleep(1); // Simulation traitement
        }
        
        // Fin du processus fils
        exit(0); 
    }
    
    // =================================================================
    // 5. CODE DU PROCESSUS PÈRE (PRODUCTEUR)
    // =================================================================
    else {
        // Le père exécute ce bloc
        
        for(int k = 0; k < NB_ITEMS; k++) {
            Donnee item_a_envoyer; // Variable locale au père
            
            // Préparation de la donnée (écriture formatée dans la structure locale)
            snprintf(item_a_envoyer.texte, TAILLE_MSG, "Colis numero %d", k * 10);
            
            // A. Attente passive si le tampon est plein
            sem_wait(&partagee->places_libres);
            
            // B. Demande d'accès exclusif
            sem_wait(&partagee->mutex);

            // --- SECTION CRITIQUE ---
            // Copie de la structure locale vers la mémoire partagée
            partagee->tab[partagee->i] = item_a_envoyer;
            
            printf("-> [Père] Écriture : '%s' (index %d)\n", item_a_envoyer.texte, partagee->i);
            
            // Mise à jour de l'index d'écriture (circulaire)
            partagee->i = (partagee->i + 1) % N;
            // --- FIN SECTION CRITIQUE ---

            // C. Libération de l'accès exclusif
            sem_post(&partagee->mutex);
            
            // D. Signalement qu'un nouvel item est disponible
            sem_post(&partagee->items_existants); 
            
            sleep(1); // Simulation production
        }

        // =================================================================
        // 6. FIN ET NETTOYAGE
        // =================================================================
        
        // Attente de la terminaison du fils pour éviter l'état zombie
        wait(NULL); 
        
        printf("--- Fin du traitement. Nettoyage... ---\n");

        // Destruction des sémaphores (bonne pratique avant de libérer la mémoire)
        sem_destroy(&partagee->places_libres);
        sem_destroy(&partagee->items_existants);
        sem_destroy(&partagee->mutex);
        
        // Détachement et libération de la mémoire partagée
        munmap(partagee, sizeof(Memoire_partagee));
    }

    return 0;
}