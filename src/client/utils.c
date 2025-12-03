#include "client/utils.h"
#include "client/wallpaper.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

#define STB_IMAGE_IMPLEMENTATION
#include "common/stb_image.h"

// Structure pour stocker les données d'un GIF animé
typedef struct {
    unsigned char **frames;     // Tableau de frames (RGBA)
    int *delays;                // Délai entre chaque frame (en ms)
    int frame_count;            // Nombre de frames
    int width;
    int height;
} AnimatedGif;

char *get_username() {
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        return strdup(pw->pw_name);
    }
    return strdup(getenv("USER"));
}

char *get_hostname() {
    static char hostname[256] = {0};
    if (hostname[0] == '\0') {
        if (gethostname(hostname, sizeof(hostname) - 1) != 0) {
            strcpy(hostname, "unknown");
        }
    }
    return hostname;
}

char *get_os_info() {
    static char os_info[128] = {0};
    FILE *fp = fopen("/etc/os-release", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                // Extraire la valeur entre guillemets
                char *start = strchr(line, '"');
                if (start) {
                    start++;
                    char *end = strchr(start, '"');
                    if (end) {
                        *end = '\0';
                        strncpy(os_info, start, sizeof(os_info) - 1);
                    }
                }
                break;
            }
        }
        fclose(fp);
    }
    if (os_info[0] == '\0') {
        strcpy(os_info, "Linux");
    }
    return os_info;
}

char *get_uptime() {
    static char uptime_str[64] = {0};
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        long uptime = si.uptime;
        int days = uptime / 86400;
        int hours = (uptime % 86400) / 3600;
        int mins = (uptime % 3600) / 60;
        
        if (days > 0) {
            snprintf(uptime_str, sizeof(uptime_str), "%dj %dh %dm", days, hours, mins);
        } else if (hours > 0) {
            snprintf(uptime_str, sizeof(uptime_str), "%dh %dm", hours, mins);
        } else {
            snprintf(uptime_str, sizeof(uptime_str), "%dm", mins);
        }
    } else {
        strcpy(uptime_str, "N/A");
    }
    return uptime_str;
}

char *get_cpu_load() {
    static char cpu_str[32] = {0};
    double loadavg[3];
    if (getloadavg(loadavg, 3) != -1) {
        snprintf(cpu_str, sizeof(cpu_str), "%.2f, %.2f, %.2f", 
                 loadavg[0], loadavg[1], loadavg[2]);
    } else {
        strcpy(cpu_str, "N/A");
    }
    return cpu_str;
}

char *get_ram_usage() {
    static char ram_str[32] = {0};
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        unsigned long total_mb = si.totalram / (1024 * 1024);
        unsigned long free_mb = si.freeram / (1024 * 1024);
        unsigned long used_mb = total_mb - free_mb;
        int percent = (int)((used_mb * 100) / total_mb);
        snprintf(ram_str, sizeof(ram_str), "%lu/%luMB (%d%%)", 
                 used_mb, total_mb, percent);
    } else {
        strcpy(ram_str, "N/A");
    }
    return ram_str;
}

void execute_reverse_screen() {
    int ret;
    ret = system("xrandr -o inverted");
    (void)ret;
    sleep(3);
    ret = system("xrandr -o normal");
    (void)ret;
}

// Vérifie si le fichier est un GIF (par extension ou magic bytes)
static int is_gif_file(const char *filepath) {
    // Vérifier l'extension
    const char *ext = strrchr(filepath, '.');
    if (ext && (strcasecmp(ext, ".gif") == 0)) {
        return 1;
    }
    
    // Vérifier les magic bytes (GIF87a ou GIF89a)
    FILE *fp = fopen(filepath, "rb");
    if (fp) {
        unsigned char header[6];
        if (fread(header, 1, 6, fp) == 6) {
            fclose(fp);
            if (memcmp(header, "GIF87a", 6) == 0 || memcmp(header, "GIF89a", 6) == 0) {
                return 1;
            }
        } else {
            fclose(fp);
        }
    }
    return 0;
}

// Charge un GIF animé depuis un fichier
static AnimatedGif* load_animated_gif(const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        printf("Erreur ouverture fichier GIF: %s\n", filepath);
        return NULL;
    }
    
    // Lire tout le fichier en mémoire
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    unsigned char *buffer = malloc(file_size);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    
    if (fread(buffer, 1, file_size, fp) != (size_t)file_size) {
        free(buffer);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    
    // Charger le GIF animé
    int *delays = NULL;
    int width, height, frames, comp;
    
    unsigned char *gif_data = stbi_load_gif_from_memory(buffer, file_size, &delays, &width, &height, &frames, &comp, 4);
    free(buffer);
    
    if (!gif_data) {
        printf("Erreur chargement GIF animé: %s\n", stbi_failure_reason());
        return NULL;
    }
    
    // Créer la structure AnimatedGif
    AnimatedGif *gif = malloc(sizeof(AnimatedGif));
    if (!gif) {
        stbi_image_free(gif_data);
        if (delays) stbi_image_free(delays);
        return NULL;
    }
    
    gif->width = width;
    gif->height = height;
    gif->frame_count = frames;
    gif->delays = malloc(frames * sizeof(int));
    gif->frames = malloc(frames * sizeof(unsigned char*));
    
    if (!gif->delays || !gif->frames) {
        free(gif->delays);
        free(gif->frames);
        free(gif);
        stbi_image_free(gif_data);
        if (delays) stbi_image_free(delays);
        return NULL;
    }
    
    // Copier les frames et les délais
    size_t frame_size = width * height * 4;
    for (int i = 0; i < frames; i++) {
        gif->frames[i] = malloc(frame_size);
        if (gif->frames[i]) {
            memcpy(gif->frames[i], gif_data + i * frame_size, frame_size);
        }
        // Si delays est NULL ou le délai est 0, utiliser 100ms par défaut
        gif->delays[i] = (delays && delays[i] > 0) ? delays[i] : 100;
    }
    
    stbi_image_free(gif_data);
    if (delays) stbi_image_free(delays);
    
    printf("GIF chargé: %dx%d, %d frames\n", width, height, frames);
    return gif;
}

// Libère la mémoire d'un GIF animé
static void free_animated_gif(AnimatedGif *gif) {
    if (!gif) return;
    for (int i = 0; i < gif->frame_count; i++) {
        free(gif->frames[i]);
    }
    free(gif->frames);
    free(gif->delays);
    free(gif);
}

// Convertit RGBA en BGRA pour X11
static unsigned char* rgba_to_bgra(const unsigned char *rgba, int width, int height) {
    unsigned char *bgra = malloc(width * height * 4);
    if (!bgra) return NULL;
    
    for (int i = 0; i < width * height; i++) {
        bgra[i*4 + 0] = rgba[i*4 + 2]; // B
        bgra[i*4 + 1] = rgba[i*4 + 1]; // G
        bgra[i*4 + 2] = rgba[i*4 + 0]; // R
        bgra[i*4 + 3] = rgba[i*4 + 3]; // A
    }
    return bgra;
}

// Affiche un GIF animé qui défile sur l'écran
static void show_animated_gif_on_screen(AnimatedGif *gif) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        printf("Erreur ouverture display X11\n");
        return;
    }

    int screen = DefaultScreen(dpy);
    int screen_width = DisplayWidth(dpy, screen);
    int screen_height = DisplayHeight(dpy, screen);

    // Création de la fenêtre
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = 0x000000;
    
    Window win = XCreateWindow(dpy, RootWindow(dpy, screen),
                             screen_width, (screen_height - gif->height) / 2,
                             gif->width, gif->height,
                             0, CopyFromParent, InputOutput, CopyFromParent,
                             CWOverrideRedirect | CWBackPixel, &attrs);

    XMapWindow(dpy, win);
    XFlush(dpy);
    
    // Pré-créer les XImage pour chaque frame
    XImage **ximages = malloc(gif->frame_count * sizeof(XImage*));
    unsigned char **bgra_frames = malloc(gif->frame_count * sizeof(unsigned char*));
    
    for (int i = 0; i < gif->frame_count; i++) {
        bgra_frames[i] = rgba_to_bgra(gif->frames[i], gif->width, gif->height);
        ximages[i] = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
                                  ZPixmap, 0, (char *)bgra_frames[i], gif->width, gif->height, 32, 0);
    }
    
    // Animation
    int x = screen_width;
    int speed = 8; // Pixels par itération
    int current_frame = 0;
    int frame_time_acc = 0; // Accumulateur de temps pour les frames
    int move_interval = 10; // ms entre chaque mouvement
    
    while (x > -gif->width) {
        XMoveWindow(dpy, win, x, (screen_height - gif->height) / 2);
        XPutImage(dpy, win, DefaultGC(dpy, screen), ximages[current_frame], 0, 0, 0, 0, gif->width, gif->height);
        XFlush(dpy);
        
        usleep(move_interval * 1000);
        x -= speed;
        
        // Avancer la frame si nécessaire
        frame_time_acc += move_interval;
        if (frame_time_acc >= gif->delays[current_frame]) {
            frame_time_acc = 0;
            current_frame = (current_frame + 1) % gif->frame_count;
        }
    }

    // Nettoyage
    for (int i = 0; i < gif->frame_count; i++) {
        ximages[i]->data = NULL; // Détacher pour éviter double free
        XDestroyImage(ximages[i]);
        free(bgra_frames[i]);
    }
    free(ximages);
    free(bgra_frames);
    
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

// Fonction pour créer une fenêtre X11 sans bordure et afficher une image statique
static void show_image_on_screen(const char *image_path) {
    int width, height, channels;
    unsigned char *img = stbi_load(image_path, &width, &height, &channels, 4); // Force RGBA
    if (!img) {
        printf("Erreur chargement image: %s\n", image_path);
        return;
    }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        printf("Erreur ouverture display X11\n");
        stbi_image_free(img);
        return;
    }

    int screen = DefaultScreen(dpy);
    int screen_width = DisplayWidth(dpy, screen);
    int screen_height = DisplayHeight(dpy, screen);

    // Création de la fenêtre
    XSetWindowAttributes attrs;
    attrs.override_redirect = True; // Pas de gestionnaire de fenêtres (pas de bordure)
    attrs.background_pixel = 0x000000; // Fond noir (sera masqué par l'image)
    
    Window win = XCreateWindow(dpy, RootWindow(dpy, screen),
                             screen_width, (screen_height - height) / 2, // Position initiale (hors écran à droite)
                             width, height,
                             0, CopyFromParent, InputOutput, CopyFromParent,
                             CWOverrideRedirect | CWBackPixel, &attrs);

    // Conversion RGBA -> BGRA pour X11
    unsigned char *bgra = rgba_to_bgra(img, width, height);
    stbi_image_free(img);
    
    if (!bgra) {
        printf("Erreur allocation mémoire BGRA\n");
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return;
    }

    XImage *ximg = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
                              ZPixmap, 0, (char *)bgra, width, height, 32, 0);

    XMapWindow(dpy, win);
    
    // Animation
    int x = screen_width;
    int speed = 10; // Pixels par frame
    
    while (x > -width) {
        XMoveWindow(dpy, win, x, (screen_height - height) / 2);
        XPutImage(dpy, win, DefaultGC(dpy, screen), ximg, 0, 0, 0, 0, width, height);
        XFlush(dpy);
        
        x -= speed;
        usleep(10000); // 10ms = 100 FPS (théorique)
    }

    ximg->data = NULL; // Détacher pour éviter double free
    XDestroyImage(ximg);
    free(bgra);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

void execute_marquee(const char *url_or_path) {
    char filepath[512];
    int is_local = 0;
    int is_temp_file = 0;

    // Vérifier si c'est une URL ou un fichier local
    if (strncmp(url_or_path, "http://", 7) == 0 || strncmp(url_or_path, "https://", 8) == 0) {
        // C'est une URL, on télécharge
        // Détecter l'extension depuis l'URL
        const char *ext = strrchr(url_or_path, '.');
        const char *query = strchr(url_or_path, '?');
        char extension[16] = ".png";
        
        if (ext && (!query || ext < query)) {
            // Copier l'extension (max 15 chars)
            int len = query ? (int)(query - ext) : (int)strlen(ext);
            if (len > 15) len = 15;
            strncpy(extension, ext, len);
            extension[len] = '\0';
        }
        
        snprintf(filepath, sizeof(filepath), "/tmp/wallchange_marquee_%d%s", getpid(), extension);
        if (!download_image(url_or_path, filepath)) {
            printf("Erreur téléchargement image pour marquee\n");
            return;
        }
        is_temp_file = 1;
    } else {
        // C'est un fichier local
        if (access(url_or_path, F_OK) != -1) {
            strncpy(filepath, url_or_path, sizeof(filepath) - 1);
            is_local = 1;
        } else {
            printf("Fichier local introuvable : %s\n", url_or_path);
            return;
        }
    }

    // 2. Forker pour ne pas bloquer le client
    pid_t pid = fork();
    if (pid == 0) {
        // Processus enfant
        
        // Vérifier si c'est un GIF animé
        if (is_gif_file(filepath)) {
            AnimatedGif *gif = load_animated_gif(filepath);
            if (gif) {
                if (gif->frame_count > 1) {
                    // GIF animé
                    printf("Affichage GIF animé: %d frames\n", gif->frame_count);
                    show_animated_gif_on_screen(gif);
                } else {
                    // GIF statique (1 seule frame), traiter comme image normale
                    show_image_on_screen(filepath);
                }
                free_animated_gif(gif);
            } else {
                // Fallback sur image statique si le chargement GIF échoue
                show_image_on_screen(filepath);
            }
        } else {
            // Image normale (PNG, JPG, etc.)
            show_image_on_screen(filepath);
        }
        
        if (is_temp_file) {
            unlink(filepath); // Supprimer le fichier temporaire
        }
        exit(0);
    }
    // Le parent continue
}

