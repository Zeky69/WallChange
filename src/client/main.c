#include "client/network.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>

static int interrupted = 0;

void signal_handler(int sig) {
    interrupted = 1;
}

int main(int argc, char **argv) {
    // Mode commande : envoi d'image
    if (argc >= 4 && strcmp(argv[1], "send") == 0) {
        return send_command(argv[2], argv[3]);
    }
    // Mode commande : mise à jour d'un client
    if (argc >= 3 && strcmp(argv[1], "update") == 0) {
        return send_update_command(argv[2]);
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    init_network();
    
    // Premier essai
    connect_ws();

    printf("Client démarré. Appuyez sur Ctrl+C pour quitter.\n");

    while (!interrupted) {
        network_poll(100);
    }

    cleanup_network();
    return 0;
}
