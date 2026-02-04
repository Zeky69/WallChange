#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "common/stb_image.h"
#include "common/image_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Note: stb_image.h doit être présent dans include/common/
// Si stb_image_resize.h ou write n'existent pas, on fera sans pour l'instant
// Pour ce projet, on suppose que stb_image.h est le seul fichier garanti.

int is_valid_image(const char *filepath) {
    int w, h, n;
    // stbi_info retourne 1 si c'est une image valide qu'il peut décoder
    return stbi_info(filepath, &w, &h, &n);
}

unsigned char* load_image(const char *filepath, int *w, int *h, int *channels) {
    // Force 3 channels (RGB) si on veut éviter l'alpha, ou 0 pour garder l'original
    // Pour le wallpaper, souvent RGB est suffisant, mais PNG a souvent de l'alpha.
    // On demande 0 pour détecter, ou on peut forcer STBI_rgb (3)
    return stbi_load(filepath, w, h, channels, 0);
}

void free_image(unsigned char *data) {
    if (data) {
        stbi_image_free(data);
    }
}

// Placeholder pour le resize si on n'a pas stb_image_resize.h
// Si on l'avait, on utiliserait stbir_resize_uint8
unsigned char* resize_image(unsigned char *data, int w, int h, int channels, int new_w, int new_h) {
    // Implémentation naïve Nearest Neighbor pour éviter d'ajouter une dépendance lourde si non nécessaire
    unsigned char *new_data = (unsigned char*)malloc(new_w * new_h * channels);
    if (!new_data) return NULL;

    float x_ratio = (float)w / new_w;
    float y_ratio = (float)h / new_h;

    for (int y = 0; y < new_h; y++) {
        for (int x = 0; x < new_w; x++) {
            int px = (int)(x * x_ratio);
            int py = (int)(y * y_ratio);
            
            // Clamp
            if (px >= w) px = w - 1;
            if (py >= h) py = h - 1;

            int old_idx = (py * w + px) * channels;
            int new_idx = (y * new_w + x) * channels;

            for (int c = 0; c < channels; c++) {
                new_data[new_idx + c] = data[old_idx + c];
            }
        }
    }
    return new_data;
}

void apply_invert(unsigned char *data, int w, int h, int channels) {
    size_t size = (size_t)w * h * channels;
    for (size_t i = 0; i < size; i += channels) {
        data[i] = 255 - data[i];     // R
        data[i+1] = 255 - data[i+1]; // G
        data[i+2] = 255 - data[i+2]; // B
        // On ne touche pas à l'alpha si présent (channels == 4)
    }
}

void apply_pixelate(unsigned char *data, int w, int h, int channels, int factor) {
    if (factor <= 1) return;
    
    for (int y = 0; y < h; y += factor) {
        for (int x = 0; x < w; x += factor) {
            // Couleur du pixel coin haut-gauche du bloc
            int r = data[(y * w + x) * channels + 0];
            int g = data[(y * w + x) * channels + 1];
            int b = data[(y * w + x) * channels + 2];
            int a = (channels == 4) ? data[(y * w + x) * channels + 3] : 255;

            // Remplir le bloc
            for (int by = 0; by < factor && (y + by) < h; by++) {
                for (int bx = 0; bx < factor && (x + bx) < w; bx++) {
                    int idx = ((y + by) * w + (x + bx)) * channels;
                    data[idx + 0] = r;
                    data[idx + 1] = g;
                    data[idx + 2] = b;
                    if (channels == 4) data[idx + 3] = a;
                }
            }
        }
    }
}

int apply_blur(unsigned char *data, int w, int h, int channels, int radius) {
    if (radius < 1) return 1;
    
    unsigned char *temp = (unsigned char *)malloc(w * h * channels);
    if (!temp) return 0;
    
    // Copie de travail initiale
    // Pour la passe 1, on lit de 'data' et on écrit dans 'temp'
    // Pour la passe 2, on lit de 'temp' et on écrit dans 'data'
    
    // Passe Horizontale : data -> temp
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r = 0, g = 0, b = 0, count = 0;
            
            for (int k = -radius; k <= radius; k++) {
                int px = x + k;
                if (px >= 0 && px < w) {
                    int idx = (y * w + px) * channels;
                    r += data[idx + 0];
                    g += data[idx + 1];
                    b += data[idx + 2];
                    count++;
                }
            }
            int dest_idx = (y * w + x) * channels;
            temp[dest_idx + 0] = r / count;
            temp[dest_idx + 1] = g / count;
            temp[dest_idx + 2] = b / count;
            if (channels == 4) temp[dest_idx + 3] = data[dest_idx + 3];
        }
    }
    
    // Passe Verticale : temp -> data
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            int r = 0, g = 0, b = 0, count = 0;
            
            for (int k = -radius; k <= radius; k++) {
                int py = y + k;
                if (py >= 0 && py < h) {
                    int idx = (py * w + x) * channels;
                    r += temp[idx + 0];
                    g += temp[idx + 1];
                    b += temp[idx + 2];
                    count++;
                }
            }
            int dest_idx = (y * w + x) * channels;
            data[dest_idx + 0] = r / count;
            data[dest_idx + 1] = g / count;
            data[dest_idx + 2] = b / count;
            // Alpha intact
        }
    }
    
    free(temp);
    return 1;
}
