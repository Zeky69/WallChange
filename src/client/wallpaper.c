#include "client/wallpaper.h"
#include <stdio.h>
#include <stdlib.h>

int download_image(const char *url, const char *filepath) {
    char command[2048];
    // -s: silencieux, -L: suivre les redirections, -o: fichier de sortie
    // On met des quotes autour des chemins pour gérer les espaces
    snprintf(command, sizeof(command), "curl -s -L -o '%s' '%s'", filepath, url);
    printf("Exécution: %s\n", command);
    int ret = system(command);
    return (ret == 0);
}

void set_wallpaper(const char *filepath) {
    char command[2048];
    // Pour GNOME / Ubuntu
    snprintf(command, sizeof(command), "gsettings set org.gnome.desktop.background picture-uri 'file://%s'", filepath);
    printf("Changement fond d'écran: %s\n", command);
    if (system(command) != 0) fprintf(stderr, "Erreur lors de l'exécution de la commande\n");
    
    // Pour le mode sombre
    snprintf(command, sizeof(command), "gsettings set org.gnome.desktop.background picture-uri-dark 'file://%s'", filepath);
    if (system(command) != 0) fprintf(stderr, "Erreur lors de l'exécution de la commande (dark mode)\n");
}
