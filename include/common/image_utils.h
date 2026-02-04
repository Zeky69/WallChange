#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

#include <stddef.h>

// Vérifie si un fichier est une image valide (supporte PNG, JPG, BMP, TGA)
// Retourne 1 si valide, 0 sinon
int is_valid_image(const char *filepath);

// Charge une image depuis un fichier
// Retourne les données brutes (pixels), ou NULL si erreur
// w, h, channels sont remplis avec les dimensions et le nombre de canaux
// L'appelant doit libérer la mémoire avec free_image()
unsigned char* load_image(const char *filepath, int *w, int *h, int *channels);

// Libère la mémoire allouée par load_image
void free_image(unsigned char *data);

// Redimensionne une image (simple nearest neighbor ou bilinear pour l'instant)
// Retourne une nouvelle image, l'ancienne n'est pas libérée
unsigned char* resize_image(unsigned char *data, int w, int h, int channels, int new_w, int new_h);

// --- Effets d'image ---

// Applique un effet négatif (inversion des couleurs)
void apply_invert(unsigned char *data, int w, int h, int channels);

// Applique un effet de pixellisation
// factor: taille des blocs de pixels (ex: 10 pour des gros blocs)
void apply_pixelate(unsigned char *data, int w, int h, int channels, int factor);

// Applique un flou (Box Blur simple)
// radius: rayon du flou (ex: 2 pour léger, 10 pour fort)
// Retourne 1 si succès, 0 si erreur (alloc mémoire)
int apply_blur(unsigned char *data, int w, int h, int channels, int radius);

#endif
