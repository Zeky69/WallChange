#include "client/network.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#define VERSION "1.0.26"

static int interrupted = 0;

void signal_handler(int sig) {
    interrupted = 1;
}

// Affiche l'aide
static void print_help(const char *prog_name) {
    printf("\n");
    printf("  \033[1;36mCOMMANDS:\033[0m\n");
    printf("    \033[1;32mlist\033[0m                           Lister les clients connectés\n");
    printf("    \033[1;32msend\033[0m <user> <image|url>        Envoyer une image à un client\n");
    printf("    \033[1;32mupdate\033[0m <user>                  Mettre à jour un client distant\n");
    printf("    \033[1;32muninstall\033[0m                      Désinstaller\n");
    printf("    \033[1;32mkey\033[0m <user> <combo>             Envoyer un raccourci clavier\n");
    printf("    \033[1;32mreverse\033[0m <user>                 Inverser l'écran pendant 3s\n\n");
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
    // Mode commande : raccourci clavier personnalisé
    if (cmd_idx > 0 && cmd_idx + 2 <= argc - 1 && strcmp(argv[cmd_idx], "key") == 0) {
        return send_key_command(argv[cmd_idx + 1], argv[cmd_idx + 2]);
    }
    // Mode commande : reverse screen
    if (cmd_idx > 0 && cmd_idx + 1 <= argc - 1 && strcmp(argv[cmd_idx], "reverse") == 0) {
        return send_reverse_command(argv[cmd_idx + 1]);
    }
    // Mode commande : lister les clients
    if (cmd_idx > 0 && strcmp(argv[cmd_idx], "list") == 0) {
        return send_list_command();
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
