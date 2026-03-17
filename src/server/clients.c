#include "clients.h"
#include "auth.h"
#include <string.h>

static int secure_mem_eq(const char *left, size_t left_len, const char *right, size_t right_len) {
    if (!left || !right || left_len != right_len) return 0;
    unsigned char diff = 0;
    for (size_t i = 0; i < left_len; i++) {
        diff |= (unsigned char)left[i] ^ (unsigned char)right[i];
    }
    return diff == 0;
}

int is_target_rate_limited(const char *id) {
    double now = (double)mg_millis() / 1000.0;
    
    for (int i = 0; i < MAX_TARGET_RL_CLIENTS; i++) {
        if (strcmp(g_target_rl_entries[i].id, id) == 0) {
            if (now - g_target_rl_entries[i].last_time < TARGET_RL_COOLDOWN_SEC) {
                return 1;
            }
            g_target_rl_entries[i].last_time = now;
            return 0;
        }
    }
    
    for (int i = 0; i < MAX_TARGET_RL_CLIENTS; i++) {
        if (g_target_rl_entries[i].id[0] == '\0') {
            strncpy(g_target_rl_entries[i].id, id, sizeof(g_target_rl_entries[i].id) - 1);
            g_target_rl_entries[i].id[sizeof(g_target_rl_entries[i].id) - 1] = '\0';
            g_target_rl_entries[i].last_time = now;
            return 0;
        }
    }
    
    int idx = (int)(now * 1000) % MAX_TARGET_RL_CLIENTS;
    strncpy(g_target_rl_entries[idx].id, id, sizeof(g_target_rl_entries[idx].id) - 1);
    g_target_rl_entries[idx].id[sizeof(g_target_rl_entries[idx].id) - 1] = '\0';
    g_target_rl_entries[idx].last_time = now;
    return 0;
}

int find_client_by_token(const char *token, size_t token_len) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_infos[i].id[0] != '\0' && g_client_infos[i].token[0] != '\0') {
            if (g_token_ttl_seconds > 0 && g_client_infos[i].token_issued_at > 0 &&
                (now - g_client_infos[i].token_issued_at) > g_token_ttl_seconds) {
                g_client_infos[i].token[0] = '\0';
                g_client_infos[i].token_issued_at = 0;
                continue;
            }
            if (secure_mem_eq(g_client_infos[i].token, strlen(g_client_infos[i].token), token, token_len)) {
                return i;
            }
        }
    }
    return -1;
}

void store_client_info(const char *id, const char *hostname, const char *os, 
                       const char *uptime, const char *cpu, const char *ram, 
                       const char *version, int locked) {
    double now = (double)mg_millis() / 1000.0;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (strcmp(g_client_infos[i].id, id) == 0) {
            if (hostname) strncpy(g_client_infos[i].hostname, hostname, sizeof(g_client_infos[i].hostname) - 1);
            if (os) strncpy(g_client_infos[i].os, os, sizeof(g_client_infos[i].os) - 1);
            if (uptime) strncpy(g_client_infos[i].uptime, uptime, sizeof(g_client_infos[i].uptime) - 1);
            if (cpu) strncpy(g_client_infos[i].cpu, cpu, sizeof(g_client_infos[i].cpu) - 1);
            if (ram) strncpy(g_client_infos[i].ram, ram, sizeof(g_client_infos[i].ram) - 1);
            if (version) strncpy(g_client_infos[i].version, version, sizeof(g_client_infos[i].version) - 1);
            g_client_infos[i].hostname[sizeof(g_client_infos[i].hostname) - 1] = '\0';
            g_client_infos[i].os[sizeof(g_client_infos[i].os) - 1] = '\0';
            g_client_infos[i].uptime[sizeof(g_client_infos[i].uptime) - 1] = '\0';
            g_client_infos[i].cpu[sizeof(g_client_infos[i].cpu) - 1] = '\0';
            g_client_infos[i].ram[sizeof(g_client_infos[i].ram) - 1] = '\0';
            g_client_infos[i].version[sizeof(g_client_infos[i].version) - 1] = '\0';
            g_client_infos[i].locked = locked;
            g_client_infos[i].last_update = now;
            return;
        }
    }
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_infos[i].id[0] == '\0') {
            strncpy(g_client_infos[i].id, id, sizeof(g_client_infos[i].id) - 1);
            if (hostname) strncpy(g_client_infos[i].hostname, hostname, sizeof(g_client_infos[i].hostname) - 1);
            if (os) strncpy(g_client_infos[i].os, os, sizeof(g_client_infos[i].os) - 1);
            if (uptime) strncpy(g_client_infos[i].uptime, uptime, sizeof(g_client_infos[i].uptime) - 1);
            if (cpu) strncpy(g_client_infos[i].cpu, cpu, sizeof(g_client_infos[i].cpu) - 1);
            if (ram) strncpy(g_client_infos[i].ram, ram, sizeof(g_client_infos[i].ram) - 1);
            if (version) strncpy(g_client_infos[i].version, version, sizeof(g_client_infos[i].version) - 1);
            g_client_infos[i].id[sizeof(g_client_infos[i].id) - 1] = '\0';
            g_client_infos[i].hostname[sizeof(g_client_infos[i].hostname) - 1] = '\0';
            g_client_infos[i].os[sizeof(g_client_infos[i].os) - 1] = '\0';
            g_client_infos[i].uptime[sizeof(g_client_infos[i].uptime) - 1] = '\0';
            g_client_infos[i].cpu[sizeof(g_client_infos[i].cpu) - 1] = '\0';
            g_client_infos[i].ram[sizeof(g_client_infos[i].ram) - 1] = '\0';
            g_client_infos[i].version[sizeof(g_client_infos[i].version) - 1] = '\0';
            g_client_infos[i].locked = locked;
            g_client_infos[i].last_update = now;
            return;
        }
    }
}

const char* generate_client_token(const char *id) {
    double now = (double)mg_millis() / 1000.0;
    
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (strcmp(g_client_infos[i].id, id) == 0) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_client_infos[i].id[0] == '\0') {
                slot = i;
                strncpy(g_client_infos[i].id, id, sizeof(g_client_infos[i].id) - 1);
                g_client_infos[i].id[sizeof(g_client_infos[i].id) - 1] = '\0';
                break;
            }
        }
    }
    
    if (slot >= 0) {
        // Only generate a new token if one doesn't exist yet to prevent race conditions
        // or invalidation when multiple clients (or reconnects) occur for the same ID.
        if (g_client_infos[slot].token[0] == '\0') {
            generate_secure_token(g_client_infos[slot].token, sizeof(g_client_infos[slot].token));
            g_client_infos[slot].token_issued_at = time(NULL);
        }
        g_client_infos[slot].last_update = now;
        return g_client_infos[slot].token;
    }
    
    return NULL;
}

void remove_client(const char *id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (strcmp(g_client_infos[i].id, id) == 0) {
            memset(&g_client_infos[i], 0, sizeof(g_client_infos[i]));
            return;
        }
    }
}

struct client_info* get_client_info(const char *id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (strcmp(g_client_infos[i].id, id) == 0) {
            return &g_client_infos[i];
        }
    }
    return NULL;
}

int match_target(const char *client_id, const char *target_id) {
    if (strcmp(target_id, "*") == 0) {
        return 1;
    }
    return strcmp(client_id, target_id) == 0;
}
