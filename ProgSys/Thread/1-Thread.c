#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>     // Pour sleep()
#include <pthread.h>    // Bibliothèque POSIX Threads (création, mutex, etc.)
#include <semaphore.h>  // Bibliothèque POSIX Semaphores

// --- PARAMÈTRES DU TAMPON ---
#define N 10            // Taille du tampon circulaire
#define NB_ITEMS 20     // Nombre total d'items à produire/consommer

// --- VARIABLES GLOBALES (MÉMOIRE PARTAGÉE PAR DÉFAUT) ---
// IMPORTANT TECHNIQUE :
// Contrairement aux processus (fork) où les variables globales sont dupliquées (COW),
// ici, les threads partagent le MÊME espace d'adressage virtuel.
// Le segment de données (.data et .bss) est commun.
// 'tab', 'i', 'j' sont donc directement accessibles et modifiables par tous les threads.
int tab[N];
int i = 0; // Index d'écriture (partagé)
int j = 0; // Index de lecture (partagé)

// --- OUTILS DE SYNCHRONISATION ---
// Ils sont déclarés en global pour être visibles de tous les threads.
sem_t places_libres;    // Sémaphore de comptage (Gestion flux producteur)
sem_t items_existants;  // Sémaphore de comptage (Gestion flux consommateur)

// Initialisation statique du Mutex.
// PTHREAD_MUTEX_INITIALIZER configure la structure pour un mutex "rapide" par défaut.
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 

// --- ROUTINE DU PRODUCTEUR ---
// Signature obligatoire : void* fonction(void* arg)
// C'est ce pointeur de fonction qui sera passé à pthread_create.
void * producteur(void * arg) {
    // 'k' est une variable LOCALE (sur la PILE du thread producteur).
    // Elle n'est PAS partagée avec le consommateur.
    for (int k = 0; k < NB_ITEMS; k++) {
        int item = k * 10;
        
        // 1. ATTENTE PASSIVE (P) sur places_libres
        // Si places_libres > 0 : décrémente et continue immédiatement.
        // Si places_libres == 0 : le noyau met le thread en état "WAITING" (bloqué).
        // Il ne consomme plus de CPU jusqu'à ce qu'une place se libère.
        sem_wait(&places_libres); 

        // 2. ENTRÉE EN SECTION CRITIQUE (MUTEX LOCK)
        // On demande l'accès exclusif aux variables partagées (tab, i).
        // Si le mutex est déjà pris (par le consommateur), ce thread est suspendu.
        // Cela garantit l'ATOMICITÉ des opérations qui suivent.
        pthread_mutex_lock(&mutex); 

        // --- DÉBUT SECTION CRITIQUE ---
        // Seul UN thread à la fois peut exécuter ces lignes.
        tab[i] = item;
        printf("[Prod] Écriture de %d à l'index %d\n", item, i);
        
        // Gestion circulaire : (0 -> 1 -> ... -> 9 -> 0)
        i = (i + 1) % N; 
        // --- FIN SECTION CRITIQUE ---
        
        // 3. SORTIE SECTION CRITIQUE (MUTEX UNLOCK)
        // On libère le verrou. Si le consommateur attendait le verrou, il est réveillé.
        pthread_mutex_unlock(&mutex); 
        
        // 4. SIGNALEMENT (V) sur items_existants
        // On incrémente le nombre d'items disponibles.
        // Si le consommateur dormait sur sem_wait(&items_existants), il est réveillé.
        sem_post(&items_existants); 

        sleep(1); 
    }
    
    // Terminaison propre du thread.
    // pthread_exit permet de renvoyer une valeur (ici NULL) récupérable par pthread_join.
    pthread_exit(NULL); 
}

// --- ROUTINE DU CONSOMMATEUR ---
void * consommateur(void * arg) {
    for (int k = 0; k < NB_ITEMS; k++) {
        int item; 
        
        // 1. ATTENTE PASSIVE (P) sur items_existants
        // Bloque tant que le tampon est vide (valeur sémaphore à 0).
        sem_wait(&items_existants); 

        // 2. ENTRÉE EN SECTION CRITIQUE
        // Protection des accès à 'tab' et 'j'.
        pthread_mutex_lock(&mutex);

        // --- DÉBUT SECTION CRITIQUE ---
        // Lecture de la variable globale partagée
        item = tab[j];
        printf("[Conso] Lecture de %d à l'index %d\n", item, j);
        j = (j + 1) % N;
        // --- FIN SECTION CRITIQUE ---
        
        // 3. SORTIE SECTION CRITIQUE
        pthread_mutex_unlock(&mutex);

        // 4. SIGNALEMENT (V) sur places_libres
        // Indique qu'une case vient d'être libérée.
        // Si le producteur était bloqué (tampon plein), il est réveillé.
        sem_post(&places_libres); 

        sleep(1); 
    }
    pthread_exit(NULL); 
}
        
int main() {
    // Structures opaques pour identifier les threads (LWP - Light Weight Process)
    pthread_t th_prod, th_conso; 

    printf("--- Debut avec Threads (Mémoire partagée intra-processus) ---\n");

    // --- INITIALISATION DES SÉMAPHORES ---
    // Prototype : int sem_init(sem_t *sem, int pshared, unsigned int value);
    // pshared = 0 : Le sémaphore est partagé entre les THREADS d'un processus.
    //               (Il est stocké à une adresse visible par tous les threads).
    // N : Valeur initiale de places_libres (Tampon vide au départ).
    sem_init(&places_libres, 0, N);
    
    // 0 : Valeur initiale de items_existants (Aucun item à lire au départ).
    sem_init(&items_existants, 0, 0);
    
    // --- CRÉATION DES THREADS ---
    // pthread_create lance une nouvelle tâche d'exécution partageant le même PID global
    // mais ayant son propre TID (Thread ID), sa propre pile (Stack) et ses registres.
    if (pthread_create(&th_prod, NULL, producteur, NULL) != 0) {
        perror("Erreur création thread producteur");
        exit(1);
    }

    if (pthread_create(&th_conso, NULL, consommateur, NULL) != 0) {
        perror("Erreur création thread consommateur");
        exit(1);
    }
    
    // --- SYNCHRONISATION DE TERMINAISON ---
    // pthread_join est l'équivalent de wait() pour les threads.
    // Le thread principal (main) est suspendu jusqu'à ce que th_prod termine.
    // Sans cela, le return 0 du main tuerait brutalement tous les threads secondaires.
    pthread_join(th_prod, NULL);
    pthread_join(th_conso, NULL);

    // --- NETTOYAGE DES RESSOURCES ---
    // Destruction des objets de synchronisation pour libérer les ressources noyau associées.
    sem_destroy(&places_libres);
    sem_destroy(&items_existants);
    pthread_mutex_destroy(&mutex); 

    printf("--- Fin du programme ---\n");
    return 0;
}