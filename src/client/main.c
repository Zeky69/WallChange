#include "client/network.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef VERSION
#define VERSION "0.0.0"
#endif

static int interrupted = 0;

void signal_handler(int sig) {
    interrupted = 1;
}

// Affiche l'aide
static void print_help(const char *prog_name) {
    printf("\n");
    printf("  \033[1;36mCOMMANDS:\033[0m\n");
    printf("    \033[1;32mlogin\033[0m <user> <pass>            Se connecter en admin\n");
    printf("    \033[1;32mlist\033[0m                           Lister les clients connectés\n");
    printf("    \033[1;32msend\033[0m <user> <image|url>        Envoyer une image à un client\n");
    printf("    \033[1;32mupdate\033[0m <user>                  Mettre à jour un client distant\n");
    printf("    \033[1;32muninstall\033[0m [user]               Désinstaller (soi-même ou autre si admin)\n");
    printf("    \033[1;32mreinstall\033[0m <user>               Réinstaller complètement un client\n");
    printf("    \033[1;32mlogs\033[0m <user>                    Voir les logs en direct d'un client\n");
    printf("    \033[1;32mkey\033[0m <user> <combo>             Envoyer un raccourci clavier\n");
    printf("    \033[1;32mreverse\033[0m <user>                 Inverser l'écran pendant 3s\n");
    printf("    \033[1;32mmarquee\033[0m <user> <url>           Faire défiler une image\n");
    printf("    \033[1;32mparticles\033[0m <user> <url>         Particules autour de la souris (5s)\n");
    printf("    \033[1;32mclones\033[0m <user>                  100 clones de souris (5s)\n");
    printf("    \033[1;32mdrunk\033[0m <user>                   Rend la souris ivre (10s)\n\n");
    printf("  \033[1;36mOPTIONS:\033[0m\n");
    printf("    \033[1;33m-l, --local\033[0m                    Mode local (localhost:8000)\n");
    printf("    \033[1;33m-h, --help\033[0m                     Afficher cette aide\n");
    printf("    \033[1;33m-v, --version\033[0m                  Afficher la version\n\n");
}

// Vérifie si un argument est l'option --local ou -l
static int check_local_flag(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--local") == 0 || strcmp(argv[i], "-l") == 0) {
            return 1;
        }
    }
    return 0;
}

// Vérifie si un argument est --help ou -h
static int check_help_flag(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "help") == 0) {
            return 1;
        }
    }
    return 0;
}

// Vérifie si un argument est --version ou -v
static int check_version_flag(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            return 1;
        }
    }
    return 0;
}

// Retourne l'index du premier argument non-flag
static int get_command_index(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--local") != 0 && strcmp(argv[i], "-l") != 0 &&
            strcmp(argv[i], "--help") != 0 && strcmp(argv[i], "-h") != 0 &&
            strcmp(argv[i], "--version") != 0 && strcmp(argv[i], "-v") != 0) {
            return i;
        }
    }
    return -1;
}

int main(int argc, char **argv) {
    // Vérifier --help
    if (check_help_flag(argc, argv)) {
        print_help(argv[0]);
        return 0;
    }
    
    // Vérifier --version
    if (check_version_flag(argc, argv)) {
        printf("WallChange version %s\n", VERSION);
        return 0;
    }

    // Vérifier le mode local
    int is_local = check_local_flag(argc, argv);
    if (is_local) {
        set_local_mode(1);
    }

    // S'assurer qu'on est dans un répertoire valide au démarrage
    // pour éviter les problèmes de getcwd() si le dossier courant a été supprimé
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        const char *home = getenv("HOME");
        if (home) {
            if (chdir(home) == 0) {
                setenv("PWD", home, 1);
            }
        } else {
            if (chdir("/tmp") == 0) {
                setenv("PWD", "/tmp", 1);
            }
        }
    }

    // Charger le token d'authentification depuis l'environnement
    const char *token = getenv("WALLCHANGE_TOKEN");
    if (token) {
        set_admin_token(token);
    } else {
        // Essayer de charger depuis le fichier .wallchange_token
        char token_path[1024];
        const char *home = getenv("HOME");
        if (home) {
            snprintf(token_path, sizeof(token_path), "%s/.wallchange_token", home);
            FILE *fp = fopen(token_path, "r");
            if (fp) {
                char file_token[128];
                if (fgets(file_token, sizeof(file_token), fp)) {
                    // Supprimer le saut de ligne
                    size_t len = strlen(file_token);
                    if (len > 0 && file_token[len-1] == '\n') file_token[len-1] = '\0';
                    set_admin_token(file_token);
                }
                fclose(fp);
            }
        }
    }

    // Trouver l'index de la commande (après les flags)
    int cmd_idx = get_command_index(argc, argv);
    
    // Mode commande : envoi d'image
    if (cmd_idx > 0 && cmd_idx + 2 <= argc - 1 && strcmp(argv[cmd_idx], "send") == 0) {
        return send_command(argv[cmd_idx + 1], argv[cmd_idx + 2]);
    }
    // Mode commande : mise à jour d'un client
    if (cmd_idx > 0 && cmd_idx + 1 <= argc - 1 && strcmp(argv[cmd_idx], "update") == 0) {
        return send_update_command(argv[cmd_idx + 1]);
    }
    // Mode commande : désinstaller
    if (cmd_idx > 0 && strcmp(argv[cmd_idx], "uninstall") == 0) {
        // Si un argument est fourni, l'utiliser, sinon utiliser l'utilisateur courant
        const char *target_user = NULL;
        if (cmd_idx + 1 <= argc - 1) {
            target_user = argv[cmd_idx + 1];
        }
        return send_uninstall_command(target_user);
    }
    // Mode commande : réinstaller
    if (cmd_idx > 0 && cmd_idx + 1 <= argc - 1 && strcmp(argv[cmd_idx], "reinstall") == 0) {
        return send_reinstall_command(argv[cmd_idx + 1]);
    }
    // Mode commande : logs en direct
    if (cmd_idx > 0 && cmd_idx + 1 <= argc - 1 && strcmp(argv[cmd_idx], "logs") == 0) {
        return watch_logs(argv[cmd_idx + 1]);
    }
    // Mode commande : raccourci clavier personnalisé
    if (cmd_idx > 0 && cmd_idx + 2 <= argc - 1 && strcmp(argv[cmd_idx], "key") == 0) {
        return send_key_command(argv[cmd_idx + 1], argv[cmd_idx + 2]);
    }
    // Mode commande : reverse screen
    if (cmd_idx > 0 && cmd_idx + 1 <= argc - 1 && strcmp(argv[cmd_idx], "reverse") == 0) {
        return send_reverse_command(argv[cmd_idx + 1]);
    }
    // Mode commande : marquee
    if (cmd_idx > 0 && cmd_idx + 2 <= argc - 1 && strcmp(argv[cmd_idx], "marquee") == 0) {
        return send_marquee_command(argv[cmd_idx + 1], argv[cmd_idx + 2]);
    }
    // Mode commande : particles
    if (cmd_idx > 0 && cmd_idx + 2 <= argc - 1 && strcmp(argv[cmd_idx], "particles") == 0) {
        return send_particles_command(argv[cmd_idx + 1], argv[cmd_idx + 2]);
    }
    // Mode commande : clones
    if (cmd_idx > 0 && cmd_idx + 1 <= argc - 1 && strcmp(argv[cmd_idx], "clones") == 0) {
        return send_clones_command(argv[cmd_idx + 1]);
    }
    // Mode commande : drunk
    if (cmd_idx > 0 && cmd_idx + 1 <= argc - 1 && strcmp(argv[cmd_idx], "drunk") == 0) {
        return send_drunk_command(argv[cmd_idx + 1]);
    }
    // Mode commande : lister les clients
    if (cmd_idx > 0 && strcmp(argv[cmd_idx], "list") == 0) {
        return send_list_command();
    }
    // Mode commande : login admin
    if (cmd_idx > 0 && cmd_idx + 2 <= argc - 1 && strcmp(argv[cmd_idx], "login") == 0) {
        return send_login_command(argv[cmd_idx + 1], argv[cmd_idx + 2]);
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    init_network();
    
    // Vérifier la version avant de se connecter
    check_and_update_version(VERSION);
    
    // Premier essai
    connect_ws();

    printf("Client démarré. Appuyez sur Ctrl+C pour quitter !\n");

    while (!interrupted) {
        network_poll(100);
    }

    cleanup_network();
    return 0;
}
