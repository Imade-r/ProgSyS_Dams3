#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>    // Nécessaire pour créer des threads (processus légers)
#include <semaphore.h>  // Nécessaire pour les sémaphores (gestion des places/items)
#include <string.h>
#include <signal.h>     // Nécessaire pour capturer Ctrl+C (SIGINT)
#include <errno.h>      // Pour analyser les erreurs (comme EINTR)

#define N 10            // La taille physique du tableau (tampon)
#define TAILLE_MSG 64   // Taille max du texte dans chaque case

// --- STRUCTURE DE DONNÉES ---
typedef struct {
    char texte[TAILLE_MSG];
} Donnee;

// --- VARIABLES GLOBALES (L'espace commun) ---
// Ces variables sont visibles et modifiables par TOUS les threads.
Donnee tab[N]; // Le tampon circulaire
int i = 0;     // Index d'écriture (utilisé par le producteur)
int j = 0;     // Index de lecture (utilisé par le consommateur)

// --- GESTION DE L'ARRÊT ---
// Variable "drapeau". 
// 0 = Tout va bien, on continue.
// 1 = L'utilisateur a fait Ctrl+C, il faut tout arrêter proprement.
int stop = 0; 

// --- OUTILS DE SYNCHRONISATION ---
sem_t places_libres;    // Compteur : Combien de places vides reste-t-il pour écrire ?
sem_t items_existants;  // Compteur : Combien de messages sont prêts à être lus ?

// Le Mutex est une "clé unique". 
// Il sert à protéger les variables i, j et tab pour qu'un seul thread y touche à la fois.
// PTHREAD_MUTEX_INITIALIZER crée le mutex en état "déverrouillé" au départ.
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 


// ============================================================================
// GESTIONNAIRE DE SIGNAL (L'interception du Ctrl+C)
// ============================================================================
// Cette fonction est appelée automatiquement par l'OS quand on fait Ctrl+C
void handler(int sig) {
    // 1. On note l'information : "Il faut s'arrêter"
    stop = 1; 
    
    // 2. ASTUCE CRITIQUE POUR L'AFFICHAGE :
    // Si le consommateur est bloqué en attente d'un item (sem_wait), il dort.
    // Si le producteur est bloqué en attente d'une place, il dort.
    // Pour qu'ils voient que 'stop' vaut 1, il faut les RÉVEILLER de force.
    
    // On simule l'ajout d'une place et d'un item. 
    // Cela débloque immédiatement les sem_wait dans les threads.
    sem_post(&places_libres);   
    sem_post(&items_existants);
}

// ============================================================================
// ROUTINE DU PRODUCTEUR (L'écrivain)
// ============================================================================
void * producteur(void * arg) {
    int k = 0; // Compteur local pour générer des messages différents

    // On boucle tant que le drapeau 'stop' est à 0 (Faux)
    while (!stop) { 
        Donnee item;
        // Préparation du message en local (hors de la zone partagée, pas besoin de protection)
        snprintf(item.texte, TAILLE_MSG, "ThreadMsg %d", k++);
        
        // --- ÉTAPE 1 : Attente d'une place libre ---
        // sem_wait décrémente le compteur. Si compteur == 0, le thread DORT ici.
        // Il se réveille si :
        //   a) Le consommateur libère une place (sem_post)
        //   b) Le handler simule une place (Ctrl+C)
        if (sem_wait(&places_libres) != 0) {
            // Si sem_wait a échoué (interruption), on vérifie si on doit arrêter
            if (stop) break; 
        }
        
        // Sécurité : Si on a été réveillé par le handler (Ctrl+C), on sort tout de suite
        if (stop) break; 

        // --- ÉTAPE 2 : Section Critique (Accès exclusif) ---
        pthread_mutex_lock(&mutex); // Je prends la clé (les autres attendent)

        // Écriture réelle en mémoire partagée
        tab[i] = item; 
        printf("-> Producteur : Ecrit '%s' index %d\n", item.texte, i);
        
        // Calcul circulaire de l'index : 0, 1, ..., 9, puis retour à 0
        i = (i + 1) % N;
        
        pthread_mutex_unlock(&mutex); // Je rends la clé
        // --- Fin Section Critique ---

        // --- ÉTAPE 3 : Signalement ---
        // On prévient le consommateur qu'il y a un nouveau message à lire
        sem_post(&items_existants);

        sleep(1); // On ralentit pour observer le résultat
    }
    
    // Ce message ne s'affiche que si on sort du while (donc si stop == 1)
    printf("--- Arrêt (SIGINT) : Fin du thread Producteur ---\n");
    pthread_exit(NULL); // Termine ce thread proprement
}

// ============================================================================
// ROUTINE DU CONSOMMATEUR (Le lecteur)
// ============================================================================
void * consommateur(void * arg) {
    while (!stop) {
        Donnee item;
        
        // --- ÉTAPE 1 : Attente de quelque chose à lire ---
        // Si items_existants == 0, on dort ici.
        if (sem_wait(&items_existants) != 0) {
            if (stop) break; // Interruption système
        }
        
        // Si on a été réveillé par le handler (et pas par le producteur), on sort.
        if (stop) break; 

        // --- ÉTAPE 2 : Section Critique ---
        pthread_mutex_lock(&mutex); // Je verrouille l'accès

        // Lecture depuis la mémoire partagée
        item = tab[j]; 
        printf("<- Consommateur : Lu '%s' index %d\n", item.texte, j);
        
        // Avancée circulaire de l'index de lecture
        j = (j + 1) % N;
        
        pthread_mutex_unlock(&mutex); // Je déverrouille
        // --- Fin Section Critique ---

        // --- ÉTAPE 3 : Signalement ---
        // On prévient le producteur qu'une case s'est libérée
        sem_post(&places_libres);

        sleep(1);
    }

    // Ce message s'affiche quand la boucle est brisée par le Ctrl+C
    printf("--- Arrêt (SIGINT) : Fin du thread Consommateur ---\n");
    pthread_exit(NULL);
}

// ============================================================================
// FONCTION PRINCIPALE (Le Chef d'orchestre)
// ============================================================================
int main() {
    // --- 1. Configuration du Signal (Le téléphone rouge) ---
    struct sigaction sa;
    sa.sa_handler = handler;  // Quelle fonction appeler ? -> handler
    sigemptyset(&sa.sa_mask); // On ne bloque pas d'autres signaux pendant l'exécution
    sa.sa_flags = 0;          // Pas d'options spéciales
    
    // On active l'écoute sur SIGINT (le signal envoyé par Ctrl+C)
    sigaction(SIGINT, &sa, NULL); 

    // Déclaration des identifiants des threads
    pthread_t th_prod, th_conso; 

    printf("--- Debut avec Threads (Faites Ctrl+C pour stopper et voir les messages) ---\n");

    // --- 2. Initialisation des Sémaphores ---
    // Argument 2 (0) : Partagé entre threads du même processus
    // Argument 3 : Valeur initiale
    sem_init(&places_libres, 0, N);   // Au début, 10 places sont vides
    sem_init(&items_existants, 0, 0); // Au début, 0 items à lire
    
    // --- 3. Lancement des Threads ---
    // pthread_create lance la fonction 'producteur' en parallèle
    if (pthread_create(&th_prod, NULL, producteur, NULL) != 0) { 
        perror("Erreur création producteur");
        exit(1);
    }

    if (pthread_create(&th_conso, NULL, consommateur, NULL) != 0) {
        perror("Erreur création consommateur");
        exit(1);
    }
    
    // --- 4. Attente (Le main se met en pause) ---
    // Le main reste bloqué sur ces lignes tant que les threads n'ont pas fini.
    // Comme ils sont dans une boucle infinie, le main ne bouge plus jusqu'au Ctrl+C.
    pthread_join(th_prod, NULL); 
    pthread_join(th_conso, NULL);

    // Si on arrive ici, c'est que les threads sont finis (grâce au Ctrl+C)
    printf("--- Fin du processus principal ---\n");

    // --- 5. Nettoyage ---
    // On détruit les outils de synchronisation pour libérer les ressources système
    sem_destroy(&places_libres);
    sem_destroy(&items_existants);
    pthread_mutex_destroy(&mutex); 

    return 0;
}