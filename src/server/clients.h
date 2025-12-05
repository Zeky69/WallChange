#ifndef CLIENTS_H
#define CLIENTS_H

#include "server.h"

// Vérifie si la cible est rate limitée
int is_target_rate_limited(const char *id);

// Cherche un client par son token
int find_client_by_token(const char *token, size_t token_len);

// Stocke les infos d'un client
void store_client_info(const char *id, const char *hostname, const char *os, 
                       const char *uptime, const char *cpu, const char *ram, 
                       const char *version);

// Génère et stocke un token unique pour un client
const char* generate_client_token(const char *id);

// Supprime un client
void remove_client(const char *id);

// Récupère les infos d'un client
struct client_info* get_client_info(const char *id);

// Vérifie si un client correspond à la cible (* = tous)
int match_target(const char *client_id, const char *target_id);

#endif // CLIENTS_H
