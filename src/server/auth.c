#include "auth.h"
#include "clients.h"
#include <string.h>
#include <stdlib.h>

int load_admin_credentials(void) {
    FILE *fp = fopen(g_credentials_file, "r");
    if (!fp) return 0;
    
    char line[256];
    if (fgets(line, sizeof(line), fp)) {
        char *sep = strchr(line, ':');
        if (sep) {
            *sep = '\0';
            strncpy(g_admin_user, line, sizeof(g_admin_user) - 1);
            
            char *hash = sep + 1;
            size_t len = strlen(hash);
            if (len > 0 && hash[len-1] == '\n') hash[len-1] = '\0';
            strncpy(g_admin_hash, hash, sizeof(g_admin_hash) - 1);
            
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
    
    char combined[256];
    snprintf(combined, sizeof(combined), "%s:%s", user, pass);
    
    char computed_hash[65];
    sha256_hex(combined, computed_hash);
    
    return strcmp(computed_hash, g_admin_hash) == 0;
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
        return 1;
    }
    
    struct mg_str *auth = mg_http_get_header(hm, "Authorization");
    if (auth == NULL || auth->len < 8) {
        return 0;
    }
    
    if (strncmp(auth->buf, "Bearer ", 7) != 0) {
        return 0;
    }
    
    size_t token_len = auth->len - 7;
    const char *token = auth->buf + 7;
    
    if (g_user_token_enabled && find_client_by_token(token, token_len) >= 0) {
        return 1;
    }
    
    if (g_admin_token_enabled && strlen(g_admin_token) == token_len &&
        strncmp(token, g_admin_token, token_len) == 0) {
        return 1;
    }
    
    return 0;
}

int validate_admin_token(struct mg_http_message *hm) {
    if (!g_admin_token_enabled) {
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
    if (token_len != strlen(g_admin_token)) {
        return 0;
    }
    
    return strncmp(auth->buf + 7, g_admin_token, token_len) == 0;
}
