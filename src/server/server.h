#ifndef SERVER_H
#define SERVER_H

#include "mongoose.h"
#include "cJSON.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <openssl/sha.h>

#ifndef VERSION
#define VERSION "0.0.0"
#endif

// ============== Configuration ==============
#define MAX_CLIENTS 100
#define TARGET_RL_COOLDOWN_SEC 10.0
#define MAX_TARGET_RL_CLIENTS 100

// ============== Structures ==============
struct client_info {
    char id[32];
    char token[65];
    char hostname[256];
    char os[128];
    char uptime[64];
    char cpu[32];
    char ram[32];
    char version[32];
    int locked;
    double last_update;
};

struct target_rl_entry {
    char id[32];
    double last_time;
};

// ============== Variables globales (extern) ==============
extern const char *g_upload_dir;
extern const char *g_credentials_file;
extern const char *g_cors_headers;

extern char g_admin_token[65];
extern int g_user_token_enabled;
extern int g_admin_token_enabled;

extern char g_admin_user[64];
extern char g_admin_hash[65];

extern struct client_info g_client_infos[MAX_CLIENTS];
extern struct target_rl_entry g_target_rl_entries[MAX_TARGET_RL_CLIENTS];

#endif // SERVER_H
