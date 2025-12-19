#ifndef AUTH_H
#define AUTH_H

#include "server.h"

// Charge les credentials admin depuis le fichier
int load_admin_credentials(void);

// Calcule le SHA256 d'une chaîne et retourne en hex
void sha256_hex(const char *str, char *output);

// Vérifie les credentials et retourne 1 si valide
int verify_admin_credentials(const char *user, const char *pass);

// Génère un token sécurisé (32 bytes = 64 caractères hex)
void generate_secure_token(char *token, size_t size);

// Valide le token Bearer dans l'en-tête Authorization (utilisateur ou admin)
int validate_bearer_token(struct mg_http_message *hm);

// Valide que le token est le token ADMIN
int validate_admin_token(struct mg_http_message *hm);

// Charge la base de données utilisateurs
void load_user_db(void);

// Vérifie ou enregistre un utilisateur
// Retourne 1 si succès (login ok ou nouvel enregistrement), 0 si échec (mauvais mot de passe)
int verify_or_register_user(const char *username, const char *password);

// Récupère le nom d'utilisateur associé au token
// Retourne "admin", l'ID du client, ou "unknown"
const char* get_user_from_token(struct mg_http_message *hm);

#endif // AUTH_H
