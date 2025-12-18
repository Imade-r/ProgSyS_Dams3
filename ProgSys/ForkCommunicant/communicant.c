#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>     // Pour write, close
#include <string.h>     // Pour strlen, strcmp
#include <fcntl.h>      // Pour open, O_WRONLY
#include "common.h"   // Pour récupérer les noms FIFO_PROD et FIFO_CONSO

#define CMD_SIZE 128

// Fonction utilitaire pour envoyer un message dans un tube nommé
void envoyer(const char* chemin_fifo, const char* message) {
    // 1. OUVERTURE DU TUBE (Appel Système open)
    // O_WRONLY : On ouvre le fichier tube en écriture seule.
    //
    // NOTE TECHNIQUE CRITIQUE :
    // Par défaut, open() sur un FIFO est BLOQUANT si personne n'a ouvert
    // le tube en lecture de l'autre côté.
    // Si le Producteur ou le Consommateur n'est pas lancé, le communicant
    // restera bloqué ici indéfiniment (sauf si on ajoutait O_NONBLOCK).
    int fd = open(chemin_fifo, O_WRONLY);
    
    if (fd == -1) {
        printf("   [Erreur] Impossible d'ouvrir %s. Le destinataire est-il lancé ?\n", chemin_fifo);
        return;
    }

    // 2. ÉCRITURE DU MESSAGE (Appel Système write)
    // On écrit la chaîne de caractères + le caractère nul de fin (\0).
    // Cela permet au lecteur de recevoir une chaîne C valide directement.
    write(fd, message, strlen(message) + 1); 
    
    printf("   -> Envoyé à %s : '%s'\n", chemin_fifo, message);
    
    // 3. FERMETURE DU DESCRIPTEUR
    // On ferme l'accès au fichier. Cela envoie un EOF (End Of File) au lecteur
    // si nous étions le seul écrivain, mais ici c'est juste un nettoyage.
    close(fd);
}

int main() {
    char buffer[CMD_SIZE];

    printf("=== COMMUNICANT (Télécommande) ===\n");
    printf("  p [msg] : Envoyer un message au Producteur\n");
    printf("  c [msg] : Envoyer un message au Consommateur\n");
    printf("  p stop  : Arrêter le Producteur\n");
    printf("  c stop  : Arrêter le Consommateur\n");
    printf("  q       : Quitter\n");
    printf("====================================\n");

    while (1) {
        printf("> ");
        // Lecture bloquante de l'entrée standard (Clavier)
        if (fgets(buffer, CMD_SIZE, stdin) == NULL) break;
        
        // Nettoyage : suppression du saut de ligne (\n) lu par fgets
        buffer[strcspn(buffer, "\n")] = 0;

        if (strcmp(buffer, "q") == 0) break;
        if (strlen(buffer) == 0) continue;

        // Analyse de la commande
        if (strncmp(buffer, "p ", 2) == 0) {
            // Envoie au Producteur tout ce qui suit "p "
            envoyer(FIFO_PROD, buffer + 2); 
        }
        else if (strncmp(buffer, "c ", 2) == 0) {
            // Envoie au Consommateur tout ce qui suit "c "
            envoyer(FIFO_CONSO, buffer + 2); 
        }
        else {
            printf("Commande inconnue. Syntaxe : 'p message' ou 'c message'\n");
        }
    }
    return 0;
}