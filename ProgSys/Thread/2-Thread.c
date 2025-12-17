#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>     // Pour sleep()
#include <pthread.h>    // API POSIX Threads [CM p.146]
#include <semaphore.h>  // API POSIX Semaphores [CM p.126]
#include <string.h>     // Pour manipulation de chaînes (snprintf)

// --- CONSTANTES DE CONFIGURATION ---
#define N 10            // Taille du tampon circulaire (Nombre de slots)
#define NB_ITEMS 20     // Nombre total d'items à produire/consommer
#define TAILLE_MSG 64   // Taille fixe du buffer pour chaque message

// --- STRUCTURE DE DONNÉES ---
// Utilisation d'une structure pour encapsuler la chaîne de caractères.
// Cela permet de copier la donnée par simple affectation (=) plutôt que par strcpy.
typedef struct {
    char texte[TAILLE_MSG];
} Donnee;

// --- MÉMOIRE PARTAGÉE (SEGMENT .BSS/.DATA) ---
// TECHNIQUE : Dans un processus multi-threadé, les variables globales
// sont situées dans un segment mémoire commun à TOUS les threads.
// Contrairement à fork() qui duplique cet espace (Copy-On-Write),
// ici l'adresse &tab est IDENTIQUE pour le producteur et le consommateur.
Donnee tab[N]; // Le tampon partagé
int in = 0;    // Index d'écriture (Producteur)
int out = 0;   // Index de lecture (Consommateur)

// --- SYNCHRONISATION ---
sem_t places_libres;    // Sémaphore comptant les slots vides
sem_t items_existants;  // Sémaphore comptant les items prêts à être lus

// Mutex (Mutual Exclusion)
// Initialisé statiquement. Il garantit que le code entre lock() et unlock()
// est atomique du point de vue des autres threads.
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 

// --- ROUTINE DU PRODUCTEUR ---
// Exécutée dans un contexte de thread propre (sa propre pile d'exécution).
void * producteur(void * arg) {
    // La variable 'k' est sur la PILE (Stack) du thread producteur.
    // Elle est privée et invisible pour le consommateur.
    for (int k = 0; k < NB_ITEMS; k++) {
        Donnee item; // Variable locale (privée)
        
        // Préparation de la donnée "hors-ligne" (ne nécessite pas de verrou)
        // snprintf est utilisé pour éviter les dépassements de tampon (buffer overflow).
        snprintf(item.texte, TAILLE_MSG, "ThreadMsg %d", k);
        
        // 1. PROTOCOLE D'ENTRÉE (FLUX)
        // Si places_libres == 0, le noyau met ce thread en état "WAITING".
        // Il est retiré de la file d'ordonnancement du CPU.
        sem_wait(&places_libres);

        // 2. PROTOCOLE D'ENTRÉE (EXCLUSION MUTUELLE)
        // On verrouille l'accès aux variables globales (tab, in).
        pthread_mutex_lock(&mutex); 

        // --- DÉBUT SECTION CRITIQUE ---
        // Seul ce thread exécute ces instructions à cet instant T.
        
        // Copie mémoire de la structure locale vers la mémoire globale.
        tab[in] = item; 
        printf("-> Producteur : Ecrit '%s' index %d\n", item.texte, in);
        
        // Mise à jour de l'index avec gestion circulaire (Modulo N)
        in = (in + 1) % N;
        // --- FIN SECTION CRITIQUE ---
        
        // 3. PROTOCOLE DE SORTIE (EXCLUSION MUTUELLE)
        // On libère le verrou. Si le consommateur attendait sur ce mutex,
        // il passe de "WAITING" à "READY".
        pthread_mutex_unlock(&mutex); 
        
        // 4. PROTOCOLE DE SORTIE (FLUX)
        // On signale qu'un nouvel item est disponible.
        sem_post(&items_existants);

        // Simulation de temps de traitement (le thread cède volontairement le CPU)
        sleep(1);
    }
    // Terminaison du thread et libération de sa pile.
    pthread_exit(NULL);
}

// --- ROUTINE DU CONSOMMATEUR ---
void * consommateur(void * arg) {
    for (int k = 0; k < NB_ITEMS; k++) {
        Donnee item; // Variable locale pour recevoir la copie
        
        // 1. ATTENTE PASSIVE
        // Bloque tant qu'il n'y a rien à lire (items_existants == 0).
        sem_wait(&items_existants);

        // 2. VERROUILLAGE
        // Protection des accès à 'tab' et 'out'.
        pthread_mutex_lock(&mutex);

        // --- DÉBUT SECTION CRITIQUE ---
        // Copie mémoire de la mémoire globale vers la variable locale.
        // C'est ici que la donnée "quitte" logiquement le tampon partagé.
        item = tab[out]; 
        printf("<- Consommateur : Lu '%s' index %d\n", item.texte, out);
        
        // Avancée de l'index de lecture
        out = (out + 1) % N;
        // --- FIN SECTION CRITIQUE ---
        
        // 3. DÉVERROUILLAGE
        pthread_mutex_unlock(&mutex);

        // 4. SIGNALEMENT
        // Une place s'est libérée dans le tampon.
        sem_post(&places_libres);

        sleep(1);
    }
    pthread_exit(NULL);
}

int main() {
    // Identifiants opaques des threads (correspondent souvent à des adresses ou ID noyau)
    pthread_t th_prod, th_conso; 

    printf("--- Debut avec Threads (Strings) ---\n");

    // --- INITIALISATION DES RESSOURCES ---
    // sem_init avec le 2ème argument à 0 indique que le sémaphore est partagé
    // entre les threads d'un MÊME processus (adresse mémoire virtuelle commune).
    sem_init(&places_libres, 0, N); // Tampon vide => N places libres
    sem_init(&items_existants, 0, 0); // Tampon vide => 0 item existant
    
    // --- CRÉATION DES THREADS (LWP) ---
    // pthread_create alloue une pile pour le thread et demande au noyau de l'ordonnancer.
    // Le thread commence son exécution à la fonction passée en 3ème argument.
    if (pthread_create(&th_prod, NULL, producteur, NULL) != 0) {
        perror("Erreur critique: pthread_create prod");
        exit(1);
    }

    if (pthread_create(&th_conso, NULL, consommateur, NULL) != 0) {
        perror("Erreur critique: pthread_create conso");
        exit(1);
    }
    
    // --- SYNCHRONISATION PARENT/ENFANTS ---
    // Le main (thread principal) doit attendre la fin des threads secondaires.
    // Sinon, 'return 0' appellerait exit(), tuant brutalement tous les threads du processus.
    pthread_join(th_prod, NULL);
    pthread_join(th_conso, NULL);

    printf("--- Fin des threads ---\n");

    // --- NETTOYAGE ---
    // Libération des ressources noyau associées aux sémaphores et au mutex.
    sem_destroy(&places_libres);
    sem_destroy(&items_existants);
    pthread_mutex_destroy(&mutex);

    return 0;
}