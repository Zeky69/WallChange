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

void perform_uninstall() {
    printf("Désinstallation de WallChange...\n");
    
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "Erreur: Impossible de récupérer HOME.\n");
        exit(1);
    }

    // 1. Supprimer le dossier source ~/.wallchange_source
    char source_dir[PATH_MAX];
    snprintf(source_dir, sizeof(source_dir), "%s/.wallchange_source", home);
    if (access(source_dir, F_OK) == 0) {
        printf("Suppression du dossier source: %s\n", source_dir);
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", source_dir);
        int ret = system(cmd);
        (void)ret; // Ignorer le warning
    }

    // 2. Supprimer le fichier autostart
    char autostart_file[PATH_MAX + 64];
    snprintf(autostart_file, sizeof(autostart_file), "%s/.config/autostart/wallchange.desktop", home);
    if (access(autostart_file, F_OK) == 0) {
        printf("Suppression du fichier autostart: %s\n", autostart_file);
        unlink(autostart_file);
    }

    // 3. Supprimer les binaires dans ~/.local/bin
    char wallchange_bin[PATH_MAX + 32];
    snprintf(wallchange_bin, sizeof(wallchange_bin), "%s/.local/bin/wallchange", home);
    if (access(wallchange_bin, F_OK) == 0) {
        printf("Suppression du binaire: %s\n", wallchange_bin);
        unlink(wallchange_bin);
    }
    
    char server_bin[PATH_MAX + 32];
    snprintf(server_bin, sizeof(server_bin), "%s/.local/bin/server", home);
    if (access(server_bin, F_OK) == 0) {
        printf("Suppression du binaire: %s\n", server_bin);
        unlink(server_bin);
    }

    // 4. Supprimer l'exécutable actuel (si différent)
    char current_exe[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", current_exe, sizeof(current_exe) - 1);
    if (len != -1) {
        current_exe[len] = '\0';
        // Ne pas supprimer si c'est le même que wallchange_bin
        if (strcmp(current_exe, wallchange_bin) != 0) {
            printf("Suppression de l'exécutable actuel: %s\n", current_exe);
            unlink(current_exe);
        }
    }

    // 5. Supprimer les alias dans ~/.zshrc et ~/.bashrc
    char zshrc[PATH_MAX + 16];
    char bashrc[PATH_MAX + 16];
    snprintf(zshrc, sizeof(zshrc), "%s/.zshrc", home);
    snprintf(bashrc, sizeof(bashrc), "%s/.bashrc", home);

    // Fonction helper pour supprimer les lignes d'alias
    const char *files[] = {zshrc, bashrc, NULL};
    for (int i = 0; files[i] != NULL; i++) {
        if (access(files[i], F_OK) == 0) {
            printf("Suppression des alias dans: %s\n", files[i]);
            char cmd[PATH_MAX + 256];
            // Supprimer les lignes contenant les alias wallchange
            snprintf(cmd, sizeof(cmd), 
                     "sed -i '/# Alias Wallchange/d; /alias wallchange=/d; /alias wallserver=/d' '%s'",
                     files[i]);
            int ret = system(cmd);
            (void)ret;
        }
    }

    printf("\n=== Désinstallation terminée ===\n");
    printf("Au revoir !\n");
    exit(0);
}
