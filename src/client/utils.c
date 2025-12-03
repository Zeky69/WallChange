#include "client/utils.h"
#include "client/wallpaper.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

#define STB_IMAGE_IMPLEMENTATION
#include "common/stb_image.h"

char *get_username() {
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        return strdup(pw->pw_name);
    }
    return strdup(getenv("USER"));
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

// Fonction pour créer une fenêtre X11 sans bordure et afficher une image
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

    // Création de l'image X11
    // Note: X11 attend souvent du BGRA sur les architectures little-endian (x86)
    // On convertit RGBA -> BGRA
    unsigned char *bgra = malloc(width * height * 4);
    for (int i = 0; i < width * height; i++) {
        bgra[i*4 + 0] = img[i*4 + 2]; // B
        bgra[i*4 + 1] = img[i*4 + 1]; // G
        bgra[i*4 + 2] = img[i*4 + 0]; // R
        bgra[i*4 + 3] = img[i*4 + 3]; // A
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

    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    stbi_image_free(img);
    // ximg->data pointe vers bgra, qui sera free par XDestroyImage si on l'utilisait, 
    // mais ici on a malloc bgra nous même. XDestroyImage free la structure ET les data si data != NULL.
    // Pour éviter double free ou leak, on laisse XDestroyImage gérer.
    ximg->data = NULL; // On détache pour free nous même ou on laisse faire.
    // XDestroyImage(ximg); // Crash souvent si mal géré, on simplifie pour ce snippet
    free(bgra);
}

void execute_marquee(const char *url) {
    // 1. Télécharger l'image
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "/tmp/wallchange_marquee_%d.png", getpid());
    
    if (download_image(url, filepath)) {
        // 2. Forker pour ne pas bloquer le client
        pid_t pid = fork();
        if (pid == 0) {
            // Processus enfant
            show_image_on_screen(filepath);
            unlink(filepath); // Supprimer le fichier temporaire
            exit(0);
        }
        // Le parent continue
    } else {
        printf("Erreur téléchargement image pour marquee\n");
    }
}

