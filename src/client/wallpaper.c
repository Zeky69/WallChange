#include "client/wallpaper.h"
#include "common/image_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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

// Redimensionne et compresse une image si elle est trop grande
// Retourne 1 si le fichier a été modifié, 0 sinon
static int auto_resize_if_needed(const char *filepath) {
    #define MAX_WALLPAPER_WIDTH 3840
    #define MAX_WALLPAPER_HEIGHT 2160
    #define MAX_FILE_SIZE (10 * 1024 * 1024)  // 10 MB

    // Vérifier d'abord la taille du fichier
    FILE *f = fopen(filepath, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fclose(f);

    int w, h, channels;
    unsigned char *data = load_image(filepath, &w, &h, &channels);
    if (!data) return 0;

    int need_resize = (w > MAX_WALLPAPER_WIDTH || h > MAX_WALLPAPER_HEIGHT);
    int need_recompress = (file_size > MAX_FILE_SIZE);

    if (!need_resize && !need_recompress) {
        free_image(data);
        return 0;
    }

    int new_w = w;
    int new_h = h;

    if (need_resize) {
        // Calculer les nouvelles dimensions en gardant le ratio
        float ratio_w = (float)MAX_WALLPAPER_WIDTH / w;
        float ratio_h = (float)MAX_WALLPAPER_HEIGHT / h;
        float ratio = ratio_w < ratio_h ? ratio_w : ratio_h;
        new_w = (int)(w * ratio);
        new_h = (int)(h * ratio);
        printf("Image trop grande (%dx%d), redimensionnement à %dx%d...\n", w, h, new_w, new_h);

        unsigned char *resized = resize_image(data, w, h, channels, new_w, new_h);
        free_image(data);
        if (!resized) {
            fprintf(stderr, "Erreur: Échec du redimensionnement.\n");
            return 0;
        }
        data = resized;
    } else {
        printf("Image trop lourde (%ld octets), recompression...\n", file_size);
    }

    // Sauvegarder en JPG qualité 85 (bon compromis taille/qualité)
    char temp_path[2048];
    snprintf(temp_path, sizeof(temp_path), "%s.resized.jpg", filepath);
    if (stbi_write_jpg(temp_path, new_w, new_h, channels, data, 85)) {
        if (need_resize) {
            free(data);  // resize_image uses malloc
        } else {
            free_image(data);
        }
        if (rename(temp_path, filepath) != 0) {
            char cmd[4096];
            snprintf(cmd, sizeof(cmd), "mv '%s' '%s'", temp_path, filepath);
            system(cmd);
        }
        printf("Image optimisée avec succès.\n");
        return 1;
    }

    if (need_resize) {
        free(data);
    } else {
        free_image(data);
    }
    return 0;
}

void set_wallpaper(const char *filepath) {
    // Vérification préalable
    if (!is_valid_image(filepath)) {
        fprintf(stderr, "Erreur: Fichier image invalide ou corrompu: %s\n", filepath);
        return;
    }

    // Auto-resize si l'image est trop grande
    auto_resize_if_needed(filepath);

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

void apply_wallpaper_effect(const char *filepath, const char *effect, int value) {
    if (!effect || strlen(effect) == 0) return;

    printf("Application de l'effet '%s' (valeur: %d) sur %s...\n", effect, value, filepath);

    int w, h, channels;
    unsigned char *data = load_image(filepath, &w, &h, &channels);
    if (!data) {
        fprintf(stderr, "Erreur: Impossible de charger l'image pour l'effet.\n");
        return;
    }

    int applied = 0;
    if (strcmp(effect, "invert") == 0) {
        apply_invert(data, w, h, channels);
        applied = 1;
    }

    if (applied) {
        char temp_path[2048];
        snprintf(temp_path, sizeof(temp_path), "%s.tmp.jpg", filepath);
        
        // Save quality 95
        if (stbi_write_jpg(temp_path, w, h, channels, data, 95)) {
            if (rename(temp_path, filepath) != 0) {
                // Si rename échoue (cross-device), mv simple
                char cmd[2048 * 2];
                snprintf(cmd, sizeof(cmd), "mv '%s' '%s'", temp_path, filepath);
                run_cmd(cmd);
            }
            printf("Effet appliqué avec succès.\n");
        } else {
            fprintf(stderr, "Erreur lors de la sauvegarde de l'image modifiée.\n");
        }
    } else {
        printf("Aucun effet appliqué (paramètres incorrects ou non reconnu).\n");
    }

    free_image(data);
}
