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
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>

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
} Confetti;

static void show_confetti(const char *img_path) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    int screen = DefaultScreen(dpy);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);
    Window root = RootWindow(dpy, screen);

    // Créer une fenêtre transparente (nécessite un compositeur pour la vraie transparence)
    // Sans compositeur, on peut copier le fond d'écran actuel
    XImage *bg = XGetImage(dpy, root, 0, 0, width, height, AllPlanes, ZPixmap);
    
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    
    Window win = XCreateWindow(dpy, root, 0, 0, width, height, 0,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWOverrideRedirect, &attrs);
                               
    // Définir le background avec l'image capturée
    Pixmap pm = XCreatePixmap(dpy, win, width, height, DefaultDepth(dpy, screen));
    GC gc_pm = XCreateGC(dpy, pm, 0, NULL);
    XPutImage(dpy, pm, gc_pm, bg, 0, 0, 0, 0, width, height);
    XSetWindowBackgroundPixmap(dpy, win, pm);
    XFreeGC(dpy, gc_pm);
    XDestroyImage(bg);

    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    XFlush(dpy);

    GC gc = XCreateGC(dpy, win, 0, NULL);
    
    int num_confetti = 200;
    Confetti *parts = malloc(sizeof(Confetti) * num_confetti);
    unsigned long colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    
    for (int i = 0; i < num_confetti; i++) {
        parts[i].x = rand() % width;
        parts[i].y = rand() % height - height; // Commencer au dessus
        parts[i].vx = (rand() % 10 - 5) / 2.0;
        parts[i].vy = (rand() % 10 + 5);
        parts[i].color = colors[rand() % 7];
        parts[i].size = rand() % 10 + 5;
    }

    time_t start = time(NULL);
    while (time(NULL) - start < 10) {
        // Redessiner le fond (effacer les confettis précédents)
        // Pour optimiser, on pourrait redessiner juste les zones sales, mais ici on clear tout
        XClearWindow(dpy, win);
        
        for (int i = 0; i < num_confetti; i++) {
            parts[i].x += parts[i].vx;
            parts[i].y += parts[i].vy;
            
            // Gravité / Vent
            parts[i].vx += (rand() % 3 - 1) * 0.1;
            
            if (parts[i].y > height) {
                parts[i].y = -10;
                parts[i].x = rand() % width;
            }
            
            XSetForeground(dpy, gc, parts[i].color);
            XFillRectangle(dpy, win, gc, (int)parts[i].x, (int)parts[i].y, parts[i].size, parts[i].size);
        }
        
        XFlush(dpy);
        usleep(30000);
    }

    free(parts);
    XFreePixmap(dpy, pm);
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
        XSetForeground(dpy, gc_bg, 0xCC000000); // Semi-transparent black
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
    XSetLineAttributes(dpy, gc, 3, LineSolid, CapRound, JoinRound);
    
    time_t start = time(NULL);
    float t = 0;
    
    while (time(NULL) - start < 10) {
        XClearWindow(dpy, win);
        
        for (int i = 0; i < 10; i++) {
            // Draw some sine waves
            XPoint *points = malloc(sizeof(XPoint) * (width/5 + 1));
            int count = 0;
            for (int x = 0; x < width; x+=5) {
                int y = height/2 + (i - 5) * 50 + sin(x * 0.01 + t + i) * 50;
                points[count].x = x;
                points[count].y = y;
                count++;
            }
            
            // Rainbow colors
            unsigned long color = 0;
            switch(i % 6) {
                case 0: color = 0xFF0000; break;
                case 1: color = 0x00FF00; break;
                case 2: color = 0x0000FF; break;
                case 3: color = 0xFFFF00; break;
                case 4: color = 0xFF00FF; break;
                case 5: color = 0x00FFFF; break;
            }
            XSetForeground(dpy, gc, color);
            XDrawLines(dpy, win, gc, points, count, CoordModeOrigin);
            free(points);
        }
        
        XFlush(dpy);
        t += 0.2;
        usleep(30000);
    }

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
    Window root = RootWindow(dpy, screen);

    Visual *visual;
    int depth;
    Window win = create_overlay_window(dpy, width, height, &visual, &depth);

    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    
    GC gc = XCreateGC(dpy, win, 0, NULL);
    
    // Charger l'image ou utiliser un rect
    int logo_w = 100, logo_h = 100;
    float x = rand() % (width - logo_w);
    float y = rand() % (height - logo_h);
    float vx = 5.0, vy = 5.0;
    unsigned long color = 0xFF0000;
    
    // TODO: Charger image si img_path existe (via stb_image + XPutImage)
    // Pour l'instant on fait un rect coloré qui change de couleur
    
    time_t start = time(NULL);
    while (time(NULL) - start < 15) {
        XClearWindow(dpy, win); // Remet le fond (pixmap)
        
        x += vx;
        y += vy;
        
        int hit = 0;
        if (x <= 0 || x + logo_w >= width) { vx = -vx; hit = 1; }
        if (y <= 0 || y + logo_h >= height) { vy = -vy; hit = 1; }
        
        if (hit) {
            color = (rand() % 0xFFFFFF) | 0x404040; // Couleur brillante
        }
        
        XSetForeground(dpy, gc, color);
        XFillRectangle(dpy, win, gc, (int)x, (int)y, logo_w, logo_h);
        
        // Texte "DVD"
        XSetForeground(dpy, gc, 0x000000);
        XDrawString(dpy, win, gc, (int)x + 40, (int)y + 55, "DVD", 3);
        
        XFlush(dpy);
        usleep(20000);
    }

    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}

void execute_dvdbounce(const char *url) {
    // Téléchargement optionnel (non implémenté ici pour simplifier, on utilise le rect)
    pid_t pid = fork();
    if (pid == 0) {
        srand(time(NULL) ^ getpid());
        show_dvdbounce(NULL);
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
