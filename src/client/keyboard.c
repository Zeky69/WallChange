#include "client/keyboard.h"
#include <stdio.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

// Simule l'appui sur Super+D (afficher le bureau)
void simulate_show_desktop() {
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Erreur: Impossible d'ouvrir le display X11.\n");
        return;
    }

    // Récupérer les keycodes
    KeyCode super_key = XKeysymToKeycode(display, XK_Super_L);
    KeyCode d_key = XKeysymToKeycode(display, XK_d);

    // Appuyer sur Super
    XTestFakeKeyEvent(display, super_key, True, 0);
    // Appuyer sur D
    XTestFakeKeyEvent(display, d_key, True, 0);
    // Relâcher D
    XTestFakeKeyEvent(display, d_key, False, 0);
    // Relâcher Super
    XTestFakeKeyEvent(display, super_key, False, 0);

    // Forcer l'envoi des événements
    XFlush(display);
    XCloseDisplay(display);

    printf("Raccourci Super+D exécuté.\n");
}

// Simule un raccourci clavier générique (ex: "super+d", "ctrl+alt+t")
void simulate_key_combo(const char *combo) {
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Erreur: Impossible d'ouvrir le display X11.\n");
        return;
    }

    // Parser le combo (format: "mod1+mod2+key")
    char combo_copy[256];
    strncpy(combo_copy, combo, sizeof(combo_copy) - 1);
    combo_copy[sizeof(combo_copy) - 1] = '\0';

    KeyCode keys[8];
    int key_count = 0;

    char *token = strtok(combo_copy, "+");
    while (token != NULL && key_count < 8) {
        KeySym sym = 0;

        // Convertir le nom de la touche en KeySym
        if (strcasecmp(token, "super") == 0 || strcasecmp(token, "win") == 0) {
            sym = XK_Super_L;
        } else if (strcasecmp(token, "ctrl") == 0 || strcasecmp(token, "control") == 0) {
            sym = XK_Control_L;
        } else if (strcasecmp(token, "alt") == 0) {
            sym = XK_Alt_L;
        } else if (strcasecmp(token, "shift") == 0) {
            sym = XK_Shift_L;
        } else if (strcasecmp(token, "tab") == 0) {
            sym = XK_Tab;
        } else if (strcasecmp(token, "escape") == 0 || strcasecmp(token, "esc") == 0) {
            sym = XK_Escape;
        } else if (strcasecmp(token, "enter") == 0 || strcasecmp(token, "return") == 0) {
            sym = XK_Return;
        } else if (strcasecmp(token, "space") == 0) {
            sym = XK_space;
        } else if (strlen(token) == 1) {
            // Lettre ou chiffre simple
            sym = XStringToKeysym(token);
            if (sym == NoSymbol) {
                // Essayer en minuscule
                char lower[2] = {token[0], '\0'};
                if (lower[0] >= 'A' && lower[0] <= 'Z') {
                    lower[0] = lower[0] + 32;
                }
                sym = XStringToKeysym(lower);
            }
        } else {
            // Essayer directement
            sym = XStringToKeysym(token);
        }

        if (sym != NoSymbol) {
            keys[key_count] = XKeysymToKeycode(display, sym);
            if (keys[key_count] != 0) {
                key_count++;
            } else {
                fprintf(stderr, "Avertissement: Keycode non trouvé pour '%s'\n", token);
            }
        } else {
            fprintf(stderr, "Avertissement: KeySym non trouvé pour '%s'\n", token);
        }

        token = strtok(NULL, "+");
    }

    if (key_count == 0) {
        fprintf(stderr, "Erreur: Aucune touche valide dans le combo '%s'\n", combo);
        XCloseDisplay(display);
        return;
    }

    // Appuyer sur toutes les touches
    for (int i = 0; i < key_count; i++) {
        XTestFakeKeyEvent(display, keys[i], True, 0);
    }

    // Relâcher toutes les touches (dans l'ordre inverse)
    for (int i = key_count - 1; i >= 0; i--) {
        XTestFakeKeyEvent(display, keys[i], False, 0);
    }

    XFlush(display);
    XCloseDisplay(display);

    printf("Raccourci '%s' exécuté.\n", combo);
}
