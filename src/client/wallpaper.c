#include "client/wallpaper.h"
#include "common/image_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void run_cmd(const char *cmd) {
    int ret = system(cmd);
    (void)ret;
}

int download_image(const char *url, const char *filepath) {
    char command[2048];
    // -s: silencieux, -L: suivre les redirections, -o: fichier de sortie
    snprintf(command, sizeof(command), "curl -s -L -o '%s' '%s'", filepath, url);
    printf("Exécution: %s\n", command);
    int ret = system(command);
    
    if (ret == 0) {
        // Vérifier si c'est une image valide après téléchargement
        if (!is_valid_image(filepath)) {
            fprintf(stderr, "Erreur: Le fichier téléchargé n'est pas une image valide.\n");
            remove(filepath);
            return 0;
        }
        return 1;
    }
    return 0;
}

void set_wallpaper(const char *filepath) {
    // Vérification préalable
    if (!is_valid_image(filepath)) {
        fprintf(stderr, "Erreur: Fichier image invalide ou corrompu: %s\n", filepath);
        return;
    }

    char command[2048];
    
    // Détection de l'environnement de bureau pour adapter la commande
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    if (!desktop) desktop = "unknown";
    
    printf("Environnement détecté: %s\n", desktop);

    if (strstr(desktop, "GNOME") || strstr(desktop, "Ubuntu")) {
        // Pour GNOME / Ubuntu
        snprintf(command, sizeof(command), "gsettings set org.gnome.desktop.background picture-uri 'file://%s'", filepath);
        run_cmd(command);
        snprintf(command, sizeof(command), "gsettings set org.gnome.desktop.background picture-uri-dark 'file://%s'", filepath);
        run_cmd(command);
    } 
    else if (strstr(desktop, "XFCE")) {
        // Pour XFCE (nécessite xfconf-query)
        // Note: Il faut souvent trouver le bon moniteur/workspace, c'est une commande générique
        snprintf(command, sizeof(command), "xfconf-query -c xfce4-desktop -p /backdrop/screen0/monitor0/workspace0/last-image -s '%s'", filepath);
        run_cmd(command);
    }
    else if (strstr(desktop, "KDE")) {
        // KDE Plasma est plus complexe via DBus, souvent on utilise qdbus ou plasma-apply-wallpaperimage
        snprintf(command, sizeof(command), "plasma-apply-wallpaperimage '%s'", filepath);
        run_cmd(command);
    }
    else {
        // Fallback générique (feh, nitrogen, etc.)
        // On essaie feh car c'est très commun sur les WM légers (i3, bspwm, etc.)
        snprintf(command, sizeof(command), "feh --bg-fill '%s'", filepath);
        if (system(command) != 0) {
            // Si feh échoue, on essaie nitrogen
            snprintf(command, sizeof(command), "nitrogen --set-zoom-fill '%s' --save", filepath);
            run_cmd(command);
        }
    }
    
    printf("Tentative de changement de fond d'écran terminée.\n");
}
