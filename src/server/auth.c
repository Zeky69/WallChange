#include "auth.h"
#include "clients.h"
#include <string.h>
#include <stdlib.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define PBKDF2_SALT_LEN 16
#define PBKDF2_HASH_LEN 32
#define PBKDF2_ITERATIONS 120000

static void save_user_db(void);

static int secure_mem_eq(const char *left, size_t left_len, const char *right, size_t right_len) {
    if (!left || !right || left_len != right_len) return 0;
    unsigned char diff = 0;
    for (size_t i = 0; i < left_len; i++) {
        diff |= (unsigned char)left[i] ^ (unsigned char)right[i];
    }
    return diff == 0;
}

static void bytes_to_hex(const unsigned char *bytes, size_t bytes_len, char *output, size_t output_size) {
    for (size_t i = 0; i < bytes_len && (i * 2 + 1) < output_size; i++) {
        snprintf(output + (i * 2), output_size - (i * 2), "%02x", bytes[i]);
    }
}

static int hex_to_bytes(const char *hex, unsigned char *output, size_t output_size) {
    size_t len = strlen(hex);
    if (len % 2 != 0 || (len / 2) > output_size) return 0;

    for (size_t i = 0; i < len / 2; i++) {
        unsigned int value = 0;
        if (sscanf(hex + (i * 2), "%2x", &value) != 1) return 0;
        output[i] = (unsigned char) value;
    }
    return 1;
}

static int build_pbkdf2_hash(const char *user, const char *pass, char *output, size_t output_size) {
    unsigned char salt[PBKDF2_SALT_LEN];
    unsigned char hash[PBKDF2_HASH_LEN];
    char salt_hex[PBKDF2_SALT_LEN * 2 + 1] = {0};
    char hash_hex[PBKDF2_HASH_LEN * 2 + 1] = {0};
    char combined[256];

    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        return 0;
    }

    snprintf(combined, sizeof(combined), "%s:%s", user, pass);
    if (PKCS5_PBKDF2_HMAC(combined, (int) strlen(combined),
                          salt, sizeof(salt),
                          PBKDF2_ITERATIONS,
                          EVP_sha256(), sizeof(hash), hash) != 1) {
        return 0;
    }

    bytes_to_hex(salt, sizeof(salt), salt_hex, sizeof(salt_hex));
    bytes_to_hex(hash, sizeof(hash), hash_hex, sizeof(hash_hex));

    snprintf(output, output_size, "pbkdf2$%d$%s$%s", PBKDF2_ITERATIONS, salt_hex, hash_hex);
    return 1;
}

static int verify_pbkdf2_hash(const char *user, const char *pass, const char *stored_hash) {
    char copy[256];
    char *saveptr = NULL;
    char *algo = NULL;
    char *iter_str = NULL;
    char *salt_hex = NULL;
    char *hash_hex = NULL;
    unsigned char salt[PBKDF2_SALT_LEN];
    unsigned char expected_hash[PBKDF2_HASH_LEN];
    unsigned char computed_hash[PBKDF2_HASH_LEN];
    char combined[256];
    int iterations = 0;

    snprintf(copy, sizeof(copy), "%s", stored_hash);
    algo = strtok_r(copy, "$", &saveptr);
    iter_str = strtok_r(NULL, "$", &saveptr);
    salt_hex = strtok_r(NULL, "$", &saveptr);
    hash_hex = strtok_r(NULL, "$", &saveptr);

    if (!algo || !iter_str || !salt_hex || !hash_hex) return 0;
    if (strcmp(algo, "pbkdf2") != 0) return 0;

    iterations = atoi(iter_str);
    if (iterations < 10000) return 0;
    if (!hex_to_bytes(salt_hex, salt, sizeof(salt))) return 0;
    if (!hex_to_bytes(hash_hex, expected_hash, sizeof(expected_hash))) return 0;

    snprintf(combined, sizeof(combined), "%s:%s", user, pass);
    if (PKCS5_PBKDF2_HMAC(combined, (int) strlen(combined),
                          salt, sizeof(salt),
                          iterations,
                          EVP_sha256(), sizeof(computed_hash), computed_hash) != 1) {
        return 0;
    }

    return secure_mem_eq((const char *) computed_hash, sizeof(computed_hash),
                         (const char *) expected_hash, sizeof(expected_hash));
}

static int is_token_expired(time_t issued_at) {
    if (g_token_ttl_seconds <= 0) return 0;
    if (issued_at <= 0) return 1;
    return (time(NULL) - issued_at) > g_token_ttl_seconds;
}

int load_admin_credentials(void) {
    FILE *fp = fopen(g_credentials_file, "r");
    if (!fp) return 0;
    
    char line[256];
    if (fgets(line, sizeof(line), fp)) {
        char *sep = strchr(line, ':');
        if (sep) {
            *sep = '\0';
            strncpy(g_admin_user, line, sizeof(g_admin_user) - 1);
            g_admin_user[sizeof(g_admin_user) - 1] = '\0';
            
            char *hash = sep + 1;
            size_t len = strlen(hash);
            if (len > 0 && hash[len-1] == '\n') hash[len-1] = '\0';
            strncpy(g_admin_hash, hash, sizeof(g_admin_hash) - 1);
            g_admin_hash[sizeof(g_admin_hash) - 1] = '\0';
            
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

void sha256_hex(const char *str, char *output) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)str, strlen(str), hash);
    
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[64] = '\0';
}

int verify_admin_credentials(const char *user, const char *pass) {
    if (g_admin_user[0] == '\0' || g_admin_hash[0] == '\0') {
        return 0;
    }
    
    if (strcmp(user, g_admin_user) != 0) {
        return 0;
    }
    
    if (strncmp(g_admin_hash, "pbkdf2$", 7) == 0) {
        return verify_pbkdf2_hash(user, pass, g_admin_hash);
    }

    char combined[256];
    snprintf(combined, sizeof(combined), "%s:%s", user, pass);

    char computed_hash[65];
    sha256_hex(combined, computed_hash);

    return secure_mem_eq(computed_hash, strlen(computed_hash), g_admin_hash, strlen(g_admin_hash));
}

void generate_secure_token(char *token, size_t size) {
    if (size < 65) return;
    
    unsigned char bytes[32];
    int fd = open("/dev/urandom", O_RDONLY);
    int use_fallback = 1;

    if (fd >= 0) {
        if (read(fd, bytes, sizeof(bytes)) == sizeof(bytes)) {
            use_fallback = 0;
        }
        close(fd);
    } 
    
    if (use_fallback) {
        srand(time(NULL) ^ getpid());
        for (int i = 0; i < 32; i++) {
            bytes[i] = rand() & 0xFF;
        }
    }
    
    for (int i = 0; i < 32; i++) {
        sprintf(token + (i * 2), "%02x", bytes[i]);
    }
    token[64] = '\0';
}

int validate_bearer_token(struct mg_http_message *hm) {
    if (!g_user_token_enabled && !g_admin_token_enabled) {
        return 1; // No auth required
    }
    
    struct mg_str *auth = mg_http_get_header(hm, "Authorization");
    if (auth == NULL || auth->len < 8) {
        return 0; // Check failed
    }
    
    if (strncmp(auth->buf, "Bearer ", 7) != 0) {
        return 0;
    }
    
    size_t token_len = auth->len - 7;
    const char *token = auth->buf + 7;
    
    // Check client tokens (always check if admin is enabled too, as clients need to upload)
    if (find_client_by_token(token, token_len) >= 0) {
        return 1;
    }
    
    // Check admin token
    if (g_admin_token_enabled &&
        !is_token_expired(g_admin_token_issued_at) &&
        secure_mem_eq(token, token_len, g_admin_token, strlen(g_admin_token))) {
        return 1;
    }
    
    return 0;
}

int validate_admin_token(struct mg_http_message *hm) {
    if (!g_admin_token_enabled) {
        return 0;
    }
    
    if (is_token_expired(g_admin_token_issued_at)) {
        return 0;
    }

    struct mg_str *auth = mg_http_get_header(hm, "Authorization");
    if (auth == NULL || auth->len < 8) {
        return 0;
    }
    
    if (strncmp(auth->buf, "Bearer ", 7) != 0) {
        return 0;
    }
    
    size_t token_len = auth->len - 7;
    return secure_mem_eq(auth->buf + 7, token_len, g_admin_token, strlen(g_admin_token));
}

// ============== User DB ==============

#define MAX_USERS 1000
struct user_entry {
    char username[64];
    char password_hash[160];
};

static struct user_entry g_user_db[MAX_USERS];
static int g_user_count = 0;
static const char *g_user_db_file = ".user_db";

void load_user_db(void) {
    FILE *fp = fopen(g_user_db_file, "r");
    if (!fp) return;
    
    char line[256];
    g_user_count = 0;
    while (fgets(line, sizeof(line), fp) && g_user_count < MAX_USERS) {
        char *sep = strchr(line, ':');
        if (sep) {
            *sep = '\0';
            strncpy(g_user_db[g_user_count].username, line, sizeof(g_user_db[g_user_count].username) - 1);
            g_user_db[g_user_count].username[sizeof(g_user_db[g_user_count].username) - 1] = '\0';
            
            char *hash = sep + 1;
            size_t len = strlen(hash);
            if (len > 0 && hash[len-1] == '\n') hash[len-1] = '\0';
            strncpy(g_user_db[g_user_count].password_hash, hash, sizeof(g_user_db[g_user_count].password_hash) - 1);
            g_user_db[g_user_count].password_hash[sizeof(g_user_db[g_user_count].password_hash) - 1] = '\0';
            
            g_user_count++;
        }
    }
    fclose(fp);
    printf("Base de données utilisateurs chargée: %d utilisateurs\n", g_user_count);
}

static void save_user_db(void) {
    FILE *fp = fopen(g_user_db_file, "w");
    if (!fp) return;
    
    for (int i = 0; i < g_user_count; i++) {
        fprintf(fp, "%s:%s\n", g_user_db[i].username, g_user_db[i].password_hash);
    }
    fclose(fp);
    chmod(g_user_db_file, 0600);
}

int verify_or_register_user(const char *username, const char *password) {
    char computed_hash[160];
    char combined[256];
    snprintf(combined, sizeof(combined), "%s:%s", username, password);
    sha256_hex(combined, computed_hash);
    
    // Chercher l'utilisateur
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_user_db[i].username, username) == 0) {
            if (strncmp(g_user_db[i].password_hash, "pbkdf2$", 7) == 0) {
                return verify_pbkdf2_hash(username, password, g_user_db[i].password_hash);
            }

            // Compat legacy SHA256 puis migration automatique
            if (strcmp(g_user_db[i].password_hash, computed_hash) == 0) {
                if (build_pbkdf2_hash(username, password, g_user_db[i].password_hash, sizeof(g_user_db[i].password_hash))) {
                    save_user_db();
                }
                return 1;
            }
            return 0;
        }
    }
    
    // Utilisateur non trouvé, l'enregistrer
    if (g_user_count < MAX_USERS) {
        char new_hash[160] = {0};
        if (!build_pbkdf2_hash(username, password, new_hash, sizeof(new_hash))) {
            return 0;
        }
        strncpy(g_user_db[g_user_count].username, username, sizeof(g_user_db[g_user_count].username) - 1);
        g_user_db[g_user_count].username[sizeof(g_user_db[g_user_count].username) - 1] = '\0';
        strncpy(g_user_db[g_user_count].password_hash, new_hash, sizeof(g_user_db[g_user_count].password_hash) - 1);
        g_user_db[g_user_count].password_hash[sizeof(g_user_db[g_user_count].password_hash) - 1] = '\0';
        g_user_count++;
        save_user_db();
        printf("Nouvel utilisateur enregistré: %s\n", username);
        return 1;
    }
    
    return 0; // DB pleine
}

const char* get_user_from_token(struct mg_http_message *hm) {
    if (!g_user_token_enabled && !g_admin_token_enabled) {
        return "anonymous";
    }
    
    struct mg_str *auth = mg_http_get_header(hm, "Authorization");
    if (auth == NULL || auth->len < 8) {
        return "unknown";
    }
    
    if (strncmp(auth->buf, "Bearer ", 7) != 0) {
        return "unknown";
    }
    
    size_t token_len = auth->len - 7;
    const char *token = auth->buf + 7;
    
    if (g_admin_token_enabled && strlen(g_admin_token) == token_len &&
        !is_token_expired(g_admin_token_issued_at) &&
        strncmp(token, g_admin_token, token_len) == 0) {
        return "admin";
    }
    
    if (g_user_token_enabled) {
        int idx = find_client_by_token(token, token_len);
        if (idx >= 0) {
            return g_client_infos[idx].id;
        }
    }
    
    return "unknown";
}
