#include "client/utils.h"
#include "client/wallpaper.h"
#include "common/image_utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>

// Note: stb_image est déjà inclus dans common/image_utils.c
// On ne doit PAS définir STB_IMAGE_IMPLEMENTATION ici pour éviter les conflits
// Mais on a besoin des déclarations de stbi_load, etc.
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
        // Enlever le suffixe de domaine (garder seulement le nom court)
        char *dot = strchr(hostname, '.');
        if (dot) {
            *dot = '\0';
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

int is_screen_locked() {
    FILE *fp;
    char buf[256] = {0};

    // Méthode 1: Vérifier si un greeter (codam-web-greeter / nody-greeter / web-greeter)
    // tourne. Le greeter ne spawn QUE quand l'écran est verrouillé.
    fp = popen("pgrep -x 'nody-greeter|web-greeter' 2>/dev/null", "r");
    if (fp) {
        if (fgets(buf, sizeof(buf), fp)) {
            pclose(fp);
            // Un PID a été trouvé → le greeter est actif → verrouillé
            return 1;
        }
        pclose(fp);
    }

    // Méthode 2: loginctl — session Active=no quand le greeter prend le focus
    fp = popen("loginctl show-session $(loginctl list-sessions --no-legend | grep $(whoami) | head -1 | awk '{print $1}') -p LockedHint -p Active 2>/dev/null", "r");
    if (fp) {
        int locked_hint = -1;
        int active = -1;
        while (fgets(buf, sizeof(buf), fp)) {
            if (strstr(buf, "LockedHint=yes")) locked_hint = 1;
            else if (strstr(buf, "LockedHint=no")) locked_hint = 0;
            if (strstr(buf, "Active=no")) active = 0;
            else if (strstr(buf, "Active=yes")) active = 1;
        }
        pclose(fp);
        // Si LockedHint=yes → verrouillé (fiable si le DE le signale)
        if (locked_hint == 1) return 1;
        // Si la session est inactive → un greeter/autre session a pris le focus → verrouillé
        if (active == 0) return 1;
        // Si LockedHint=no ET Active=yes → déverrouillé
        if (locked_hint == 0 && active == 1) return 0;
    }

    // Méthode 3: gnome-screensaver-command (fonctionne sur certains setups Unity)
    fp = popen("gnome-screensaver-command -q 2>/dev/null", "r");
    if (fp) {
        buf[0] = '\0';
        if (fgets(buf, sizeof(buf), fp)) {
            pclose(fp);
            if (strstr(buf, "is active")) return 1;
            if (strstr(buf, "is inactive")) return 0;
        } else {
            pclose(fp);
        }
    }

    // Méthode 4: Vérifier si un processus greeter LightDM existe (fallback)
    fp = popen("pgrep -f 'lightdm.*greeter' 2>/dev/null", "r");
    if (fp) {
        buf[0] = '\0';
        if (fgets(buf, sizeof(buf), fp)) {
            pclose(fp);
            return 1;  // Un greeter LightDM est actif → verrouillé
        }
        pclose(fp);
    }

    return 0; // Par défaut, considérer comme déverrouillé
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
        // stbi_failure_reason() est disponible via stb_image.h
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
        // Note: is_gif_file n'est pas défini dans image_utils.h, on utilise une détection simple par extension ou on tente de charger
        // Pour l'instant, on suppose que load_animated_gif gère l'échec si ce n'est pas un GIF
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
        
        if (is_temp_file) {
            unlink(filepath); // Supprimer le fichier temporaire
        }
        exit(0);
    }
    // Le parent continue
}

// Structure pour une particule
typedef struct {
    float x, y;           // Position
    float vx, vy;         // Vélocité
    float life;           // Durée de vie restante (0-1)
    float size_factor;    // Facteur de taille (0.5-1.5)
    float rotation;       // Rotation en degrés
    float rot_speed;      // Vitesse de rotation
} Particle;

#define MAX_PARTICLES 50
#define PARTICLE_SPAWN_RATE 8  // Particules par frame
#define PARTICLE_SIZE 48       // Taille standard des particules en pixels

// Crée un masque de transparence à partir des données RGBA
static Pixmap create_shape_mask(Display *dpy, Window root, const unsigned char *rgba, int w, int h) {
    Pixmap mask = XCreatePixmap(dpy, root, w, h, 1);
    GC gc = XCreateGC(dpy, mask, 0, NULL);
    
    // Remplir avec 0 (transparent)
    XSetForeground(dpy, gc, 0);
    XFillRectangle(dpy, mask, gc, 0, 0, w, h);
    
    // Dessiner les pixels opaques (alpha > 128)
    XSetForeground(dpy, gc, 1);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 4;
            if (rgba[idx + 3] > 128) {  // Alpha > 50%
                XDrawPoint(dpy, mask, gc, x, y);
            }
        }
    }
    
    XFreeGC(dpy, gc);
    return mask;
}

// Affiche des particules autour de la souris pendant 5 secondes
static void show_particles_around_mouse(const char *image_path) {
    int orig_width, orig_height, channels;
    unsigned char *orig_img = stbi_load(image_path, &orig_width, &orig_height, &channels, 4);
    if (!orig_img) {
        printf("Erreur chargement image particule: %s\n", image_path);
        return;
    }

    // Redimensionner à la taille standard
    int img_width = PARTICLE_SIZE;
    int img_height = PARTICLE_SIZE;
    unsigned char *img = resize_image(orig_img, orig_width, orig_height, 4, img_width, img_height);
    stbi_image_free(orig_img);
    
    if (!img) {
        printf("Erreur redimensionnement image\n");
        return;
    }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        printf("Erreur ouverture display X11\n");
        free(img);
        return;
    }

    int screen = DefaultScreen(dpy);
    int screen_width = DisplayWidth(dpy, screen);
    int screen_height = DisplayHeight(dpy, screen);
    Window root = RootWindow(dpy, screen);

    // Convertir RGBA -> BGRA pour X11
    unsigned char *bgra = rgba_to_bgra(img, img_width, img_height);
    
    // Créer le masque de transparence AVANT de libérer img
    Pixmap shape_mask = create_shape_mask(dpy, root, img, img_width, img_height);
    free(img);
    
    if (!bgra) {
        XFreePixmap(dpy, shape_mask);
        XCloseDisplay(dpy);
        return;
    }

    // Créer l'image X11 de base
    XImage *base_ximg = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
                                      ZPixmap, 0, (char *)bgra, img_width, img_height, 32, 0);

    // Initialiser les particules
    Particle particles[MAX_PARTICLES];
    Window particle_windows[MAX_PARTICLES];
    int particle_active[MAX_PARTICLES];
    
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_active[i] = 0;
        
        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        attrs.background_pixel = 0;
        
        particle_windows[i] = XCreateWindow(dpy, root,
                                            0, 0, img_width, img_height,
                                            0, CopyFromParent, InputOutput, CopyFromParent,
                                            CWOverrideRedirect | CWBackPixel, &attrs);
        
        // Appliquer le masque de transparence
        XShapeCombineMask(dpy, particle_windows[i], ShapeBounding, 0, 0, shape_mask, ShapeSet);
    }

    // Animation pendant 10 secondes (temps réel)
    struct timeval start_tv, current_tv;
    gettimeofday(&start_tv, NULL);
    double elapsed = 0.0;
    
    while (elapsed < 10.0) {
        // Obtenir la position de la souris
        Window root_return, child_return;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;
        XQueryPointer(dpy, root, &root_return, &child_return, &root_x, &root_y, &win_x, &win_y, &mask);
        
        // Spawner de nouvelles particules
        for (int s = 0; s < PARTICLE_SPAWN_RATE; s++) {
            for (int i = 0; i < MAX_PARTICLES; i++) {
                if (!particle_active[i]) {
                    particle_active[i] = 1;
                    particles[i].x = root_x - img_width / 2;
                    particles[i].y = root_y - img_height / 2;
                    
                    // Vélocité aléatoire en cercle
                    float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
                    float speed = 2.0f + (rand() % 50) / 10.0f;  // 2-7 pixels/frame
                    particles[i].vx = cos(angle) * speed;
                    particles[i].vy = sin(angle) * speed;
                    
                    particles[i].life = 1.0f;
                    particles[i].size_factor = 0.5f + (rand() % 100) / 100.0f;  // 0.5-1.5
                    particles[i].rotation = (float)(rand() % 360);
                    particles[i].rot_speed = (rand() % 20) - 10;  // -10 à +10 deg/frame
                    
                    XMapWindow(dpy, particle_windows[i]);
                    break;
                }
            }
        }
        
        // Mettre à jour et afficher les particules
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (particle_active[i]) {
                // Mise à jour physique
                particles[i].x += particles[i].vx;
                particles[i].y += particles[i].vy;
                particles[i].vy += 0.3f;  // Gravité
                particles[i].life -= 0.02f;
                particles[i].rotation += particles[i].rot_speed;
                
                // Vérifier si la particule est morte
                if (particles[i].life <= 0 || 
                    particles[i].x < -img_width || particles[i].x > screen_width ||
                    particles[i].y < -img_height || particles[i].y > screen_height) {
                    particle_active[i] = 0;
                    XUnmapWindow(dpy, particle_windows[i]);
                    continue;
                }
                
                // Déplacer la fenêtre
                XMoveWindow(dpy, particle_windows[i], (int)particles[i].x, (int)particles[i].y);
                XPutImage(dpy, particle_windows[i], DefaultGC(dpy, screen), base_ximg, 
                          0, 0, 0, 0, img_width, img_height);
            }
        }
        
        XFlush(dpy);
        usleep(16000);  // ~60 FPS
        
        gettimeofday(&current_tv, NULL);
        elapsed = (current_tv.tv_sec - start_tv.tv_sec) + 
                  (current_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
    }

    // Nettoyage
    for (int i = 0; i < MAX_PARTICLES; i++) {
        XDestroyWindow(dpy, particle_windows[i]);
    }
    
    XFreePixmap(dpy, shape_mask);
    base_ximg->data = NULL;
    XDestroyImage(base_ximg);
    free(bgra);
    XCloseDisplay(dpy);
}

void execute_particles(const char *url_or_path) {
    char filepath[512];
    int is_temp_file = 0;

    // Vérifier si c'est une URL ou un fichier local
    if (strncmp(url_or_path, "http://", 7) == 0 || strncmp(url_or_path, "https://", 8) == 0) {
        // C'est une URL, on télécharge
        const char *ext = strrchr(url_or_path, '.');
        const char *query = strchr(url_or_path, '?');
        char extension[16] = ".png";
        
        if (ext && (!query || ext < query)) {
            int len = query ? (int)(query - ext) : (int)strlen(ext);
            if (len > 15) len = 15;
            strncpy(extension, ext, len);
            extension[len] = '\0';
        }
        
        snprintf(filepath, sizeof(filepath), "/tmp/wallchange_particle_%d%s", getpid(), extension);
        if (!download_image(url_or_path, filepath)) {
            printf("Erreur téléchargement image pour particles\n");
            return;
        }
        is_temp_file = 1;
    } else {
        // C'est un fichier local
        if (access(url_or_path, F_OK) != -1) {
            strncpy(filepath, url_or_path, sizeof(filepath) - 1);
        } else {
            printf("Fichier local introuvable : %s\n", url_or_path);
            return;
        }
    }

    // Forker pour ne pas bloquer le client
    pid_t pid = fork();
    if (pid == 0) {
        srand(time(NULL) ^ getpid());
        show_particles_around_mouse(filepath);
        
        if (is_temp_file) {
            unlink(filepath);
        }
        exit(0);
    }
}

#define MAX_COVER_WINDOWS 1000


static void show_static_cover_on_screen(const char *image_path) {
    int orig_width, orig_height, channels;
    unsigned char *orig_img = stbi_load(image_path, &orig_width, &orig_height, &channels, 4);
    if (!orig_img) {
        printf("Erreur chargement image cover: %s\n", image_path);
        return;
    }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        printf("Erreur ouverture display X11\n");
        stbi_image_free(orig_img);
        return;
    }

    int screen = DefaultScreen(dpy);
    int screen_width = DisplayWidth(dpy, screen);
    int screen_height = DisplayHeight(dpy, screen);
    Window root = RootWindow(dpy, screen);

    Window windows[MAX_COVER_WINDOWS];
    int window_count = 0;
    
    // Initialise windows array
    memset(windows, 0, sizeof(windows));

    struct timeval start_tv, current_tv;
    gettimeofday(&start_tv, NULL);
    double elapsed = 0.0;
    
    while (elapsed < 10.0 && window_count < MAX_COVER_WINDOWS) {
        int scale_percent = 20 + (rand() % 180); // 20% to 200%
        int new_w = (orig_width * scale_percent) / 100;
        int new_h = (orig_height * scale_percent) / 100;
        
        if (new_w > screen_width) new_w = screen_width;
        if (new_h > screen_height) new_h = screen_height;
        if (new_w < 50) new_w = 50;
        if (new_h < 50) new_h = 50;

        int pos_x = (rand() % (screen_width + 100)) - 50;
        int pos_y = (rand() % (screen_height + 100)) - 50;
        
        unsigned char *resized_img = resize_image(orig_img, orig_width, orig_height, 4, new_w, new_h);
        if (!resized_img) {
             usleep(10000);
             continue;
        }

        unsigned char *bgra = rgba_to_bgra(resized_img, new_w, new_h);
        
        Pixmap shape_mask = create_shape_mask(dpy, root, resized_img, new_w, new_h);
        free(resized_img);
        
        if (!bgra) {
            XFreePixmap(dpy, shape_mask);
             continue;
        }

        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        attrs.background_pixel = 0;
        
        Window win = XCreateWindow(dpy, root,
                                   pos_x, pos_y, new_w, new_h,
                                   0, CopyFromParent, InputOutput, CopyFromParent,
                                   CWOverrideRedirect | CWBackPixel, &attrs);
                                   
        XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, shape_mask, ShapeSet);
        XFreePixmap(dpy, shape_mask);
        
        XMapWindow(dpy, win);
        
        XImage *ximg = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
                                  ZPixmap, 0, (char *)bgra, new_w, new_h, 32, 0);
                                  
        XPutImage(dpy, win, DefaultGC(dpy, screen), ximg, 0, 0, 0, 0, new_w, new_h);
        
        ximg->data = NULL;
        XDestroyImage(ximg);
        free(bgra);

        windows[window_count++] = win;
        XFlush(dpy);
        
        usleep(10000); 
        
        gettimeofday(&current_tv, NULL);
        elapsed = (current_tv.tv_sec - start_tv.tv_sec) + 
                  (current_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
    }
    
    while (elapsed < 10.0) {
        usleep(100000);
        gettimeofday(&current_tv, NULL);
        elapsed = (current_tv.tv_sec - start_tv.tv_sec) + 
                  (current_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
    }

    for (int i = 0; i < window_count; i++) {
        XDestroyWindow(dpy, windows[i]);
    }
    stbi_image_free(orig_img);
    XCloseDisplay(dpy);
}

// Structure pour gérer les variations de taille (pour les GIFs)
typedef struct {
    int width;
    int height;
    Pixmap *frames_pmap;
    Pixmap *masks_pmap;
    int valid;
} GifVariation;

static void show_gif_cover_on_screen(const char *image_path) {
    AnimatedGif *gif = load_animated_gif(image_path);
    if (!gif) {
        printf("Erreur chargement GIF cover: %s\n", image_path);
        return;
    }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        printf("Erreur ouverture display X11\n");
        free_animated_gif(gif);
        return;
    }

    int screen = DefaultScreen(dpy);
    int screen_width = DisplayWidth(dpy, screen);
    int screen_height = DisplayHeight(dpy, screen);
    Window root = RootWindow(dpy, screen);

    // Initialiser les variations de taille
    const int NUM_VARIATIONS = 5;
    GifVariation variations[NUM_VARIATIONS];
    memset(variations, 0, sizeof(variations));

    // Générer les différentes tailles
    for (int v = 0; v < NUM_VARIATIONS; v++) {
        variations[v].valid = 0;
        int scale_percent = 20 + (rand() % 130); // 20% à 150%
        int new_w = (gif->width * scale_percent) / 100;
        int new_h = (gif->height * scale_percent) / 100;

        // Limites raisonnables
        if (new_w > screen_width / 2) new_w = screen_width / 2;
        if (new_h > screen_height / 2) new_h = screen_height / 2;
        if (new_w < 50) new_w = 50;
        if (new_h < 50) new_h = 50;

        variations[v].width = new_w;
        variations[v].height = new_h;
        variations[v].frames_pmap = calloc(gif->frame_count, sizeof(Pixmap));
        variations[v].masks_pmap = calloc(gif->frame_count, sizeof(Pixmap));

        if (!variations[v].frames_pmap || !variations[v].masks_pmap) {
            continue; // Skip this variation if alloc fails
        }
        
        int success = 1;
        for (int i = 0; i < gif->frame_count; i++) {
            unsigned char *resized = resize_image(gif->frames[i], gif->width, gif->height, 4, new_w, new_h);
            if (!resized) { success = 0; break; }

            unsigned char *bgra = rgba_to_bgra(resized, new_w, new_h);
            if (!bgra) { free(resized); success = 0; break; }

            variations[v].masks_pmap[i] = create_shape_mask(dpy, root, resized, new_w, new_h);

            variations[v].frames_pmap[i] = XCreatePixmap(dpy, root, new_w, new_h, DefaultDepth(dpy, screen));
            XImage *ximg = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
                                        ZPixmap, 0, (char *)bgra, new_w, new_h, 32, 0);

            GC gc = XCreateGC(dpy, variations[v].frames_pmap[i], 0, NULL);
            XPutImage(dpy, variations[v].frames_pmap[i], gc, ximg, 0, 0, 0, 0, new_w, new_h);
            XFreeGC(dpy, gc);

            ximg->data = NULL; // bgra est libéré manuellement
            XDestroyImage(ximg);
            free(bgra);
            free(resized);
        }
        
        if (success) {
            variations[v].valid = 1;
        } else {
             // Clean up partial allocation
             for (int i = 0; i < gif->frame_count; i++) {
                 if (variations[v].frames_pmap[i]) XFreePixmap(dpy, variations[v].frames_pmap[i]);
                 if (variations[v].masks_pmap[i]) XFreePixmap(dpy, variations[v].masks_pmap[i]);
             }
             free(variations[v].frames_pmap);
             free(variations[v].masks_pmap);
             variations[v].frames_pmap = NULL;
             variations[v].masks_pmap = NULL;
        }
    }
    
    // Boucle principale
    Window windows[MAX_COVER_WINDOWS]; 
    int window_variations[MAX_COVER_WINDOWS]; // Map window index to variation index
    int window_count = 0;
    memset(windows, 0, sizeof(windows));
    memset(window_variations, 0, sizeof(window_variations));
    
    struct timeval start_tv, current_tv, last_frame_tv;
    gettimeofday(&start_tv, NULL);
    last_frame_tv = start_tv;
    
    double elapsed = 0.0;
    int current_frame = 0;
    double last_window_creation = 0.0;
    int max_windows_gif = 100; // Moins de fenêtres pour les GIFs pour la perf

    while (elapsed < 10.0) {
        gettimeofday(&current_tv, NULL);
        elapsed = (current_tv.tv_sec - start_tv.tv_sec) + 
                  (current_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
                  
        // Créer de nouvelles fenêtres
        if (window_count < max_windows_gif && (elapsed - last_window_creation > 0.1)) {
             // Choisir une variation valide aléatoire
             int v_idx = rand() % NUM_VARIATIONS;
             int attempts = 0;
             while (!variations[v_idx].valid && attempts < NUM_VARIATIONS) {
                 v_idx = (v_idx + 1) % NUM_VARIATIONS;
                 attempts++;
             }

             if (variations[v_idx].valid) {
                 int new_w = variations[v_idx].width;
                 int new_h = variations[v_idx].height;
                 int pos_x = (rand() % (screen_width + 100)) - 50;
                 int pos_y = (rand() % (screen_height + 100)) - 50;
                 
                 XSetWindowAttributes attrs;
                 attrs.override_redirect = True;
                 attrs.background_pixmap = variations[v_idx].frames_pmap[current_frame];
                 
                 Window win = XCreateWindow(dpy, root,
                                           pos_x, pos_y, new_w, new_h,
                                           0, CopyFromParent, InputOutput, CopyFromParent,
                                           CWOverrideRedirect | CWBackPixmap, &attrs);
                                           
                 XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, variations[v_idx].masks_pmap[current_frame], ShapeSet);
                 XMapWindow(dpy, win);
                 
                 windows[window_count] = win;
                 window_variations[window_count] = v_idx;
                 window_count++;
                 last_window_creation = elapsed;
             }
        }
        
        // Mettre à jour l'animation
        double frame_elapsed = (current_tv.tv_sec - last_frame_tv.tv_sec) * 1000.0 + 
                               (current_tv.tv_usec - last_frame_tv.tv_usec) / 1000.0;
                               
        if (frame_elapsed >= gif->delays[current_frame]) {
            current_frame = (current_frame + 1) % gif->frame_count;
            last_frame_tv = current_tv;
            
            for (int i = 0; i < window_count; i++) {
                int v_idx = window_variations[i];
                if (variations[v_idx].valid) {
                    XSetWindowBackgroundPixmap(dpy, windows[i], variations[v_idx].frames_pmap[current_frame]);
                    XShapeCombineMask(dpy, windows[i], ShapeBounding, 0, 0, variations[v_idx].masks_pmap[current_frame], ShapeSet);
                    XClearWindow(dpy, windows[i]);
                }
            }
            XFlush(dpy);
        }
        
        usleep(10000); 
    }

    // Nettoyage
    for (int i = 0; i < window_count; i++) {
        XDestroyWindow(dpy, windows[i]);
    }
    
    for (int v = 0; v < NUM_VARIATIONS; v++) {
        if (variations[v].valid) {
            for (int i = 0; i < gif->frame_count; i++) {
                if (variations[v].frames_pmap[i]) XFreePixmap(dpy, variations[v].frames_pmap[i]);
                if (variations[v].masks_pmap[i]) XFreePixmap(dpy, variations[v].masks_pmap[i]);
            }
            free(variations[v].frames_pmap);
            free(variations[v].masks_pmap);
        }
    }
    
    free_animated_gif(gif);
    XCloseDisplay(dpy);
}

static void show_cover_on_screen(const char *image_path) {
    if (is_gif_file(image_path)) {
        show_gif_cover_on_screen(image_path);
    } else {
        show_static_cover_on_screen(image_path);
    }
}

void execute_cover(const char *url_or_path) {
    char filepath[512];
    int is_temp_file = 0;

    if (strncmp(url_or_path, "http://", 7) == 0 || strncmp(url_or_path, "https://", 8) == 0) {
        const char *ext = strrchr(url_or_path, '.');
        const char *query = strchr(url_or_path, '?');
        char extension[16] = ".png";
        
        if (ext && (!query || ext < query)) {
            int len = query ? (int)(query - ext) : (int)strlen(ext);
            if (len > 15) len = 15;
            strncpy(extension, ext, len);
            extension[len] = '\0';
        }
        
        snprintf(filepath, sizeof(filepath), "/tmp/wallchange_cover_%d%s", getpid(), extension);
        if (!download_image(url_or_path, filepath)) {
            printf("Erreur téléchargement image pour cover\n");
            return;
        }
        is_temp_file = 1;
    } else {
        if (access(url_or_path, F_OK) != -1) {
            strncpy(filepath, url_or_path, sizeof(filepath) - 1);
        } else {
            printf("Fichier local introuvable : %s\n", url_or_path);
            return;
        }
    }

    pid_t pid = fork();
    if (pid == 0) {
        srand(time(NULL) ^ getpid());
        show_cover_on_screen(filepath);
        
        if (is_temp_file) {
            unlink(filepath);
        }
        exit(0);
    }
}

// ============== MOUSE CLONES ==============

#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xfixes.h>

#define NUM_CLONES 150
#define CLONE_SIZE 24  // Taille du curseur clone

// Structure pour un clone de souris
typedef struct {
    float x, y;           // Position
    float offset_x, offset_y;  // Décalage par rapport à la vraie souris
    float delay;          // Délai de suivi (0-1)
    float target_x, target_y;  // Position cible
    int follow_mouse;     // 1 = suit la souris, 0 = position fixe aléatoire
} MouseClone;

// Capture l'image du curseur actuel
static unsigned char* capture_cursor_image(Display *dpy, int *width, int *height) {
    XFixesCursorImage *cursor = XFixesGetCursorImage(dpy);
    if (!cursor) {
        printf("Impossible de capturer le curseur\n");
        return NULL;
    }
    
    *width = cursor->width;
    *height = cursor->height;
    
    // Convertir les pixels (ARGB format) en RGBA
    unsigned char *rgba = malloc(cursor->width * cursor->height * 4);
    if (!rgba) {
        XFree(cursor);
        return NULL;
    }
    
    for (int i = 0; i < cursor->width * cursor->height; i++) {
        unsigned long pixel = cursor->pixels[i];
        rgba[i * 4 + 0] = (pixel >> 16) & 0xFF;  // R
        rgba[i * 4 + 1] = (pixel >> 8) & 0xFF;   // G
        rgba[i * 4 + 2] = pixel & 0xFF;          // B
        rgba[i * 4 + 3] = (pixel >> 24) & 0xFF;  // A
    }
    
    XFree(cursor);
    return rgba;
}

// Affiche les clones de souris
static void show_mouse_clones(void) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        printf("Erreur ouverture display X11\n");
        return;
    }

    // Vérifier XFixes
    int event_base, error_base;
    if (!XFixesQueryExtension(dpy, &event_base, &error_base)) {
        printf("Extension XFixes non disponible\n");
        XCloseDisplay(dpy);
        return;
    }

    int screen = DefaultScreen(dpy);
    int screen_width = DisplayWidth(dpy, screen);
    int screen_height = DisplayHeight(dpy, screen);
    Window root = RootWindow(dpy, screen);

    // Capturer l'image du curseur
    int cursor_width, cursor_height;
    unsigned char *cursor_rgba = capture_cursor_image(dpy, &cursor_width, &cursor_height);
    if (!cursor_rgba) {
        // Fallback : créer un curseur simple (flèche)
        cursor_width = CLONE_SIZE;
        cursor_height = CLONE_SIZE;
        cursor_rgba = malloc(CLONE_SIZE * CLONE_SIZE * 4);
        memset(cursor_rgba, 0, CLONE_SIZE * CLONE_SIZE * 4);
        
        // Dessiner une flèche simple
        for (int y = 0; y < CLONE_SIZE; y++) {
            for (int x = 0; x < y && x < CLONE_SIZE; x++) {
                int idx = (y * CLONE_SIZE + x) * 4;
                cursor_rgba[idx + 0] = 0;    // R
                cursor_rgba[idx + 1] = 0;    // G
                cursor_rgba[idx + 2] = 0;    // B
                cursor_rgba[idx + 3] = 255;  // A
            }
        }
    }

    // Redimensionner si nécessaire
    unsigned char *img = cursor_rgba;
    int img_width = cursor_width;
    int img_height = cursor_height;
    
    if (cursor_width != CLONE_SIZE || cursor_height != CLONE_SIZE) {
        img = resize_image(cursor_rgba, cursor_width, cursor_height, 4, CLONE_SIZE, CLONE_SIZE);
        free(cursor_rgba);
        if (!img) {
            XCloseDisplay(dpy);
            return;
        }
        img_width = CLONE_SIZE;
        img_height = CLONE_SIZE;
    }

    // Convertir RGBA -> BGRA pour X11
    unsigned char *bgra = rgba_to_bgra(img, img_width, img_height);
    
    // Créer le masque de transparence
    Pixmap shape_mask = create_shape_mask(dpy, root, img, img_width, img_height);
    free(img);
    
    if (!bgra) {
        XFreePixmap(dpy, shape_mask);
        XCloseDisplay(dpy);
        return;
    }

    // Créer l'image X11
    XImage *ximg = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
                                ZPixmap, 0, (char *)bgra, img_width, img_height, 32, 0);

    // Initialiser les clones - allocation dynamique pour 1000 clones
    MouseClone *clones = malloc(NUM_CLONES * sizeof(MouseClone));
    Window *clone_windows = malloc(NUM_CLONES * sizeof(Window));
    
    if (!clones || !clone_windows) {
        free(clones);
        free(clone_windows);
        XFreePixmap(dpy, shape_mask);
        ximg->data = NULL;
        XDestroyImage(ximg);
        free(bgra);
        XCloseDisplay(dpy);
        return;
    }
    
    // Position initiale de la souris
    Window root_return, child_return;
    int mouse_x, mouse_y, win_x, win_y;
    unsigned int mask;
    XQueryPointer(dpy, root, &root_return, &child_return, &mouse_x, &mouse_y, &win_x, &win_y, &mask);
    
    for (int i = 0; i < NUM_CLONES; i++) {
        float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
        float distance;
        float delay;
        int follow = 1;
        
        if (i < 5) {
            // 5 clones suivent EXACTEMENT la souris (indétectable)
            distance = 0;
            delay = 1.0f;
            follow = 1;
        } else if (i < 20) {
            // 15 clones très proches avec réponse instantanée
            distance = 10.0f + (rand() % 40);  // 10-50 pixels
            delay = 0.5f + (rand() % 50) / 100.0f;  // 0.5-1.0 (très rapide)
            follow = 1;
        } else if (i < 50) {
            // 30 clones proches rapides
            distance = 30.0f + (rand() % 100);  // 30-130 pixels
            delay = 0.2f + (rand() % 40) / 100.0f;  // 0.2-0.6
            follow = 1;
        } else if (i < 90) {
            // 40 clones qui suivent avec délai
            distance = 50.0f + (rand() % 200);  // 50-250 pixels
            delay = 0.1f + (rand() % 20) / 100.0f;  // 0.1-0.3
            follow = 1;
        } else {
            // 60 clones dispersés sur TOUT l'écran (position aléatoire fixe ou lent)
            // Ils bougent lentement ou restent fixes pour créer du bruit visuel
            clones[i].x = rand() % screen_width;
            clones[i].y = rand() % screen_height;
            clones[i].offset_x = 0;
            clones[i].offset_y = 0;
            clones[i].target_x = clones[i].x;
            clones[i].target_y = clones[i].y;
            clones[i].delay = 0.02f + (rand() % 5) / 100.0f;  // Très lent
            clones[i].follow_mouse = (rand() % 3 == 0) ? 1 : 0;  // 1/3 suivent lentement
            
            if (clones[i].follow_mouse) {
                clones[i].offset_x = (rand() % screen_width) - screen_width/2;
                clones[i].offset_y = (rand() % screen_height) - screen_height/2;
            }
            
            XSetWindowAttributes attrs;
            attrs.override_redirect = True;
            attrs.background_pixel = 0;
            
            clone_windows[i] = XCreateWindow(dpy, root,
                                             (int)clones[i].x, (int)clones[i].y,
                                             img_width, img_height,
                                             0, CopyFromParent, InputOutput, CopyFromParent,
                                             CWOverrideRedirect | CWBackPixel, &attrs);
            
            XShapeCombineMask(dpy, clone_windows[i], ShapeBounding, 0, 0, shape_mask, ShapeSet);
            XMapWindow(dpy, clone_windows[i]);
            continue;
        }
        
        clones[i].offset_x = cos(angle) * distance;
        clones[i].offset_y = sin(angle) * distance;
        clones[i].x = mouse_x + clones[i].offset_x;
        clones[i].y = mouse_y + clones[i].offset_y;
        clones[i].target_x = clones[i].x;
        clones[i].target_y = clones[i].y;
        clones[i].delay = delay;
        clones[i].follow_mouse = follow;
        
        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        attrs.background_pixel = 0;
        
        clone_windows[i] = XCreateWindow(dpy, root,
                                         (int)clones[i].x, (int)clones[i].y,
                                         img_width, img_height,
                                         0, CopyFromParent, InputOutput, CopyFromParent,
                                         CWOverrideRedirect | CWBackPixel, &attrs);
        
        // Appliquer le masque de transparence
        XShapeCombineMask(dpy, clone_windows[i], ShapeBounding, 0, 0, shape_mask, ShapeSet);
        XMapWindow(dpy, clone_windows[i]);
    }

    // Cacher le vrai curseur
    Cursor invisible_cursor;
    Pixmap cursor_pixmap = XCreatePixmap(dpy, root, 1, 1, 1);
    XColor black = {0};
    invisible_cursor = XCreatePixmapCursor(dpy, cursor_pixmap, cursor_pixmap, &black, &black, 0, 0);
    XDefineCursor(dpy, root, invisible_cursor);
    XFreePixmap(dpy, cursor_pixmap);

    // Animation pendant 5 secondes
    struct timeval start_tv, current_tv;
    gettimeofday(&start_tv, NULL);
    double elapsed = 0.0;
    
    while (elapsed < 5.0) {
        // Obtenir la position de la souris
        XQueryPointer(dpy, root, &root_return, &child_return, &mouse_x, &mouse_y, &win_x, &win_y, &mask);
        
        // Mettre à jour chaque clone
        for (int i = 0; i < NUM_CLONES; i++) {
            if (clones[i].follow_mouse) {
                // Nouvelle cible = position souris + décalage
                clones[i].target_x = mouse_x + clones[i].offset_x;
                clones[i].target_y = mouse_y + clones[i].offset_y;
            }
            // Sinon la cible reste sa position actuelle (fixe ou mouvement aléatoire)
            
            // Interpolation douce vers la cible (effet de traîne)
            float lerp = clones[i].delay;
            clones[i].x += (clones[i].target_x - clones[i].x) * lerp;
            clones[i].y += (clones[i].target_y - clones[i].y) * lerp;
            
            // Limiter aux bords de l'écran
            if (clones[i].x < 0) clones[i].x = 0;
            if (clones[i].x > screen_width - img_width) clones[i].x = screen_width - img_width;
            if (clones[i].y < 0) clones[i].y = 0;
            if (clones[i].y > screen_height - img_height) clones[i].y = screen_height - img_height;
            
            // Déplacer la fenêtre
            XMoveWindow(dpy, clone_windows[i], (int)clones[i].x, (int)clones[i].y);
            XPutImage(dpy, clone_windows[i], DefaultGC(dpy, screen), ximg, 
                      0, 0, 0, 0, img_width, img_height);
        }
        
        XFlush(dpy);
        usleep(8000);  // ~120 FPS pour plus de fluidité
        
        gettimeofday(&current_tv, NULL);
        elapsed = (current_tv.tv_sec - start_tv.tv_sec) + 
                  (current_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
    }

    // Restaurer le curseur normal
    XUndefineCursor(dpy, root);
    XFreeCursor(dpy, invisible_cursor);

    // Nettoyage
    for (int i = 0; i < NUM_CLONES; i++) {
        XDestroyWindow(dpy, clone_windows[i]);
    }
    
    free(clones);
    free(clone_windows);
    XFreePixmap(dpy, shape_mask);
    ximg->data = NULL;
    XDestroyImage(ximg);
    free(bgra);
    XCloseDisplay(dpy);
}

void execute_clones(void) {
    pid_t pid = fork();
    if (pid == 0) {
        srand(time(NULL) ^ getpid());
        show_mouse_clones();
        exit(0);
    }
}

void execute_drunk(void) {
    pid_t pid = fork();
    if (pid == 0) {
        Display *display = XOpenDisplay(NULL);
        if (!display) exit(1);
        
        srand(time(NULL) ^ getpid());
        time_t start = time(NULL);
        
        // Durée de l'effet : 10 secondes
        while (time(NULL) - start < 10) {
            int dx = (rand() % 41) - 20; // -20 à 20
            int dy = (rand() % 41) - 20;
            
            XWarpPointer(display, None, None, 0, 0, 0, 0, dx, dy);
            XFlush(display);
            
            usleep(50000); // 50ms
        }
        
        XCloseDisplay(display);
        exit(0);
    }
}



// ==========================================
// NOUVEAUX EFFETS
// ==========================================

// Helper to create a transparent overlay window that passes clicks through
static Window create_overlay_window(Display *dpy, int width, int height, Visual **visual_out, int *depth_out) {
    XVisualInfo vinfo;
    // Try to find a 32-bit visual for transparency
    if (XMatchVisualInfo(dpy, DefaultScreen(dpy), 32, TrueColor, &vinfo)) {
        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        attrs.colormap = XCreateColormap(dpy, DefaultRootWindow(dpy), vinfo.visual, AllocNone);
        attrs.background_pixel = 0; // Transparent
        attrs.border_pixel = 0;
        
        Window win = XCreateWindow(dpy, DefaultRootWindow(dpy), 0, 0, width, height, 0,
                                   vinfo.depth, InputOutput, vinfo.visual,
                                   CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel, &attrs);
                                   
        // Make click-through
        Region region = XCreateRegion();
        XShapeCombineRegion(dpy, win, ShapeInput, 0, 0, region, ShapeSet);
        XDestroyRegion(region);
        
        if (visual_out) *visual_out = vinfo.visual;
        if (depth_out) *depth_out = vinfo.depth;
        return win;
    }
    
    // Fallback to normal window if no 32-bit visual (no transparency support)
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = BlackPixel(dpy, DefaultScreen(dpy)); // Black
    
    Window win = XCreateWindow(dpy, DefaultRootWindow(dpy), 0, 0, width, height, 0,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWOverrideRedirect | CWBackPixel, &attrs);
                               
    // Make click-through
    Region region = XCreateRegion();
    XShapeCombineRegion(dpy, win, ShapeInput, 0, 0, region, ShapeSet);
    XDestroyRegion(region);
    
    if (visual_out) *visual_out = DefaultVisual(dpy, DefaultScreen(dpy));
    if (depth_out) *depth_out = DefaultDepth(dpy, DefaultScreen(dpy));
    return win;
}

// --- FAKE TERMINAL (MATRIX) ---
static void show_faketerminal() {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    int screen = DefaultScreen(dpy);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);
    Window root = RootWindow(dpy, screen);

    Visual *visual;
    int depth;
    Window win = create_overlay_window(dpy, width, height, &visual, &depth);

    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    XFlush(dpy);

    GC gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, gc, 0x00FF00); // Green
    
    XFontStruct *font = XLoadQueryFont(dpy, "fixed");
    if (font) XSetFont(dpy, gc, font->fid);

    int char_width = font ? font->max_bounds.width : 10;
    int char_height = font ? font->ascent + font->descent : 15;
    int cols = width / char_width + 1;
    int *drops = calloc(cols, sizeof(int));

    time_t start = time(NULL);
    while (time(NULL) - start < 10) { // 10 secondes
        // Effacer légèrement (trail effect) - simulation simple
        // Pour un vrai effet Matrix, on devrait dessiner des rects noirs semi-transparents
        // mais X11 pur ne gère pas l'alpha facilement.
        // On va juste redessiner quelques rects noirs aléatoires pour effacer
        
        for (int i = 0; i < cols; i++) {
            char c = (rand() % 94) + 33; // ASCII printable
            int x = i * char_width;
            int y = drops[i] * char_height;

            // Dessiner le caractère
            XDrawString(dpy, win, gc, x, y, &c, 1);

            // Parfois effacer la colonne pour recommencer
            if (y > height && (rand() % 20) > 18) {
                drops[i] = 0;
            } else {
                drops[i]++;
            }
            
            // Effacer le caractère au dessus (pour faire tomber)
            // Ou laisser la trace... Matrix laisse une trace.
            // On va effacer un bloc plus haut pour limiter la trace
            if (drops[i] > 15) {
                 XSetForeground(dpy, gc, 0); // Transparent/Black
                 XFillRectangle(dpy, win, gc, x, (drops[i] - 15) * char_height, char_width, char_height);
                 XSetForeground(dpy, gc, 0x00FF00);
            }
        }
        XFlush(dpy);
        usleep(30000);
    }

    free(drops);
    if (font) XFreeFont(dpy, font);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

void execute_faketerminal(void) {
    pid_t pid = fork();
    if (pid == 0) {
        srand(time(NULL) ^ getpid());
        show_faketerminal();
        exit(0);
    }
}

// --- CONFETTI ---
typedef struct {
    float x, y;
    float vx, vy;
    unsigned long color;
    int size;
    int frame_offset; // Pour des animations désynchronisées
} Confetti;

static void show_confetti(const char *img_path) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    int screen = DefaultScreen(dpy);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);
    
    // Création de la fenêtre overlay (transparente si possible)
    Visual *visual = NULL;
    int depth = 0;
    Window win = create_overlay_window(dpy, width, height, &visual, &depth);

    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    XFlush(dpy);

    GC gc = XCreateGC(dpy, win, 0, NULL);
    
    // Chargement de l'image / GIF si présent
    int use_image = 0;
    Pixmap *frames_pmap = NULL;
    int frame_count = 0;
    int *delays = NULL;
    int img_w = 48, img_h = 48; // Taille des confettis image
    
    if (img_path && img_path[0] != '\0' && access(img_path, F_OK) == 0) {
        if (is_gif_file(img_path)) {
            AnimatedGif *gif = load_animated_gif(img_path);
            if (gif) {
                use_image = 1;
                frame_count = gif->frame_count;
                delays = malloc(frame_count * sizeof(int));
                memcpy(delays, gif->delays, frame_count * sizeof(int));
                
                frames_pmap = calloc(frame_count, sizeof(Pixmap));
                
                for (int i = 0; i < frame_count; i++) {
                    unsigned char *resized = resize_image(gif->frames[i], gif->width, gif->height, 4, img_w, img_h);
                    if (resized) {
                        unsigned char *bgra = rgba_to_bgra(resized, img_w, img_h);
                        free(resized);
                        
                        if (bgra) {
                            frames_pmap[i] = XCreatePixmap(dpy, win, img_w, img_h, depth);
                            XImage *ximg = XCreateImage(dpy, visual, depth, ZPixmap, 0, (char *)bgra, img_w, img_h, 32, 0);
                            GC pmap_gc = XCreateGC(dpy, frames_pmap[i], 0, NULL);
                            XPutImage(dpy, frames_pmap[i], pmap_gc, ximg, 0, 0, 0, 0, img_w, img_h);
                            XFreeGC(dpy, pmap_gc);
                            
                            ximg->data = NULL; // bgra libéré manuellement si besoin, mais XCreateImage prend ownership de data si on ne fait pas gaffe. 
                            // Ici on a alloué bgra, on le passe à XCreateImage. XDestroyImage va free data.
                            XDestroyImage(ximg);
                        }
                    }
                }
                free_animated_gif(gif); // Note: cela free gif->delays donc on a copié avant
            }
        } else {
            // Image statique
            int w, h, c;
            unsigned char *img = stbi_load(img_path, &w, &h, &c, 4);
            if (img) {
                use_image = 1;
                frame_count = 1;
                delays = malloc(sizeof(int));
                delays[0] = 1000; // Pas utilisé
                
                frames_pmap = calloc(1, sizeof(Pixmap));
                
                unsigned char *resized = resize_image(img, w, h, 4, img_w, img_h);
                stbi_image_free(img);
                
                if (resized) {
                    unsigned char *bgra = rgba_to_bgra(resized, img_w, img_h);
                    free(resized);
                    if (bgra) {
                        frames_pmap[0] = XCreatePixmap(dpy, win, img_w, img_h, depth);
                        XImage *ximg = XCreateImage(dpy, visual, depth, ZPixmap, 0, (char *)bgra, img_w, img_h, 32, 0);
                        GC pmap_gc = XCreateGC(dpy, frames_pmap[0], 0, NULL);
                        XPutImage(dpy, frames_pmap[0], pmap_gc, ximg, 0, 0, 0, 0, img_w, img_h);
                        XFreeGC(dpy, pmap_gc);
                        XDestroyImage(ximg);
                    }
                }
            }
        }
    }

    int num_confetti = 150;
    Confetti *parts = malloc(sizeof(Confetti) * num_confetti);
    unsigned long colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    
    for (int i = 0; i < num_confetti; i++) {
        parts[i].x = rand() % width;
        parts[i].y = rand() % height - height; // Commencer au dessus
        parts[i].vx = (rand() % 10 - 5) / 2.0;
        parts[i].vy = (rand() % 10 + 5);
        parts[i].color = colors[rand() % 7];
        parts[i].size = use_image ? img_w : (rand() % 10 + 5);
        parts[i].frame_offset = rand() % (frame_count > 0 ? frame_count : 1);
    }

    struct timeval start_tv, current_tv;
    gettimeofday(&start_tv, NULL);
    struct timeval last_frame_tv = start_tv;
    int global_frame = 0; // Si on veut synchroniser
    
    while (1) {
        gettimeofday(&current_tv, NULL);
        double elapsed_total = (current_tv.tv_sec - start_tv.tv_sec) + 
                              (current_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
        
        if (elapsed_total >= 10.0) break;

        // Update animation frame based on delays
        if (use_image && frame_count > 1) {
            double frame_elapsed = (current_tv.tv_sec - last_frame_tv.tv_sec) * 1000.0 + 
                                   (current_tv.tv_usec - last_frame_tv.tv_usec) / 1000.0;
            if (frame_elapsed >= delays[global_frame]) {
                 global_frame = (global_frame + 1) % frame_count;
                 last_frame_tv = current_tv;
            }
        }

        // Redessiner le fond (effacer les confettis précédents)
        XClearWindow(dpy, win);
        
        for (int i = 0; i < num_confetti; i++) {
            parts[i].x += parts[i].vx;
            parts[i].y += parts[i].vy;
            
            // Gravité / Vent
            parts[i].vx += (rand() % 3 - 1) * 0.1;
            
            // Reset si sort de l'écran par le bas
            if (parts[i].y > height) {
                parts[i].y = -50;
                parts[i].x = rand() % width;
            }
            
            if (use_image && frames_pmap && frames_pmap[0]) {
                // Determine frame index
                int f_idx = 0;
                if (frame_count > 1) {
                    // Option 1: Synchronized
                    f_idx = global_frame;
                    
                    // Option 2: Individual offset (uncomment to desynchronize)
                    // f_idx = (global_frame + parts[i].frame_offset) % frame_count;
                }
                
                if (frames_pmap[f_idx]) {
                    // Use XCopyArea to copy Pixmap to Window (fast)
                    // Note: This requires Pixmap depth == Window depth
                    XCopyArea(dpy, frames_pmap[f_idx], win, gc, 
                              0, 0, img_w, img_h, 
                              (int)parts[i].x, (int)parts[i].y);
                }
            } else {
                XSetForeground(dpy, gc, parts[i].color);
                XFillRectangle(dpy, win, gc, (int)parts[i].x, (int)parts[i].y, parts[i].size, parts[i].size);
            }
        }
        
        XFlush(dpy);
        usleep(20000); // ~50 FPS
    }

    if (frames_pmap) {
        for(int i=0; i<frame_count; i++) {
            if (frames_pmap[i]) XFreePixmap(dpy, frames_pmap[i]);
        }
        free(frames_pmap);
    }
    if (delays) free(delays);
    free(parts);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

void execute_confetti(const char *url) {
    char filepath[512] = {0};
    if (url && strlen(url) > 0) {
        char *username = get_username();
        snprintf(filepath, sizeof(filepath), "/home/%s/.cache/wallchange_confetti.png", username);
        free(username);
        
        // Créer le dossier cache si nécessaire
        char *dir = strdup(filepath);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            mkdir(dir, 0755);
        }
        free(dir);

        if (!download_image(url, filepath)) {
            filepath[0] = '\0'; // Fallback aux confettis couleurs
        }
    }

    pid_t pid = fork();
    if (pid == 0) {
        srand(time(NULL) ^ getpid());
        show_confetti(filepath[0] ? filepath : NULL);
        exit(0);
    }
}

// --- SPOTLIGHT ---
static void show_spotlight() {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    int screen = DefaultScreen(dpy);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);
    Window root = RootWindow(dpy, screen);

    Visual *visual;
    int depth;
    Window win = create_overlay_window(dpy, width, height, &visual, &depth);

    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    XFlush(dpy);

    // Créer un GC pour le dessin si besoin
    // GC gc = XCreateGC(dpy, win, 0, NULL);
    
    // Create GC for background filling
    GC gc_bg = XCreateGC(dpy, win, 0, NULL);
    if (depth == 32) {
        XSetForeground(dpy, gc_bg, 0xF0000000); // Semi-transparent black
    } else {
        XSetForeground(dpy, gc_bg, BlackPixel(dpy, screen));
    }

    time_t start = time(NULL);
    int radius = 150;

    while (time(NULL) - start < 10) {
        Window root_return, child_return;
        int root_x, root_y, win_x, win_y;
        unsigned int mask_return;

        if (XQueryPointer(dpy, root, &root_return, &child_return, 
                          &root_x, &root_y, &win_x, &win_y, &mask_return)) {
            
            // Créer la région pour tout l'écran
            // XRectangle screen_rect = {0, 0, width, height};
            // Region region = XCreateRegion();
            // XUnionRectWithRegion(&screen_rect, region, region);
            
            // Créer la région pour le trou (cercle approximé par octogone ou rects)
            // XShape ne supporte pas les cercles parfaits directement, il faut une bitmap ou des rects
            // On va utiliser une bitmap pour faire un cercle propre
            
            Pixmap mask = XCreatePixmap(dpy, win, width, height, 1);
            GC mask_gc = XCreateGC(dpy, mask, 0, NULL);
            
            // Remplir tout en noir (opaque)
            XSetForeground(dpy, mask_gc, 1);
            XFillRectangle(dpy, mask, mask_gc, 0, 0, width, height);
            
            // Dessiner le cercle en blanc (transparent pour ShapeBounding ? Non, 1=opaque, 0=transparent)
            // Pour XShapeCombineMask: 1 = opaque (montre la fenêtre noire), 0 = transparent (montre le bureau)
            // Donc on veut tout à 1 sauf le cercle à 0.
            
            XSetForeground(dpy, mask_gc, 0);
            XFillArc(dpy, mask, mask_gc, root_x - radius, root_y - radius, radius*2, radius*2, 0, 360*64);
            
            XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, mask, ShapeSet);
            
            XFreeGC(dpy, mask_gc);
            XFreePixmap(dpy, mask);
            // XDestroyRegion(region);
            
            // Redraw background to ensure it stays dark (XShape might expose/clear areas)
            XFillRectangle(dpy, win, gc_bg, 0, 0, width, height);
        }
        
        XFlush(dpy);
        usleep(20000);
    }
    
    XFreeGC(dpy, gc_bg);

    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

void execute_spotlight(void) {
    pid_t pid = fork();
    if (pid == 0) {
        show_spotlight();
        exit(0);
    }
}



// ==========================================
// NOUVEAUX EFFETS (V2)
// ==========================================

// --- TEXTSCREEN ---
static void show_textscreen(const char *text) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    int screen = DefaultScreen(dpy);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);
    Window root = RootWindow(dpy, screen);

    Visual *visual;
    int depth;
    Window win = create_overlay_window(dpy, width, height, &visual, &depth);

    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    XFlush(dpy);

    GC gc = XCreateGC(dpy, win, 0, NULL);
    
    // Essayer de charger une TRES grande police
    // On essaie plusieurs tailles décroissantes
    XFontStruct *font = XLoadQueryFont(dpy, "-*-*-bold-r-*-*-72-*-*-*-*-*-*-*");
    if (!font) font = XLoadQueryFont(dpy, "-*-*-bold-r-*-*-48-*-*-*-*-*-*-*");
    if (!font) font = XLoadQueryFont(dpy, "-*-*-bold-r-*-*-34-*-*-*-*-*-*-*");
    if (!font) font = XLoadQueryFont(dpy, "fixed");
    
    if (font) XSetFont(dpy, gc, font->fid);

    const char *msg = (text && strlen(text) > 0) ? text : "HELLO WORLD";
    int text_len = strlen(msg);
    int text_width = font ? XTextWidth(font, msg, text_len) : text_len * 10;
    int text_height = font ? font->ascent + font->descent : 15;

    // Centrer
    int x = (width - text_width) / 2;
    int y = (height - text_height) / 2;

    srand(time(NULL));
    time_t start = time(NULL);
    
    while (time(NULL) - start < 5) {
        XClearWindow(dpy, win); // Redessine le fond (transparence)
        
        // Couleur aléatoire vive
        unsigned long color = (rand() % 0xFFFFFF);
        // S'assurer que ce n'est pas trop sombre
        if ((color & 0xFF) < 50 && ((color >> 8) & 0xFF) < 50 && ((color >> 16) & 0xFF) < 50) {
            color |= 0x808080;
        }
        XSetForeground(dpy, gc, color);

        XDrawString(dpy, win, gc, x, y, msg, text_len);
        
        XFlush(dpy);
        usleep(200000); // Changer de couleur 5 fois par seconde
    }

    if (font) XFreeFont(dpy, font);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

void execute_textscreen(const char *text) {
    pid_t pid = fork();
    if (pid == 0) {
        show_textscreen(text);
        exit(0);
    }
}

// --- WAVESCREEN ---
static void show_wavescreen() {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    int screen = DefaultScreen(dpy);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);
    
    Visual *visual;
    int depth;
    Window win = create_overlay_window(dpy, width, height, &visual, &depth);

    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    XFlush(dpy);
    
    GC gc = XCreateGC(dpy, win, 0, NULL);
    XSetLineAttributes(dpy, gc, 2, LineSolid, CapRound, JoinRound);
    
    // Optimisation : allocation unique du tableau de points
    int step = 5;
    int max_points = width / step + 5;
    XPoint *points = malloc(sizeof(XPoint) * max_points);
    
    time_t start = time(NULL);
    float t = 0;
    
    // Palette "Neon / Synthwave"
    unsigned long colors[] = {
        0x00FFFF, // Cyan
        0xFF00FF, // Magenta
        0x0000FF, // Blue
        0x8A2BE2, // BlueViolet
        0xFF1493  // DeepPink
    };

    while (time(NULL) - start < 10) {
        XClearWindow(dpy, win);
        
        for (int i = 0; i < 5; i++) {
            int count = 0;
            float offset_base = height / 2.0;
            float amplitude = 60.0;
            
            for (int x = 0; x <= width; x += step) {
                // Combinaison de plusieurs ondes pour un effet plus fluide
                float val = sin(x * 0.003 + t + i * 0.5) * amplitude
                          + sin(x * 0.01 + t * 1.5 + i) * (amplitude * 0.5);
                
                points[count].x = x;
                points[count].y = offset_base + val + (i - 2) * 40;
                count++;
            }
            
            XSetForeground(dpy, gc, colors[i % 5]);
            XDrawLines(dpy, win, gc, points, count, CoordModeOrigin);
        }
        
        XFlush(dpy);
        t += 0.08;
        usleep(16000); // ~60 FPS
    }

    free(points);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

void execute_wavescreen(void) {
    pid_t pid = fork();
    if (pid == 0) {
        show_wavescreen();
        exit(0);
    }
}

// --- DVDBOUNCE ---
static void show_dvdbounce(const char *img_path) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    int screen = DefaultScreen(dpy);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);

    Visual *visual;
    int depth;
    Window win = create_overlay_window(dpy, width, height, &visual, &depth);

    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    
    GC gc = XCreateGC(dpy, win, 0, NULL);
    
    // --- Charger l'image si possible ---
    int img_w = 0, img_h = 0;
    unsigned char *alpha_mask = NULL; // On stocke juste l'alpha pour coloriser
    unsigned char *current_bgra = NULL;
    XImage *ximg = NULL;

    if (img_path) {
        int w, h, c;
        unsigned char *file_data = stbi_load(img_path, &w, &h, &c, 4);
        if (file_data) {
            // Redimensionner si trop grand ou trop petit
            int target_w = 200;
            // Ratio
            float ratio = (float)h / (float)w;
            int target_h = (int)(target_w * ratio);
            
            unsigned char *resized = resize_image(file_data, w, h, 4, target_w, target_h);
            stbi_image_free(file_data);
            
            if (resized) {
                img_w = target_w;
                img_h = target_h;
                
                // Extraire l'alpha mask
                alpha_mask = malloc(img_w * img_h);
                // Pré-allouer le buffer BGRA pour X11
                current_bgra = malloc(img_w * img_h * 4);
                
                for(int i=0; i < img_w * img_h; i++) {
                    // Si l'image source est blanche/transparente, l'alpha est en [3].
                    // Si c'est un PNG noir et blanc, on peut assumer que la luminosité est l'alpha, 
                    // ou utiliser l'alpha channel direct.
                    // Le logo wikipedia est Noir sur Transparent.
                    // Si on veut le coloriser, on utilise l'alpha pur.
                    alpha_mask[i] = resized[i*4 + 3]; 
                }
                
                free(resized); // On a copié ce qu'il fallait (alpha)
                
                // Créer XImage
                ximg = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
                                    ZPixmap, 0, (char *)current_bgra, img_w, img_h, 32, 0);
            }
        }
    }
    
    // Si pas d'image, on définit des dimensions par défaut pour le fallback vectoriel
    int bounce_w = (ximg) ? img_w : 160;
    int bounce_h = (ximg) ? img_h : 70;

    float x = rand() % (width - bounce_w);
    float y = rand() % (height - bounce_h);
    float vx = 4.0, vy = 4.0;
    
    // Palette
    unsigned long colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x00FFFF, 0xFF00FF, 0xFFFFFF, 0xFFA500};
    int color_idx = rand() % 8;
    int prev_color_idx = -1;

    // Fallback fonts
    XFontStruct *font = NULL;
    if (!ximg) {
        XSetLineAttributes(dpy, gc, 3, LineSolid, CapRound, JoinRound);
        font = XLoadQueryFont(dpy, "-*-helvetica-bold-r-*-*-34-*-*-*-*-*-*-*");
        if (!font) font = XLoadQueryFont(dpy, "-*-*-bold-r-*-*-24-*-*-*-*-*-*-*");
        if (!font) font = XLoadQueryFont(dpy, "fixed");
        if (font) XSetFont(dpy, gc, font->fid);
    }
    
    time_t start = time(NULL);
    while (time(NULL) - start < 60) {
        // Logique de rebond
        x += vx;
        y += vy;
        
        int hit = 0;
        if (x <= 0) { x = 0; vx = -vx; hit = 1; }
        else if (x + bounce_w >= width) { x = width - bounce_w; vx = -vx; hit = 1; }
        
        if (y <= 0) { y = 0; vy = -vy; hit = 1; }
        else if (y + bounce_h >= height) { y = height - bounce_h; vy = -vy; hit = 1; }
        
        if (hit) {
            color_idx = (color_idx + 1) % 8;
        }

        XClearWindow(dpy, win);
        
        if (ximg) {
            // --- MODE IMAGE COLORISÉE ---
            // Si la couleur a changé (ou premier passage), on recrée le buffer pixels
            if (color_idx != prev_color_idx) {
                unsigned long c = colors[color_idx];
                unsigned char r = (c >> 16) & 0xFF;
                unsigned char g = (c >> 8) & 0xFF;
                unsigned char b = (c) & 0xFF;
                
                for (int i = 0; i < img_w * img_h; i++) {
                    unsigned char a = alpha_mask[i];
                    // BGRA
                    // Si alpha > 0, on met la couleur. Sinon 0 (transparent).
                    // Mais attention, XPutImage copie tout. Si on veut de la transparence sur Overlay,
                    // Soit on utilise ShapeMask (compliqué pour anti-aliasing partiel), 
                    // Soit on assume que le "fond" est noir/transparent (ce qui est le cas de overlay window).
                    
                    // Note: Notre create_overlay_window crée une fenêtre ARGB si possible (compositing).
                    // Si on a le compositing, on peut utiliser le canal alpha du BGRA.
                    // Si on ne l'a pas, ça fera du noir.
                    
                    // Premultiplied alpha n'est pas standard X11 core, mais pour un visual 32bit:
                    // Pixel = (A << 24) | (R << 16) | (G << 8) | B
                    // Souvent X11 attend un RGB prémultiplié pour la compo? Ça dépend du compositeur.
                    // Essayons standard non-prémultiplié ou simple mapping.
                    
                    current_bgra[i*4 + 0] = b;
                    current_bgra[i*4 + 1] = g;
                    current_bgra[i*4 + 2] = r;
                    current_bgra[i*4 + 3] = a; 
                }
                prev_color_idx = color_idx;
            }
            
            XPutImage(dpy, win, DefaultGC(dpy, screen), ximg, 0, 0, (int)x, (int)y, img_w, img_h);
        } else {
            // --- MODE FALLBACK VECTORIEL ---
            XSetForeground(dpy, gc, colors[color_idx]);
            XDrawArc(dpy, win, gc, (int)x, (int)y, bounce_w, bounce_h, 0, 360*64);
            const char *txt = "DVD";
            int txt_w = font ? XTextWidth(font, txt, 3) : 20;
            int txt_h = font ? (font->ascent) : 10;
            XDrawString(dpy, win, gc, (int)x + (bounce_w - txt_w)/2, (int)y + (bounce_h + txt_h)/2 - 5, txt, 3);
            XFillArc(dpy, win, gc, (int)x + bounce_w/2 - 25, (int)y + bounce_h - 8, 50, 16, 0, 360*64);
        }
        
        XFlush(dpy);
        usleep(16000); 
    }

    if (ximg) {
        ximg->data = NULL; // Éviter free par XDestroyImage car alloué par nous
        XDestroyImage(ximg);
        free(current_bgra);
        free(alpha_mask);
    }
    if (font) XFreeFont(dpy, font);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

void execute_dvdbounce(const char *url) {
    char filepath[512] = {0};
    char *username = get_username();
    snprintf(filepath, sizeof(filepath), "/home/%s/.cache/wallchange_dvd.png", username);
    free(username);
    
    // Créer répertoire si besoin
    char *dir = strdup(filepath);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    mkdir(dir, 0755);
    free(dir);

    // URL par défaut si vide
    // Logo DVD transparent (Wikipedia)
    const char *default_url = "https://upload.wikimedia.org/wikipedia/commons/thumb/9/9b/DVD_logo.svg/320px-DVD_logo.svg.png";
    const char *target = (url && strlen(url) > 0) ? url : NULL;

    int needs_download = 0;
    if (target) {
        // Si URL perso, on télécharge toujours (ou on pourrait hasher l'url pour le cache mais simple ici)
        needs_download = 1;
    } else {
        // Si defaut, on vérifie si existe déjà
        if (access(filepath, F_OK) == -1) {
            target = default_url;
            needs_download = 1;
        } else {
            // Existe déjà, on l'utilise
        }
    }

    if (needs_download && target) {
        download_image(target, filepath);
    }
    
    // Si fichier n'existe pas (échec download), on passe NULL pour activer le fallback vectoriel
    if (access(filepath, F_OK) == -1) {
        filepath[0] = '\0';
    }

    pid_t pid = fork();
    if (pid == 0) {
        srand(time(NULL) ^ getpid());
        show_dvdbounce(filepath[0] ? filepath : NULL);
        exit(0);
    }
}

// --- FIREWORKS ---
typedef struct {
    float x, y;
    float vx, vy;
    int life;
    unsigned long color;
} FireworkParticle;

#define MAX_FIREWORKS_PARTICLES 1000
static FireworkParticle particles[MAX_FIREWORKS_PARTICLES];
static int p_count = 0;

static void spawn_explosion(int x, int y) {
    int count = 50;
    unsigned long color = (rand() % 0xFFFFFF);
    for (int i = 0; i < count; i++) {
        if (p_count >= MAX_FIREWORKS_PARTICLES) break;
        particles[p_count].x = x;
        particles[p_count].y = y;
        float angle = (rand() % 360) * 3.14159 / 180.0;
        float speed = (rand() % 100) / 10.0;
        particles[p_count].vx = cos(angle) * speed;
        particles[p_count].vy = sin(angle) * speed;
        particles[p_count].life = 50 + (rand() % 20);
        particles[p_count].color = color;
        p_count++;
    }
}

static void show_fireworks() {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    int screen = DefaultScreen(dpy);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);
    Window root = RootWindow(dpy, screen);

    Visual *visual;
    int depth;
    Window win = create_overlay_window(dpy, width, height, &visual, &depth);

    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    
    GC gc = XCreateGC(dpy, win, 0, NULL);
    
    time_t start = time(NULL);
    int last_mouse_state = 0;

    while (time(NULL) - start < 15) {
        XClearWindow(dpy, win);
        
        // Gestion souris
        Window root_ret, child_ret;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;
        if (XQueryPointer(dpy, root, &root_ret, &child_ret, &root_x, &root_y, &win_x, &win_y, &mask)) {
            int clicked = (mask & Button1Mask);
            if (clicked && !last_mouse_state) {
                spawn_explosion(root_x, root_y);
            }
            last_mouse_state = clicked;
        }
        
        // Auto spawn
        if (rand() % 20 == 0) {
            spawn_explosion(rand() % width, rand() % height);
        }
        
        // Update & Draw
        int new_count = 0;
        for (int i = 0; i < p_count; i++) {
            if (particles[i].life > 0) {
                particles[i].x += particles[i].vx;
                particles[i].y += particles[i].vy;
                particles[i].vy += 0.2; // Gravité
                particles[i].life--;
                
                XSetForeground(dpy, gc, particles[i].color);
                XFillRectangle(dpy, win, gc, (int)particles[i].x, (int)particles[i].y, 3, 3);
                
                particles[new_count++] = particles[i];
            }
        }
        p_count = new_count;
        
        XFlush(dpy);
        usleep(20000);
    }

    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

void execute_fireworks(void) {
    pid_t pid = fork();
    if (pid == 0) {
        srand(time(NULL) ^ getpid());
        show_fireworks();
        exit(0);
    }
}

void execute_lock(void) {
    // Execute the lock command
    if (system("/usr/bin/dm-tool switch-to-greeter") == -1) {
    }
}

void execute_blackout(void) {
    // Éteint l'écran (brightness 0), surveille la souris.
    // Si la souris bouge → brightness 1 + lock session immédiatement.
    // Sinon après 20 min → brightness 1 + lock.
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int ret;
        
        // Récupérer la position initiale de la souris
        Display *dpy = XOpenDisplay(NULL);
        int init_x = 0, init_y = 0;
        if (dpy) {
            Window root_ret, child_ret;
            int win_x, win_y;
            unsigned int mask;
            XQueryPointer(dpy, DefaultRootWindow(dpy), &root_ret, &child_ret,
                          &init_x, &init_y, &win_x, &win_y, &mask);
        }
        
        // Brightness 0 → écran noir
        ret = system("xrandr --output eDP --brightness 0 2>/dev/null || "
                     "xrandr --output eDP-1 --brightness 0 2>/dev/null");
        (void)ret;
        printf("Blackout: écran éteint, surveillance souris active...\n");
        
        // Surveiller la souris pendant 20 minutes (1200 secondes)
        // Vérifier toutes les 500ms si la souris a bougé
        int mouse_moved = 0;
        for (int i = 0; i < 2400; i++) { // 2400 * 500ms = 1200s = 20min
            usleep(500000); // 500ms
            if (dpy) {
                Window root_ret, child_ret;
                int cur_x, cur_y, win_x, win_y;
                unsigned int mask;
                XQueryPointer(dpy, DefaultRootWindow(dpy), &root_ret, &child_ret,
                              &cur_x, &cur_y, &win_x, &win_y, &mask);
                // Tolérance de 5 pixels pour éviter les micro-mouvements
                if (abs(cur_x - init_x) > 5 || abs(cur_y - init_y) > 5) {
                    printf("Blackout: mouvement souris détecté (%d,%d → %d,%d), arrêt blackout + lock\n",
                           init_x, init_y, cur_x, cur_y);
                    mouse_moved = 1;
                    break;
                }
            }
        }
        
        if (dpy) XCloseDisplay(dpy);
        
        if (mouse_moved) {
            printf("Blackout: souris bougée → déconnexion + rallumage en parallèle\n");
        } else {
            printf("Blackout: 20 min écoulées → déconnexion + rallumage en parallèle\n");
        }
        
        // Rallumer l'écran d'abord puis déconnecter la session
        // Brightness 1 → rallumer l'écran
        ret = system("xrandr --output eDP --brightness 1 2>/dev/null || "
                     "xrandr --output eDP-1 --brightness 1 2>/dev/null");
        (void)ret;
        
        if (mouse_moved) {
            // Souris bougée → déconnecter la session (logout) au lieu de juste lock
            printf("Blackout: déconnexion de la session...\n");
            ret = system("loginctl terminate-session $(loginctl show-user $(whoami) -p Sessions --value | awk '{print $1}') 2>/dev/null || "
                         "pkill -KILL -u $(whoami) 2>/dev/null");
            (void)ret;
        } else {
            // 20 min écoulées → simple verrouillage
            ret = system("/usr/bin/dm-tool switch-to-greeter");
            (void)ret;
        }
        _exit(0);
    }
    if (pid > 0) {
        printf("Blackout lancé (PID: %d) — écran noir + surveillance souris\n", pid);
    }
}

void execute_fakelock(void) {
    // Lance le greeter codam en mode debug (fenêtre) puis le passe en fullscreen
    // sans réellement verrouiller la session
    int ret;
    // Tuer un éventuel fakelock déjà en cours
    ret = system("pkill -f 'nody-greeter.*--mode.*debug' 2>/dev/null");
    (void)ret;
    usleep(200000);

    // Lancer nody-greeter en debug mode en background
    pid_t pid = fork();
    if (pid == 0) {
        // Enfant: lancer le greeter
        setsid();
        // Rediriger stdout/stderr vers /dev/null
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
        execlp("nody-greeter", "nody-greeter", "--mode", "debug", "--theme", "codam", NULL);
        _exit(1);
    }
    if (pid < 0) return;

    // Attendre que la fenêtre apparaisse puis la passer en fullscreen via xprop
    // On fork un helper pour ne pas bloquer le thread principal
    pid_t helper = fork();
    if (helper == 0) {
        // Attendre que la fenêtre du greeter apparaisse (max 5s)
        char wid_str[64] = {0};
        for (int i = 0; i < 50; i++) {
            usleep(100000); // 100ms
            FILE *fp = popen("xdotool search --name 'nody-greeter\\|Codam\\|web-greeter\\|Login' 2>/dev/null | head -1", "r");
            if (!fp) {
                // xdotool pas disponible, essayer xprop method
                break;
            }
            if (fgets(wid_str, sizeof(wid_str), fp)) {
                pclose(fp);
                // Supprimer newline
                char *nl = strchr(wid_str, '\n');
                if (nl) *nl = '\0';
                if (wid_str[0] != '\0') break;
            } else {
                pclose(fp);
            }
        }

        if (wid_str[0] != '\0') {
            // Passer la fenêtre en fullscreen avec xprop
            char cmd[256];
            snprintf(cmd, sizeof(cmd),
                "xprop -id %s -f _NET_WM_STATE 32a "
                "-set _NET_WM_STATE _NET_WM_STATE_FULLSCREEN,_NET_WM_STATE_ABOVE 2>/dev/null",
                wid_str);
            ret = system(cmd);
            (void)ret;
        } else {
            // Fallback sans xdotool: attendre puis chercher la fenêtre par PID
            char script[512];
            snprintf(script, sizeof(script),
                "sleep 2; for w in $(xprop -root _NET_CLIENT_LIST 2>/dev/null "
                "| grep -oP '0x[0-9a-f]+'); do "
                "xprop -id $w _NET_WM_PID 2>/dev/null | grep -q ' = %d$' && "
                "xprop -id $w -f _NET_WM_STATE 32a "
                "-set _NET_WM_STATE _NET_WM_STATE_FULLSCREEN,_NET_WM_STATE_ABOVE "
                "2>/dev/null && break; done",
                pid);
            ret = system(script);
            (void)ret;
        }
        _exit(0);
    }

    printf("Fakelock lancé (PID greeter: %d)\n", pid);
}

// Helper pour rendre une fenêtre totalement transparente aux clics (Input Transparent)
static void make_window_input_transparent(Display *dpy, Window win) {
    XRectangle rect;
    // Créer une région vide (0 rectangles)
    XserverRegion region = XFixesCreateRegion(dpy, &rect, 0);
    // Appliquer cette règle à la forme d'entrée (ShapeInput)
    // Cela signifie que la "zone cliquable" est vide, donc tous les clics passent au travers
    XFixesSetWindowShapeRegion(dpy, win, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(dpy, region);
}

// --- INVERT SCREEN (GAMMA RAMP) ---
static void show_invert() {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    Window root = RootWindow(dpy, DefaultScreen(dpy));
    XRRScreenResources *res = XRRGetScreenResources(dpy, root);
    if (!res) {
        XCloseDisplay(dpy);
        return;
    }

    // Sauvegarder et inverser pour chaque CRTC
    typedef struct {
        RRCrtc crtc;
        int size;
        unsigned short *red;
        unsigned short *green;
        unsigned short *blue;
    } SavedGamma;

    SavedGamma *saved = calloc(res->ncrtc, sizeof(SavedGamma));
    int saved_count = 0;

    for (int i = 0; i < res->ncrtc; i++) {
        RRCrtc crtc = res->crtcs[i];
        if (XRRGetCrtcInfo(dpy, res, crtc)->noutput == 0) continue; // Skip disconnected

        int size = XRRGetCrtcGammaSize(dpy, crtc);
        if (size == 0) continue;

        XRRCrtcGamma *gamma = XRRGetCrtcGamma(dpy, crtc);
        if (gamma) {
            // Sauvegarde
            saved[saved_count].crtc = crtc;
            saved[saved_count].size = size;
            saved[saved_count].red = malloc(size * sizeof(unsigned short));
            saved[saved_count].green = malloc(size * sizeof(unsigned short));
            saved[saved_count].blue = malloc(size * sizeof(unsigned short));
            memcpy(saved[saved_count].red, gamma->red, size * sizeof(unsigned short));
            memcpy(saved[saved_count].green, gamma->green, size * sizeof(unsigned short));
            memcpy(saved[saved_count].blue, gamma->blue, size * sizeof(unsigned short));

            // Inversion
            for (int j = 0; j < size; j++) {
                gamma->red[j] = 65535 - gamma->red[j];
                gamma->green[j] = 65535 - gamma->green[j];
                gamma->blue[j] = 65535 - gamma->blue[j];
            }
            
            XRRSetCrtcGamma(dpy, crtc, gamma);
            XRRFreeGamma(gamma);
            saved_count++;
        }
    }

    // Attendre 15 secondes (pendant lesquelles l'utilisateur peut interagir normalement)
    sleep(15);

    // Restaurer
    for (int i = 0; i < saved_count; i++) {
        XRRCrtcGamma *gamma = XRRAllocGamma(saved[i].size);
        if (gamma) {
            memcpy(gamma->red, saved[i].red, saved[i].size * sizeof(unsigned short));
            memcpy(gamma->green, saved[i].green, saved[i].size * sizeof(unsigned short));
            memcpy(gamma->blue, saved[i].blue, saved[i].size * sizeof(unsigned short));
            XRRSetCrtcGamma(dpy, saved[i].crtc, gamma);
            XRRFreeGamma(gamma);
        }
        free(saved[i].red);
        free(saved[i].green);
        free(saved[i].blue);
    }

    free(saved);
    XRRFreeScreenResources(res);
    XCloseDisplay(dpy);
}

void execute_invert(void) {
    pid_t pid = fork();
    if (pid == 0) {
        show_invert();
        exit(0);
    }
}

// --- NYAN CAT (RAINBOW MOUSE TRAIL) ---
static void show_nyancat() {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return;
    
    int screen = DefaultScreen(dpy);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);
    Window root = RootWindow(dpy, screen);

    Visual *visual;
    int depth;
    // Utiliser la même fonction create_overlay_window que pour les autres effets
    Window win = create_overlay_window(dpy, width, height, &visual, &depth);
    
    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    
    // Télécharger et charger le sprite Nyan Cat
    char filepath[512];
    char *username = get_username();
    snprintf(filepath, sizeof(filepath), "/home/%s/.cache/wallchange_nyan.png", username);
    free(username);
    
    // Créer dossier cache sil nexiste pas
    char *dir = strdup(filepath);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    mkdir(dir, 0755);
    free(dir);
    
    if (access(filepath, F_OK) == -1) {
        download_image("https://upload.wikimedia.org/wikipedia/en/e/ed/Nyan_cat_250px_frame.PNG", filepath);
    }

    int img_w = 0, img_h = 0;
    XImage *cat_img = NULL;
    unsigned char *cat_bgra = NULL;
    
    int w, h, c;
    unsigned char *file_data = stbi_load(filepath, &w, &h, &c, 4);
    if (file_data) {
        // Resize plus petit (40px hauteur)
        float ratio = (float)w / h;
        int target_h = 80;
        int target_w = (int)(target_h * ratio);
        
        unsigned char *resized = resize_image(file_data, w, h, 4, target_w, target_h);
        stbi_image_free(file_data);
        
        if (resized) {
            img_w = target_w;
            img_h = target_h;
            cat_bgra = rgba_to_bgra(resized, img_w, img_h); // Utiliser la fonction existante
            free(resized);
            
            // Correction CRITIQUE: Utiliser 'visual' et 'depth' de create_overlay_window
            // sinon on risque un BadMatch si la fenêtre a une profondeur différente (ex: 32 vs 24)
            cat_img = XCreateImage(dpy, visual, depth,
                                   ZPixmap, 0, (char *)cat_bgra, img_w, img_h, 32, 0);
        }
    }
    
    GC gc = XCreateGC(dpy, win, 0, NULL);
    
    // Rainbow colors for the trail
    unsigned long rainbow[] = {
        0xFF0000, 0xFF7F00, 0xFFFF00, 0x00FF00, 0x0000FF, 0x4B0082, 0x9400D3
    };
    int rainbow_h = 6;
    
    // Augmenter la capacité pour l'interpolation (15s de trail fluide)
    #define TRAIL_LEN 5000
    XPoint points[TRAIL_LEN];
    // Initialiser hors écran
    for(int i=0; i<TRAIL_LEN; i++) { points[i].x = -10000; points[i].y = -10000; }
    
    int head = 0;
    int point_count = 0;
    
    // Pour l'interpolation
    int prev_x = -1, prev_y = -1;

    time_t start = time(NULL);
    // Boucle de 15 secondes
    while (time(NULL) - start < 15) {
        XClearWindow(dpy, win);
        
        Window root_ret, child_ret;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;
        
        if (XQueryPointer(dpy, root, &root_ret, &child_ret, &root_x, &root_y, &win_x, &win_y, &mask)) {
            
            // Interpolation pour combler les trous si la souris bouge vite
            if (prev_x != -1) {
                float dx = root_x - prev_x;
                float dy = root_y - prev_y;
                float dist = sqrt(dx*dx + dy*dy);
                
                // On ajoute un point tous les 5 pixels environ
                int steps = (int)(dist / 5.0f); 
                if (steps < 1) steps = 1;
                
                for (int s = 1; s <= steps; s++) {
                    float t = (float)s / steps;
                    points[head].x = (short)(prev_x + dx * t);
                    points[head].y = (short)(prev_y + dy * t);
                    head = (head + 1) % TRAIL_LEN;
                    if (point_count < TRAIL_LEN) point_count++;
                }
            } else {
                // Premier point
                points[head].x = root_x;
                points[head].y = root_y;
                head = (head + 1) % TRAIL_LEN;
                if (point_count < TRAIL_LEN) point_count++;
            }
            
            prev_x = root_x;
            prev_y = root_y;
            
            // Draw Trail
            for (int k = 0; k < 6; k++) { // 6 colors
                XSetForeground(dpy, gc, rainbow[k]);
                
                // Dessiner tous les points accumulés
                for (int i = 0; i < point_count; i++) {
                    // On remonte le temps depuis head
                    // head pointe vers la prochaine case vide, donc head-1 est le dernier ajouté
                    int idx = (head - 1 - i + TRAIL_LEN) % TRAIL_LEN;
                    
                    if (points[idx].x < -5000) continue;

                    int x = points[idx].x - (img_w/2) - 10; // Offset derrière le chat
                    
                    // Oscillation de la queue 
                    // i représente l'index temporel (plus i est grand, plus c'est vieux)
                    // On divise i par (steps/frames) pour avoir une fréquence stable visualement ?
                    // Simplifions : oscillation tous les X points. Si on a 5px par point, 
                    // une onde de nyan cat fait env 40-50px. Donc cycle de 10 points.
                    int wave_phase = (i / 8) % 2; 
                    int wave = (wave_phase == 0) ? 3 : -3;
                    
                    int y = points[idx].y + (k * rainbow_h) - (img_h/2) + wave;
                    
                    // Rectangle un peu plus large (6px) pour bien recouvrir l'espace de 5px entre points
                    XFillRectangle(dpy, win, gc, x, y, 8, rainbow_h);
                }
            }
            
            // Draw Cat
            if (cat_img) {
                XPutImage(dpy, win, gc, cat_img, 0, 0, root_x - img_w/2, root_y - img_h/2, img_w, img_h);
            }
        }
        
        XFlush(dpy);
        usleep(16000); // 60 FPS pour plus de fluidité
    }
    
    if (cat_img) {
        cat_img->data = NULL;
        XDestroyImage(cat_img);
        free(cat_bgra);
    }
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

void execute_nyancat(void) {
    pid_t pid = fork();
    if (pid == 0) {
        show_nyancat();
        exit(0);
    }
}

// --- THE FLY (LA MOUCHE) ---
static void show_fly() {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    // Utilisation d'une petite fenêtre shaped (forme irrégulière) plutôt qu'un overlay plein écran
    // C'est plus léger et permet de cliquer à côté
    int screen = DefaultScreen(dpy);
    
    // Télécharger sprite Mouche
    char filepath[512];
    char *username = get_username();
    snprintf(filepath, sizeof(filepath), "/home/%s/.cache/wallchange_fly.png", username);
    free(username);
    
    char *dir = strdup(filepath);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    mkdir(dir, 0755);
    free(dir);

    // PNG transparent d'une mouche vue de dessus
    if (access(filepath, F_OK) == -1) {
        download_image("https://pngimg.com/uploads/fly/fly_PNG3954.png", filepath);
    }
    
    int w, h, c;
    unsigned char *file_data = stbi_load(filepath, &w, &h, &c, 4);
    if (!file_data) {
        XCloseDisplay(dpy);
        return;
    }
    
    // Resize 
    int size = 50; 
    unsigned char *resized = resize_image(file_data, w, h, 4, size, size);
    stbi_image_free(file_data);
    w = size; h = size;

    // Création Pixmap et Masque pour Shape
    Pixmap pm = XCreatePixmap(dpy, RootWindow(dpy, screen), w, h, DefaultDepth(dpy, screen));
    Pixmap mask = XCreatePixmap(dpy, RootWindow(dpy, screen), w, h, 1);
    
    GC gc_pm = XCreateGC(dpy, pm, 0, NULL);
    GC gc_mask = XCreateGC(dpy, mask, 0, NULL);
    
    XImage *img = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
                               ZPixmap, 0, NULL, w, h, 32, 0);
    img->data = malloc(w * h * 4);
    
    // Copier RGBA vers Pixmap (BGRA) et Masque (1bit)
    for(int y=0; y<h; y++) {
        for(int x=0; x<w; x++) {
            int idx = (y*w + x)*4;
            unsigned char r = resized[idx];
            unsigned char g = resized[idx+1];
            unsigned char b = resized[idx+2];
            unsigned char a = resized[idx+3];
            
            // Masque
            if (a > 128) XSetForeground(dpy, gc_mask, 1);
            else XSetForeground(dpy, gc_mask, 0);
            XDrawPoint(dpy, mask, gc_mask, x, y);
            
            // Image
            unsigned long pixel = (a << 24) | (r << 16) | (g << 8) | b;
            XPutPixel(img, x, y, pixel);
        }
    }
    free(resized); // plus besoin de resized brut
    
    XPutImage(dpy, pm, gc_pm, img, 0, 0, 0, 0, w, h);
    XDestroyImage(img); // avec free data
    
    // Fenêtre
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixmap = pm; // Utiliser le pixmap comme fond direct
    
    // Position initiale aléatoire
    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);
    float px = rand() % (sw - w);
    float py = rand() % (sh - h);
    
    Window win = XCreateWindow(dpy, RootWindow(dpy, screen), px, py, w, h, 0, 
                               DefaultDepth(dpy, screen), InputOutput, 
                               DefaultVisual(dpy, screen), 
                               CWOverrideRedirect | CWBackPixmap, &attrs);
                               
    // Appliquer le masque de forme
    XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, mask, ShapeSet);
    
    // Laisser passer les clics (optionnel, mais mieux pour une mouche)
    make_window_input_transparent(dpy, win);
    
    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    
    // Animation Loop
    time_t start = time(NULL);
    float angle = (rand()%360) * 3.14 / 180.0;
    float speed = 0;
    int state = 0; // 0: marche, 1: pause, 2: rotate
    int timer = 0;
    
    while(time(NULL) - start < 20) {
        if (timer <= 0) {
            // Changer d'état
            state = rand() % 3;
            timer = 20 + rand() % 50; // frames
            if (state == 0) speed = 3 + (rand()%5);
            else speed = 0;
        }
        
        if (state == 0) { // Marche
             px += cos(angle) * speed;
             py += sin(angle) * speed;
             
             // Bords
             if (px < 0 || px > sw - w || py < 0 || py > sh - h) {
                 angle += 3.14; // Demi-tour
                 px += cos(angle) * speed * 2;
                 py += sin(angle) * speed * 2;
             }
             
             // Wobble
             if (rand()%10 == 0) angle += ((rand()%100)/100.0 - 0.5);
             
             XMoveWindow(dpy, win, (int)px, (int)py);
             XRaiseWindow(dpy, win); // Toujours au dessus
        } else if (state == 2) { // Rotate (changement direction)
            angle += ((rand()%100)/100.0 - 0.5);
        }
        // State 1 = Pause (frotte les pattes - animation difficile sans sprite sheet, juste immobile ici)
        
        timer--;
        XFlush(dpy);
        usleep(20000);
    }
    
    XFreePixmap(dpy, pm);
    XFreePixmap(dpy, mask);
    XFreeGC(dpy, gc_pm);
    XFreeGC(dpy, gc_mask);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

void execute_screen_off(int duration) {
    if (duration <= 0) duration = 3;
    
    pid_t pid = fork();
    if (pid == 0) {
        printf("Turning screen off for %d seconds\n", duration);
        int ret = system("xset dpms force off");
        (void)ret;
        sleep(duration);
        ret = system("xset dpms force on");
        (void)ret;
        ret = system("xset s reset");
        (void)ret;
        exit(0);
    }
}

void execute_fly(void) {
    pid_t pid = fork();
    if (pid == 0) {
        srand(time(NULL) ^ getpid());
        show_fly();
        exit(0);
    }
}
