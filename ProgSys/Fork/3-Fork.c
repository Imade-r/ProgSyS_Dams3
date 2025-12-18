#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h> // Pour la gestion des signaux
#include <errno.h>  // Pour capturer les interruptions (EINTR)

#define N 10 
#define TAILLE_MSG 64

// --- GLOBAL ---
// Variable globale qui sert de "drapeau". 
// 0 = Tout va bien, on continue.
// 1 = On a reçu Ctrl+C, il faut s'arrêter.
int stop = 0; 

// Fonction appelée automatiquement quand on fait Ctrl+C
void handler_signal(int sig) {
    stop = 1; // On lève le drapeau : "C'est l'heure d'arrêter !"
}


// --- STRUCTURE DONNÉES ---
// On "emballe" le tableau de char dans une structure pour pouvoir 
// le copier facilement avec un simple signe "=" (astuce C).

typedef struct {
    char texte[TAILLE_MSG];
} Donnee;


// --- MÉMOIRE PARTAGÉE ---
// Cette structure représente tout ce que le Père et le Fils vont partager.
typedef struct {
    Donnee tab[N];      // Le tableau circulaire de données
    int i;              // Où le Producteur écrit
    int j;              // Où le Consommateur lit
    sem_t places_libres;    // Sém. pour savoir si on peut écrire
    sem_t items_existants;  // Sém. pour savoir si on peut lire
    sem_t mutex;            // Sém. pour protéger les variables i et j
} MemoirePartagee;



int main() {
    // === 1. CONFIGURATION DU SIGNAL (Slide 127) ===
    // On prépare la structure pour dire au système : 
    // "Si tu reçois SIGINT (Ctrl+C), lance la fonction 'handler_signal'"

    struct sigaction sa; 
    sa.sa_handler = handler_signal;  //Intitulé de la case vide sur le papier, marquée : "Qui doit gérer le problème ?" (ca vient de la struct sigaction -> <signal.h>)
    
    // On applique la configuration
    sigaction(SIGINT, &sa, NULL);   // Si on fait CTRL + C --> handler_signal --> stop = 1;

    // === 2. CRÉATION MÉMOIRE PARTAGÉE (Slide 89) ===
    // mmap avec MAP_ANONYMOUS permet de créer de la RAM partagée sans fichier.
    // PROT_READ | PROT_WRITE : On veut lire et écrire dedans.
    // MAP_SHARED : Les modifications du fils seront vues par le père.
    MemoirePartagee* partage = mmap(NULL, sizeof(MemoirePartagee), 
                                    PROT_READ | PROT_WRITE, 
                                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    // Initialisation des indices à 0
    partage->i = 0; 
    partage->j = 0;

    // === 3. INITIALISATION SÉMAPHORES (Slide 126) ===
    // pshared = 1 car partagé entre processus (Père/Fils).
    sem_init(&partage->places_libres, 1, N); // Au début, N places vides
    sem_init(&partage->items_existants, 1, 0); // Au début, 0 message à lire
    sem_init(&partage->mutex, 1, 1); // Mutex ouvert (1 clé disponible)

    // === 4. DUPLICATION DU PROCESSUS (Slide 38) ===
    pid_t pid = fork();

    // --- CODE DU FILS (CONSOMMATEUR) ---
    if (pid == 0) {
        while (stop==0) { 
            Donnee item_recu;
            
            // On attend qu'il y ait un item (P sur items_existants)
            // Si on fait Ctrl+C pendant l'attente, sem_wait renvoie -1
            if (sem_wait(&partage->items_existants) == -1) {
                // On vérifie si c'est à cause du signal
                if (errno == EINTR) { //Si errno vaut EINTR , cela signifie:"Je n'ai pas eu de problème technique, j'ai été interrompu par un signal (comme Ctrl+C)."
                    // Oui, c'est le signal, donc on sort de la boucle while
                    continue; // Le continue nous renvoie au while (!stop).
                }
            }

            // Si on a reçu le signal stop juste après le wait, on sort
            if (stop) break;

            // -- DÉBUT SECTION CRITIQUE --
            sem_wait(&partage->mutex); // Je verrouille l'accès aux index

            // Je copie la donnée depuis la mémoire partagée
            item_recu = partage->tab[partage->j];
            
            // J'avance mon index de lecture (circulaire)
            partage->j = (partage->j + 1) % N;

            sem_post(&partage->mutex); // Je déverrouille
            // -- FIN SECTION CRITIQUE --

            // Je signale qu'une place s'est libérée (V sur places_libres)
            sem_post(&partage->places_libres);

            // J'affiche mon message personnalisé
            printf("   [Fils] J'ai lu : '%s'\n", item_recu.texte);
            
            sleep(1); // Pause pour voir ce qui se passe
        }
        
        // Si on sort du while, c'est qu'on a fait Ctrl+C
        printf("\n -> [Fils] J'ai reçu l'ordre d'arrêt. Je termine.\n");
        exit(0); 
    } 
    
    // --- CODE DU PÈRE (PRODUCTEUR) ---
    else {
        int k = 0;
        while (stop==0) {
            Donnee item_a_envoyer;
            // Je prépare mon message dans ma variable locale
            snprintf(item_a_envoyer.texte, TAILLE_MSG, "Message n°%d", k++);
            
            // J'attends une place libre. Gestion du Ctrl+C ici aussi.
            if (sem_wait(&partage->places_libres) == -1) {
                if (errno == EINTR) continue; // Interruption signal -> on re-test le while
            }

            if (stop) break;

            // -- DÉBUT SECTION CRITIQUE --
            sem_wait(&partage->mutex); // Verrouillage

            // Copie de ma structure locale vers la mémoire partagée
            partage->tab[partage->i] = item_a_envoyer;
            
            // Avance l'index écriture
            partage->i = (partage->i + 1) % N;

            sem_post(&partage->mutex); // Déverrouillage
            // -- FIN SECTION CRITIQUE --

            // Je signale qu'un item est dispo
            sem_post(&partage->items_existants); 
            
            printf("[Père] J'ai écrit : '%s'\n", item_a_envoyer.texte);
            sleep(1);
        }

        // Gestion de la fin propre
        printf("\n -> [Père] Arrêt demandé. J'attends que mon fils finisse.\n");
        wait(NULL); // J'attends que le fils soit vraiment parti
        
        // --- NETTOYAGE (Slide 127) ---
        printf("[Père] Destruction des sémaphores et mémoire.\n");
        sem_destroy(&partage->places_libres);
        sem_destroy(&partage->items_existants);
        sem_destroy(&partage->mutex);
        munmap(partage, sizeof(MemoirePartagee));
        
        printf("[Père] Fin du programme.\n");
    }
    return 0;
}
