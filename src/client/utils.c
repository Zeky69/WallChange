#include "client/utils.h"
#include "client/wallpaper.h"
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

// Redimensionne une image RGBA
static unsigned char* resize_image_rgba(const unsigned char *src, int src_w, int src_h, int dst_w, int dst_h) {
    unsigned char *dst = malloc(dst_w * dst_h * 4);
    if (!dst) return NULL;
    
    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            // Échantillonnage simple (nearest neighbor)
            int src_x = x * src_w / dst_w;
            int src_y = y * src_h / dst_h;
            
            int src_idx = (src_y * src_w + src_x) * 4;
            int dst_idx = (y * dst_w + x) * 4;
            
            dst[dst_idx + 0] = src[src_idx + 0]; // R
            dst[dst_idx + 1] = src[src_idx + 1]; // G
            dst[dst_idx + 2] = src[src_idx + 2]; // B
            dst[dst_idx + 3] = src[src_idx + 3]; // A
        }
    }
    return dst;
}

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
    unsigned char *img = resize_image_rgba(orig_img, orig_width, orig_height, img_width, img_height);
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
        img = resize_image_rgba(cursor_rgba, cursor_width, cursor_height, CLONE_SIZE, CLONE_SIZE);
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

// ============================================================================
// PONG GAME - Jeu de Pong en réseau avec écrans combinés
// ============================================================================

#include <X11/keysym.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

// Configuration du jeu
#define PONG_PORT 9999
#define PONG_PADDLE_WIDTH 20
#define PONG_PADDLE_HEIGHT 120
#define PONG_BALL_SIZE 20
#define PONG_PADDLE_SPEED 15
#define PONG_BALL_SPEED_X 8
#define PONG_BALL_SPEED_Y 5
#define PONG_GAME_DURATION 120  // Durée en secondes
#define PONG_FPS 60
#define PONG_MARGIN 40  // Marge depuis le bord de l'écran
#define PONG_SCORE_WIDTH 180
#define PONG_SCORE_HEIGHT 50
#define PONG_INTERPOLATION_SPEED 0.3f  // Vitesse d'interpolation (0-1)

// Structure pour les données du jeu
typedef struct {
    // Position de la balle (coordonnées virtuelles sur les 2 écrans combinés)
    // Le terrain va de 0 à total_width (= 2 * screen_width)
    float ball_x, ball_y;
    float ball_vx, ball_vy;
    
    // Position cible pour interpolation (côté client)
    float ball_target_x, ball_target_y;
    
    // Position des raquettes (y seulement, x est fixe)
    float paddle_left_y;
    float paddle_right_y;
    
    // Scores
    int score_left;
    int score_right;
    
    // Dimensions du terrain virtuel (2 écrans)
    int total_width;
    int screen_height;
    int single_screen_width;
    
    // Côté local (1 = gauche/serveur, 0 = droite/client)
    int is_left_side;
    
    // Synchronisation
    unsigned int frame_count;
} PongGame;

// Message réseau pour synchronisation
typedef struct {
    float ball_x, ball_y;
    float ball_vx, ball_vy;
    float paddle_y;  // Position de la raquette de l'expéditeur
    int score_left, score_right;
    int game_active;
    unsigned int frame_count;  // Pour synchronisation
    int ball_owner;  // Qui contrôle la balle (0=gauche, 1=droite)
} PongNetMessage;

// Détermine le côté basé sur le hostname
// Format hostname: k0r4p6 (lettre + numéro pour chaque type de machine)
// La position de la machine locale détermine le côté:
// Position paire = GAUCHE, impaire = DROITE
// Retourne 1 pour gauche, 0 pour droite
static int determine_side_from_hostname() {
    char *hostname = get_hostname();
    
    if (hostname && hostname[0] != '\0') {
        // Le hostname est du style "k0r4p6"
        // On cherche notre type de machine et son numéro
        // Pour simplifier: on prend le premier numéro trouvé après la première lettre
        
        int position = -1;
        
        // Parcourir le hostname pour trouver le premier chiffre
        for (int i = 0; hostname[i]; i++) {
            if (hostname[i] >= '0' && hostname[i] <= '9') {
                position = hostname[i] - '0';
                break;
            }
        }
        
        if (position >= 0) {
            // Position paire (0, 2, 4...) = GAUCHE
            // Position impaire (1, 3, 5...) = DROITE
            int is_left = (position % 2 == 0);
            printf("🏓 Hostname '%s' (position=%d) -> côté %s\n", 
                   hostname, position, is_left ? "GAUCHE" : "DROITE");
            return is_left;
        }
        
        // Fallback si pas de numéro trouvé
        printf("🏓 Hostname '%s' -> pas de position trouvée, défaut GAUCHE\n", hostname);
        return 1;
    }
    return 1; // Par défaut gauche
}

// Crée un socket UDP non-bloquant
static int create_udp_socket(int port, int is_server) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    // Rendre le socket non-bloquant
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    // Permettre la réutilisation de l'adresse
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if (is_server) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            close(sock);
            return -1;
        }
    }
    
    return sock;
}

// Crée un pixmap rectangulaire plein (pour raquette)
static Pixmap create_rect_mask(Display *dpy, Window root, int w, int h) {
    Pixmap mask = XCreatePixmap(dpy, root, w, h, 1);
    GC gc = XCreateGC(dpy, mask, 0, NULL);
    XSetForeground(dpy, gc, 1);
    XFillRectangle(dpy, mask, gc, 0, 0, w, h);
    XFreeGC(dpy, gc);
    return mask;
}

// Crée un pixmap circulaire (pour balle)
static Pixmap create_circle_mask(Display *dpy, Window root, int size) {
    Pixmap mask = XCreatePixmap(dpy, root, size, size, 1);
    GC gc = XCreateGC(dpy, mask, 0, NULL);
    
    // Remplir avec 0 (transparent)
    XSetForeground(dpy, gc, 0);
    XFillRectangle(dpy, mask, gc, 0, 0, size, size);
    
    // Dessiner le cercle
    XSetForeground(dpy, gc, 1);
    XFillArc(dpy, mask, gc, 0, 0, size, size, 0, 360*64);
    
    XFreeGC(dpy, gc);
    return mask;
}

// Réinitialise la balle au centre du terrain virtuel
static void reset_ball(PongGame *game) {
    // Centre du terrain = screen_width (la jonction des deux écrans)
    game->ball_x = game->single_screen_width;  // Au milieu exact
    game->ball_y = game->screen_height / 2.0f;
    game->ball_target_x = game->ball_x;
    game->ball_target_y = game->ball_y;
    
    // Direction aléatoire
    game->ball_vx = (rand() % 2 == 0) ? PONG_BALL_SPEED_X : -PONG_BALL_SPEED_X;
    game->ball_vy = ((rand() % 60) - 30) / 10.0f;  // Entre -3 et +3
}

// Met à jour la physique du jeu - SEULEMENT appelé par le serveur (côté gauche)
static void update_pong_physics_server(PongGame *game) {
    // Déplacer la balle
    game->ball_x += game->ball_vx;
    game->ball_y += game->ball_vy;
    
    // Rebond sur les bords haut/bas
    if (game->ball_y <= PONG_BALL_SIZE/2) {
        game->ball_y = PONG_BALL_SIZE/2;
        game->ball_vy = fabsf(game->ball_vy);
    }
    if (game->ball_y >= game->screen_height - PONG_BALL_SIZE/2) {
        game->ball_y = game->screen_height - PONG_BALL_SIZE/2;
        game->ball_vy = -fabsf(game->ball_vy);
    }
    
    // Collision avec la raquette gauche (à x = PONG_MARGIN)
    float left_paddle_x = PONG_MARGIN + PONG_PADDLE_WIDTH;
    if (game->ball_vx < 0 && game->ball_x <= left_paddle_x + PONG_BALL_SIZE/2 && game->ball_x > PONG_MARGIN) {
        if (game->ball_y >= game->paddle_left_y - PONG_BALL_SIZE/2 && 
            game->ball_y <= game->paddle_left_y + PONG_PADDLE_HEIGHT + PONG_BALL_SIZE/2) {
            game->ball_x = left_paddle_x + PONG_BALL_SIZE/2;
            game->ball_vx = fabsf(game->ball_vx) * 1.03f;
            
            float hit_pos = (game->ball_y - game->paddle_left_y) / PONG_PADDLE_HEIGHT;
            game->ball_vy += (hit_pos - 0.5f) * 4;
        }
    }
    
    // Collision avec la raquette droite (à x = total_width - PONG_MARGIN - PONG_PADDLE_WIDTH)
    float right_paddle_x = game->total_width - PONG_MARGIN - PONG_PADDLE_WIDTH;
    if (game->ball_vx > 0 && game->ball_x >= right_paddle_x - PONG_BALL_SIZE/2 && game->ball_x < game->total_width - PONG_MARGIN) {
        if (game->ball_y >= game->paddle_right_y - PONG_BALL_SIZE/2 && 
            game->ball_y <= game->paddle_right_y + PONG_PADDLE_HEIGHT + PONG_BALL_SIZE/2) {
            game->ball_x = right_paddle_x - PONG_BALL_SIZE/2;
            game->ball_vx = -fabsf(game->ball_vx) * 1.03f;
            
            float hit_pos_r = (game->ball_y - game->paddle_right_y) / PONG_PADDLE_HEIGHT;
            game->ball_vy += (hit_pos_r - 0.5f) * 4;
        }
    }
    
    // Limiter la vitesse
    if (game->ball_vx > 20) game->ball_vx = 20;
    if (game->ball_vx < -20) game->ball_vx = -20;
    if (game->ball_vy > 12) game->ball_vy = 12;
    if (game->ball_vy < -12) game->ball_vy = -12;
    
    // But côté gauche (joueur gauche perd)
    if (game->ball_x < 0) {
        game->score_right++;
        reset_ball(game);
    }
    
    // But côté droit (joueur droit perd)
    if (game->ball_x > game->total_width) {
        game->score_left++;
        reset_ball(game);
    }
}

// Interpolation côté client - met à jour la position visible vers la cible
static void update_pong_interpolation(PongGame *game) {
    game->ball_x += (game->ball_target_x - game->ball_x) * PONG_INTERPOLATION_SPEED;
    game->ball_y += (game->ball_target_y - game->ball_y) * PONG_INTERPOLATION_SPEED;
}

// Fonction principale du jeu Pong avec fenêtres transparentes
static void run_pong_game(const char *opponent_ip, int is_left_side) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        printf("Erreur ouverture display X11\n");
        return;
    }

    int screen = DefaultScreen(dpy);
    int screen_width = DisplayWidth(dpy, screen);
    int screen_height = DisplayHeight(dpy, screen);
    Window root = RootWindow(dpy, screen);

    // Initialiser le jeu
    PongGame game;
    memset(&game, 0, sizeof(game));
    game.single_screen_width = screen_width;
    game.total_width = screen_width * 2;  // Deux écrans combinés
    game.screen_height = screen_height;
    game.is_left_side = is_left_side;
    game.paddle_left_y = (screen_height - PONG_PADDLE_HEIGHT) / 2.0f;
    game.paddle_right_y = (screen_height - PONG_PADDLE_HEIGHT) / 2.0f;
    
    reset_ball(&game);

    // Créer les masques de forme
    Pixmap paddle_mask = create_rect_mask(dpy, root, PONG_PADDLE_WIDTH, PONG_PADDLE_HEIGHT);
    Pixmap ball_mask = create_circle_mask(dpy, root, PONG_BALL_SIZE);
    Pixmap score_mask = create_rect_mask(dpy, root, PONG_SCORE_WIDTH, PONG_SCORE_HEIGHT);

    // Attributs communs pour les fenêtres
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = 0;

    // Créer la fenêtre de la raquette locale
    Window paddle_win = XCreateWindow(dpy, root,
                                      0, 0, PONG_PADDLE_WIDTH, PONG_PADDLE_HEIGHT,
                                      0, CopyFromParent, InputOutput, CopyFromParent,
                                      CWOverrideRedirect | CWBackPixel, &attrs);
    XShapeCombineMask(dpy, paddle_win, ShapeBounding, 0, 0, paddle_mask, ShapeSet);

    // Créer la fenêtre de la balle
    Window ball_win = XCreateWindow(dpy, root,
                                    0, 0, PONG_BALL_SIZE, PONG_BALL_SIZE,
                                    0, CopyFromParent, InputOutput, CopyFromParent,
                                    CWOverrideRedirect | CWBackPixel, &attrs);
    XShapeCombineMask(dpy, ball_win, ShapeBounding, 0, 0, ball_mask, ShapeSet);

    // Créer la fenêtre du score
    Window score_win = XCreateWindow(dpy, root,
                                     (screen_width - PONG_SCORE_WIDTH) / 2, 20,
                                     PONG_SCORE_WIDTH, PONG_SCORE_HEIGHT,
                                     0, CopyFromParent, InputOutput, CopyFromParent,
                                     CWOverrideRedirect | CWBackPixel, &attrs);
    XShapeCombineMask(dpy, score_win, ShapeBounding, 0, 0, score_mask, ShapeSet);

    // Créer les GC pour dessiner
    GC paddle_gc = XCreateGC(dpy, paddle_win, 0, NULL);
    GC ball_gc = XCreateGC(dpy, ball_win, 0, NULL);
    GC score_gc = XCreateGC(dpy, score_win, 0, NULL);

    // Couleurs
    XColor white, yellow, green, exact;
    Colormap cmap = DefaultColormap(dpy, screen);
    XAllocNamedColor(dpy, cmap, "white", &white, &exact);
    XAllocNamedColor(dpy, cmap, "yellow", &yellow, &exact);
    XAllocNamedColor(dpy, cmap, "lime green", &green, &exact);

    // Charger une police plus grande pour le score
    XFontStruct *font = XLoadQueryFont(dpy, "-*-helvetica-bold-r-*-*-34-*-*-*-*-*-*-*");
    if (!font) {
        font = XLoadQueryFont(dpy, "fixed");
    }
    if (font) {
        XSetFont(dpy, score_gc, font->fid);
    }

    // Mapper les fenêtres
    XMapWindow(dpy, paddle_win);
    XMapWindow(dpy, ball_win);
    XMapWindow(dpy, score_win);
    XRaiseWindow(dpy, paddle_win);
    XRaiseWindow(dpy, ball_win);
    XRaiseWindow(dpy, score_win);
    XFlush(dpy);

    // Créer les sockets UDP
    int recv_sock = create_udp_socket(PONG_PORT, 1);
    int send_sock = create_udp_socket(0, 0);
    
    if (recv_sock < 0 || send_sock < 0) {
        printf("Erreur création sockets\n");
        XDestroyWindow(dpy, paddle_win);
        XDestroyWindow(dpy, ball_win);
        XDestroyWindow(dpy, score_win);
        XCloseDisplay(dpy);
        return;
    }
    
    // Configurer l'adresse de l'adversaire
    struct sockaddr_in opponent_addr;
    memset(&opponent_addr, 0, sizeof(opponent_addr));
    opponent_addr.sin_family = AF_INET;
    opponent_addr.sin_port = htons(PONG_PORT);
    
    // Résoudre le hostname de l'adversaire
    struct hostent *he = gethostbyname(opponent_ip);
    if (he) {
        memcpy(&opponent_addr.sin_addr, he->h_addr_list[0], he->h_length);
    } else {
        inet_pton(AF_INET, opponent_ip, &opponent_addr.sin_addr);
    }

    printf("🏓 Pong démarré ! Côté: %s, Adversaire: %s\n", 
           is_left_side ? "GAUCHE" : "DROITE", opponent_ip);
    printf("⌨️  Utilisez les touches ↑/↓ ou Z/S pour déplacer la raquette\n");
    printf("⏱️  Durée: %d secondes | Appuyez sur Q ou Echap pour quitter\n", PONG_GAME_DURATION);

    // Variables pour le timing
    time_t start_time = time(NULL);
    
    // États des touches - utiliser XQueryKeymap au lieu des événements
    int key_up = 0, key_down = 0;
    
    // Boucle principale
    int running = 1;
    while (running) {
        struct timeval frame_start, frame_end;
        gettimeofday(&frame_start, NULL);
        
        // Vérifier le temps écoulé
        if (time(NULL) - start_time >= PONG_GAME_DURATION) {
            running = 0;
            break;
        }
        
        // Lire l'état du clavier directement
        char keys[32];
        XQueryKeymap(dpy, keys);
        
        // Codes des touches (peuvent varier selon le clavier)
        KeyCode kc_up = XKeysymToKeycode(dpy, XK_Up);
        KeyCode kc_down = XKeysymToKeycode(dpy, XK_Down);
        KeyCode kc_z = XKeysymToKeycode(dpy, XK_z);
        KeyCode kc_s = XKeysymToKeycode(dpy, XK_s);
        KeyCode kc_w = XKeysymToKeycode(dpy, XK_w);
        KeyCode kc_q = XKeysymToKeycode(dpy, XK_q);
        KeyCode kc_esc = XKeysymToKeycode(dpy, XK_Escape);
        
        key_up = (keys[kc_up/8] & (1 << (kc_up%8))) || 
                 (keys[kc_z/8] & (1 << (kc_z%8))) ||
                 (keys[kc_w/8] & (1 << (kc_w%8)));
        key_down = (keys[kc_down/8] & (1 << (kc_down%8))) || 
                   (keys[kc_s/8] & (1 << (kc_s%8)));
        
        // Quitter avec Q ou Echap
        if ((keys[kc_q/8] & (1 << (kc_q%8))) || (keys[kc_esc/8] & (1 << (kc_esc%8)))) {
            running = 0;
            break;
        }
        
        // Déplacer la raquette locale
        float *local_paddle = is_left_side ? &game.paddle_left_y : &game.paddle_right_y;
        if (key_up) {
            *local_paddle -= PONG_PADDLE_SPEED;
            if (*local_paddle < 0) *local_paddle = 0;
        }
        if (key_down) {
            *local_paddle += PONG_PADDLE_SPEED;
            if (*local_paddle > screen_height - PONG_PADDLE_HEIGHT)
                *local_paddle = screen_height - PONG_PADDLE_HEIGHT;
        }
        
        // Recevoir les données de l'adversaire
        PongNetMessage recv_msg;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        ssize_t received = recvfrom(recv_sock, &recv_msg, sizeof(recv_msg), 0,
                                    (struct sockaddr*)&from_addr, &from_len);
        
        if (received == sizeof(PongNetMessage)) {
            if (is_left_side) {
                // GAUCHE (serveur) : récupère seulement la position de la raquette droite
                game.paddle_right_y = recv_msg.paddle_y;
            } else {
                // DROITE (client) : récupère la position de la raquette gauche
                game.paddle_left_y = recv_msg.paddle_y;
                // Et les cibles de la balle pour interpolation
                game.ball_target_x = recv_msg.ball_x;
                game.ball_target_y = recv_msg.ball_y;
                game.ball_vx = recv_msg.ball_vx;
                game.ball_vy = recv_msg.ball_vy;
                game.score_left = recv_msg.score_left;
                game.score_right = recv_msg.score_right;
            }
            
            if (!recv_msg.game_active) {
                running = 0;
            }
        }
        
        // Mise à jour de la physique
        if (is_left_side) {
            // GAUCHE est le serveur - calcule la physique
            update_pong_physics_server(&game);
        } else {
            // DROITE est le client - interpole vers la position cible
            update_pong_interpolation(&game);
        }
        
        // Envoyer les données à l'adversaire
        PongNetMessage send_msg;
        send_msg.ball_x = game.ball_x;
        send_msg.ball_y = game.ball_y;
        send_msg.ball_vx = game.ball_vx;
        send_msg.ball_vy = game.ball_vy;
        send_msg.paddle_y = *local_paddle;
        send_msg.score_left = game.score_left;
        send_msg.score_right = game.score_right;
        send_msg.game_active = running;
        send_msg.ball_owner = is_left_side ? 0 : 1;
        send_msg.frame_count = game.frame_count++;
        
        sendto(send_sock, &send_msg, sizeof(send_msg), 0,
               (struct sockaddr*)&opponent_addr, sizeof(opponent_addr));
        
        // Calculer les positions locales
        int local_offset = is_left_side ? 0 : game.single_screen_width;
        
        // Positionner la raquette locale
        int paddle_x;
        if (is_left_side) {
            paddle_x = PONG_MARGIN;
        } else {
            paddle_x = screen_width - PONG_MARGIN - PONG_PADDLE_WIDTH;
        }
        XMoveWindow(dpy, paddle_win, paddle_x, (int)*local_paddle);
        
        // Dessiner la raquette (blanc)
        XSetForeground(dpy, paddle_gc, white.pixel);
        XFillRectangle(dpy, paddle_win, paddle_gc, 0, 0, PONG_PADDLE_WIDTH, PONG_PADDLE_HEIGHT);
        
        // Positionner et dessiner la balle
        int ball_screen_x = (int)game.ball_x - local_offset - PONG_BALL_SIZE/2;
        int ball_screen_y = (int)game.ball_y - PONG_BALL_SIZE/2;
        
        // Afficher/cacher la balle selon sa position
        if (ball_screen_x >= -PONG_BALL_SIZE && ball_screen_x < screen_width) {
            XMoveWindow(dpy, ball_win, ball_screen_x, ball_screen_y);
            XMapWindow(dpy, ball_win);
            XSetForeground(dpy, ball_gc, yellow.pixel);
            XFillArc(dpy, ball_win, ball_gc, 0, 0, PONG_BALL_SIZE, PONG_BALL_SIZE, 0, 360*64);
        } else {
            XUnmapWindow(dpy, ball_win);
        }
        
        // Dessiner le score
        XSetForeground(dpy, score_gc, green.pixel);
        XFillRectangle(dpy, score_win, score_gc, 0, 0, PONG_SCORE_WIDTH, PONG_SCORE_HEIGHT);
        XSetForeground(dpy, score_gc, WhitePixel(dpy, screen));
        
        char score_text[32];
        snprintf(score_text, sizeof(score_text), "%d  -  %d", game.score_left, game.score_right);
        int text_width = XTextWidth(font, score_text, strlen(score_text));
        XDrawString(dpy, score_win, score_gc, 
                   (PONG_SCORE_WIDTH - text_width) / 2, 
                   PONG_SCORE_HEIGHT / 2 + 10, 
                   score_text, strlen(score_text));
        
        // Garder les fenêtres au premier plan
        XRaiseWindow(dpy, paddle_win);
        XRaiseWindow(dpy, ball_win);
        XRaiseWindow(dpy, score_win);
        
        XFlush(dpy);
        
        // Attendre pour maintenir ~60 FPS
        gettimeofday(&frame_end, NULL);
        long elapsed_us = (frame_end.tv_sec - frame_start.tv_sec) * 1000000 +
                         (frame_end.tv_usec - frame_start.tv_usec);
        long target_us = 1000000 / PONG_FPS;
        
        if (elapsed_us < target_us) {
            usleep(target_us - elapsed_us);
        }
    }

    // Afficher le score final
    printf("\n🏆 FIN DU JEU !\n");
    printf("📊 Score final: Gauche %d - %d Droite\n", game.score_left, game.score_right);
    
    if (is_left_side) {
        if (game.score_left > game.score_right) printf("🎉 Vous avez GAGNÉ !\n");
        else if (game.score_left < game.score_right) printf("😢 Vous avez perdu...\n");
        else printf("🤝 Match nul !\n");
    } else {
        if (game.score_right > game.score_left) printf("🎉 Vous avez GAGNÉ !\n");
        else if (game.score_right < game.score_left) printf("😢 Vous avez perdu...\n");
        else printf("🤝 Match nul !\n");
    }

    // Nettoyage
    close(recv_sock);
    close(send_sock);
    if (font) XFreeFont(dpy, font);
    XFreeGC(dpy, paddle_gc);
    XFreeGC(dpy, ball_gc);
    XFreeGC(dpy, score_gc);
    XFreePixmap(dpy, paddle_mask);
    XFreePixmap(dpy, ball_mask);
    XFreePixmap(dpy, score_mask);
    XDestroyWindow(dpy, paddle_win);
    XDestroyWindow(dpy, ball_win);
    XDestroyWindow(dpy, score_win);
    XCloseDisplay(dpy);
}

void execute_pong(const char *opponent_user, int is_left_side) {
    // Si is_left_side == -1, on le détermine automatiquement via le hostname
    if (is_left_side == -1) {
        is_left_side = determine_side_from_hostname();
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        srand(time(NULL) ^ getpid());
        run_pong_game(opponent_user, is_left_side);
        exit(0);
    }
}
