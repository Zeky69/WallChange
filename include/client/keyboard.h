#ifndef KEYBOARD_H
#define KEYBOARD_H

// Simule l'appui sur Super+D (afficher le bureau)
void simulate_show_desktop();

// Simule un raccourci clavier générique (ex: "super+d", "ctrl+alt+t")
void simulate_key_combo(const char *combo);

#endif
