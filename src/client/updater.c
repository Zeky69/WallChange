#include "client/updater.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

void perform_update() {
    printf("Mise à jour demandée...\n");
    
    // 1. Déterminer le dossier source
    char source_dir[PATH_MAX];
    const char *home = getenv("HOME");
    int source_found = 0;

    if (home) {
        snprintf(source_dir, sizeof(source_dir), "%s/.wallchange_source", home);
        if (access(source_dir, F_OK) == 0) {
            source_found = 1;
        }
    }

    // Fallback: dossier courant si Makefile présent
    if (!source_found && access("Makefile", F_OK) == 0) {
        if (getcwd(source_dir, sizeof(source_dir)) != NULL) {
            source_found = 1;
            printf("Utilisation du dossier courant comme source: %s\n", source_dir);
        }
    }

    if (!source_found) {
        fprintf(stderr, "Erreur: Impossible de localiser le dossier source (.wallchange_source ou dossier courant).\n");
        return;
    }

    // 2. Sauvegarder le chemin de l'exécutable actuel
    char current_exe[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", current_exe, sizeof(current_exe) - 1);
    if (len == -1) {
        perror("readlink failed");
        return;
    }
    current_exe[len] = '\0';

    // 3. Se déplacer dans le dossier source
    printf("Changement de répertoire vers %s\n", source_dir);
    if (chdir(source_dir) != 0) {
        perror("chdir failed");
        return;
    }
    
    // 4. Git pull
    printf("Exécution de git pull...\n");
    if (system("git pull") != 0) {
        printf("Attention: git pull a échoué ou n'est pas nécessaire.\n");
    }
    
    // 5. Recompile
    printf("Recompilation...\n");
    if (system("make re") != 0) {
        printf("Erreur lors de la compilation.\n");
        return;
    }
    
    // 6. Copier le nouveau binaire si nécessaire
    char new_binary[PATH_MAX + 16];
    snprintf(new_binary, sizeof(new_binary), "%s/wallchange", source_dir);
    
    // On compare les chemins
    char *real_new = realpath(new_binary, NULL);
    char *real_current = realpath(current_exe, NULL);

    if (real_new && real_current && strcmp(real_new, real_current) != 0) {
        printf("Mise à jour du binaire installé: %s -> %s\n", new_binary, current_exe);
        
        // Suppression de l'ancien pour éviter "Text file busy"
        unlink(current_exe);
        
        size_t cmd_len = strlen(new_binary) + strlen(current_exe) + 64;
        char *cp_cmd = malloc(cmd_len);
        if (cp_cmd) {
            snprintf(cp_cmd, cmd_len, "cp '%s' '%s'", new_binary, current_exe);
            if (system(cp_cmd) != 0) {
                fprintf(stderr, "Erreur lors de la copie du binaire.\n");
                free(cp_cmd);
                if (real_new) free(real_new);
                if (real_current) free(real_current);
                return;
            }
            free(cp_cmd);
        }
    } else {
        printf("Le binaire s'exécute déjà depuis la source ou chemins identiques.\n");
    }
    
    if (real_new) free(real_new);
    if (real_current) free(real_current);
    
    // 7. Restart
    printf("Redémarrage du client...\n");
    
    char *args[] = {current_exe, NULL};
    execv(current_exe, args);
    
    // Si execv échoue
    perror("execv failed");
}
