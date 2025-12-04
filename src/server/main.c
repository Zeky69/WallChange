#include "mongoose.h"
#include "cJSON.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <math.h>

#ifndef VERSION
#define VERSION "0.0.0"
#endif

static const char *s_listen_on = "ws://0.0.0.0:8000";
static const char *s_upload_dir = "uploads";
static const char *s_credentials_file = ".admin_credentials";
static const char *s_cors_headers = "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Authorization\r\n";

// Tokens d'authentification (générés ou désactivés)
static char s_admin_token[65] = {0};  // Token admin (32 bytes hex)
static int s_user_token_enabled = 0;   // Active les tokens utilisateur uniques
static int s_admin_token_enabled = 0;

// Credentials admin (chargés depuis fichier)
static char s_admin_user[64] = {0};
static char s_admin_hash[65] = {0};  // SHA256 hash en hex

// Charge les credentials admin depuis le fichier
static int load_admin_credentials() {
    FILE *fp = fopen(s_credentials_file, "r");
    if (!fp) return 0;
    
    char line[256];
    if (fgets(line, sizeof(line), fp)) {
        // Format: username:sha256hash
        char *sep = strchr(line, ':');
        if (sep) {
            *sep = '\0';
            strncpy(s_admin_user, line, sizeof(s_admin_user) - 1);
            
            // Copier le hash (enlever newline si présent)
            char *hash = sep + 1;
            size_t len = strlen(hash);
            if (len > 0 && hash[len-1] == '\n') hash[len-1] = '\0';
            strncpy(s_admin_hash, hash, sizeof(s_admin_hash) - 1);
            
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

// Calcule le SHA256 d'une chaîne et retourne en hex
static void sha256_hex(const char *str, char *output) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)str, strlen(str), hash);
    
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[64] = '\0';
}

// Vérifie les credentials et retourne 1 si valide
static int verify_admin_credentials(const char *user, const char *pass) {
    if (s_admin_user[0] == '\0' || s_admin_hash[0] == '\0') {
        return 0;  // Pas de credentials chargés
    }
    
    // Vérifier le username
    if (strcmp(user, s_admin_user) != 0) {
        return 0;
    }
    
    // Calculer le hash de "user:pass" et comparer
    char combined[256];
    snprintf(combined, sizeof(combined), "%s:%s", user, pass);
    
    char computed_hash[65];
    sha256_hex(combined, computed_hash);
    
    return strcmp(computed_hash, s_admin_hash) == 0;
}

// Génère un token sécurisé (32 bytes = 64 caractères hex)
static void generate_secure_token(char *token, size_t size) {
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
        // Fallback si /dev/urandom n'est pas disponible ou erreur de lecture
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

// ===================== PONG GAME SERVER =====================
#define PONG_PORT 9999
#define PONG_FPS 60
#define PONG_BALL_SIZE 20
#define PONG_PADDLE_WIDTH 15
#define PONG_PADDLE_HEIGHT 120
#define PONG_BALL_SPEED_X 8.0f
#define PONG_MARGIN 30
#define PONG_GAME_DURATION 60

// Message réseau pour le jeu Pong
typedef struct {
    int type;  // 0 = état du jeu (serveur->client), 1 = position raquette (client->serveur)
    float ball_x, ball_y;
    float ball_vx, ball_vy;
    float paddle_y;
    int score_left, score_right;
    int game_active;
    int is_left_side;  // Pour identifier quel client envoie
    unsigned int frame_count;
} PongNetMessage;

// État du jeu Pong géré par le serveur
typedef struct {
    int active;
    float ball_x, ball_y;
    float ball_vx, ball_vy;
    float paddle_left_y, paddle_right_y;
    int score_left, score_right;
    int screen_width, screen_height;
    int total_width;
    unsigned int frame_count;
    time_t start_time;
    
    // Adresses des deux joueurs
    struct sockaddr_in left_player;
    struct sockaddr_in right_player;
    int left_connected, right_connected;
} PongGameState;

static PongGameState s_pong_game = {0};
static int s_pong_socket = -1;
static pthread_t s_pong_thread;
static int s_pong_running = 0;

// Réinitialise la balle au centre
static void pong_reset_ball(PongGameState *game) {
    game->ball_x = game->total_width / 2.0f;
    game->ball_y = game->screen_height / 2.0f;
    game->ball_vx = (rand() % 2 == 0) ? PONG_BALL_SPEED_X : -PONG_BALL_SPEED_X;
    game->ball_vy = ((rand() % 60) - 30) / 10.0f;
}

// Met à jour la physique du jeu (appelé par le serveur)
static void pong_update_physics(PongGameState *game) {
    game->ball_x += game->ball_vx;
    game->ball_y += game->ball_vy;
    
    // Rebond haut/bas
    if (game->ball_y <= PONG_BALL_SIZE/2) {
        game->ball_y = PONG_BALL_SIZE/2;
        game->ball_vy = fabsf(game->ball_vy);
    }
    if (game->ball_y >= game->screen_height - PONG_BALL_SIZE/2) {
        game->ball_y = game->screen_height - PONG_BALL_SIZE/2;
        game->ball_vy = -fabsf(game->ball_vy);
    }
    
    // Collision raquette gauche
    float left_paddle_x = PONG_MARGIN + PONG_PADDLE_WIDTH;
    if (game->ball_vx < 0 && game->ball_x <= left_paddle_x + PONG_BALL_SIZE/2 && game->ball_x > PONG_MARGIN) {
        if (game->ball_y >= game->paddle_left_y - PONG_BALL_SIZE/2 && 
            game->ball_y <= game->paddle_left_y + PONG_PADDLE_HEIGHT + PONG_BALL_SIZE/2) {
            game->ball_x = left_paddle_x + PONG_BALL_SIZE/2;
            game->ball_vx = fabsf(game->ball_vx) * 1.03f;
            float hit_pos = (game->ball_y - game->paddle_left_y) / PONG_PADDLE_HEIGHT;
            game->ball_vy += (hit_pos - 0.5f) * 4;
        }
    }
    
    // Collision raquette droite
    float right_paddle_x = game->total_width - PONG_MARGIN - PONG_PADDLE_WIDTH;
    if (game->ball_vx > 0 && game->ball_x >= right_paddle_x - PONG_BALL_SIZE/2 && game->ball_x < game->total_width - PONG_MARGIN) {
        if (game->ball_y >= game->paddle_right_y - PONG_BALL_SIZE/2 && 
            game->ball_y <= game->paddle_right_y + PONG_PADDLE_HEIGHT + PONG_BALL_SIZE/2) {
            game->ball_x = right_paddle_x - PONG_BALL_SIZE/2;
            game->ball_vx = -fabsf(game->ball_vx) * 1.03f;
            float hit_pos = (game->ball_y - game->paddle_right_y) / PONG_PADDLE_HEIGHT;
            game->ball_vy += (hit_pos - 0.5f) * 4;
        }
    }
    
    // Limiter vitesse
    if (game->ball_vx > 20) game->ball_vx = 20;
    if (game->ball_vx < -20) game->ball_vx = -20;
    if (game->ball_vy > 12) game->ball_vy = 12;
    if (game->ball_vy < -12) game->ball_vy = -12;
    
    // Buts
    if (game->ball_x < 0) {
        game->score_right++;
        pong_reset_ball(game);
    }
    if (game->ball_x > game->total_width) {
        game->score_left++;
        pong_reset_ball(game);
    }
}

// Thread principal du serveur Pong
static void *pong_server_thread(void *arg) {
    (void)arg;
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PONG_PORT);
    
    s_pong_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_pong_socket < 0) {
        perror("pong socket");
        return NULL;
    }
    
    int opt = 1;
    setsockopt(s_pong_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if (bind(s_pong_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("pong bind");
        close(s_pong_socket);
        s_pong_socket = -1;
        return NULL;
    }
    
    // Non-bloquant
    int flags = fcntl(s_pong_socket, F_GETFL, 0);
    fcntl(s_pong_socket, F_SETFL, flags | O_NONBLOCK);
    
    printf("🏓 Serveur Pong UDP démarré sur port %d\n", PONG_PORT);
    
    struct timeval frame_start, frame_end;
    
    while (s_pong_running) {
        gettimeofday(&frame_start, NULL);
        
        // Recevoir les messages des clients
        PongNetMessage recv_msg;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        while (1) {
            ssize_t received = recvfrom(s_pong_socket, &recv_msg, sizeof(recv_msg), 0,
                                        (struct sockaddr*)&from_addr, &from_len);
            if (received <= 0) break;
            
            if (received == sizeof(PongNetMessage) && recv_msg.type == 1) {
                // Message de position de raquette
                if (recv_msg.is_left_side) {
                    s_pong_game.paddle_left_y = recv_msg.paddle_y;
                    if (!s_pong_game.left_connected) {
                        s_pong_game.left_player = from_addr;
                        s_pong_game.left_connected = 1;
                        printf("🎮 Joueur GAUCHE connecté: %s\n", inet_ntoa(from_addr.sin_addr));
                    }
                } else {
                    s_pong_game.paddle_right_y = recv_msg.paddle_y;
                    if (!s_pong_game.right_connected) {
                        s_pong_game.right_player = from_addr;
                        s_pong_game.right_connected = 1;
                        printf("🎮 Joueur DROITE connecté: %s\n", inet_ntoa(from_addr.sin_addr));
                    }
                }
                
                // Initialiser les dimensions si c'est le premier message
                if (s_pong_game.screen_width == 0) {
                    // On assume 1920x1080 par défaut, les clients peuvent envoyer leurs dimensions
                    s_pong_game.screen_width = 1920;
                    s_pong_game.screen_height = 1080;
                    s_pong_game.total_width = s_pong_game.screen_width * 2;
                    s_pong_game.paddle_left_y = (s_pong_game.screen_height - PONG_PADDLE_HEIGHT) / 2.0f;
                    s_pong_game.paddle_right_y = (s_pong_game.screen_height - PONG_PADDLE_HEIGHT) / 2.0f;
                    pong_reset_ball(&s_pong_game);
                    s_pong_game.start_time = time(NULL);
                    s_pong_game.active = 1;
                    printf("🏓 Partie démarrée! Dimensions: %dx%d (total: %d)\n", 
                           s_pong_game.screen_width, s_pong_game.screen_height, s_pong_game.total_width);
                }
            }
        }
        
        // Si le jeu est actif, mettre à jour la physique
        if (s_pong_game.active && s_pong_game.left_connected && s_pong_game.right_connected) {
            // Vérifier le temps
            if (time(NULL) - s_pong_game.start_time >= PONG_GAME_DURATION) {
                s_pong_game.active = 0;
                printf("🏆 Fin de partie! Score: %d - %d\n", s_pong_game.score_left, s_pong_game.score_right);
            } else {
                pong_update_physics(&s_pong_game);
                s_pong_game.frame_count++;
            }
            
            // Envoyer l'état aux deux joueurs
            PongNetMessage send_msg;
            send_msg.type = 0;
            send_msg.ball_x = s_pong_game.ball_x;
            send_msg.ball_y = s_pong_game.ball_y;
            send_msg.ball_vx = s_pong_game.ball_vx;
            send_msg.ball_vy = s_pong_game.ball_vy;
            send_msg.score_left = s_pong_game.score_left;
            send_msg.score_right = s_pong_game.score_right;
            send_msg.game_active = s_pong_game.active;
            send_msg.frame_count = s_pong_game.frame_count;
            
            // Envoyer au joueur gauche avec la position de la raquette droite
            send_msg.paddle_y = s_pong_game.paddle_right_y;
            send_msg.is_left_side = 1;
            sendto(s_pong_socket, &send_msg, sizeof(send_msg), 0,
                   (struct sockaddr*)&s_pong_game.left_player, sizeof(s_pong_game.left_player));
            
            // Envoyer au joueur droit avec la position de la raquette gauche
            send_msg.paddle_y = s_pong_game.paddle_left_y;
            send_msg.is_left_side = 0;
            sendto(s_pong_socket, &send_msg, sizeof(send_msg), 0,
                   (struct sockaddr*)&s_pong_game.right_player, sizeof(s_pong_game.right_player));
        }
        
        // Maintenir ~60 FPS
        gettimeofday(&frame_end, NULL);
        long elapsed_us = (frame_end.tv_sec - frame_start.tv_sec) * 1000000 +
                         (frame_end.tv_usec - frame_start.tv_usec);
        long target_us = 1000000 / PONG_FPS;
        if (elapsed_us < target_us) {
            usleep(target_us - elapsed_us);
        }
    }
    
    if (s_pong_socket >= 0) {
        close(s_pong_socket);
        s_pong_socket = -1;
    }
    return NULL;
}

// Démarre le serveur Pong
static void start_pong_server() {
    if (s_pong_running) return;
    
    memset(&s_pong_game, 0, sizeof(s_pong_game));
    srand(time(NULL));
    s_pong_running = 1;
    pthread_create(&s_pong_thread, NULL, pong_server_thread, NULL);
}

// Arrête le serveur Pong
static void stop_pong_server() {
    if (!s_pong_running) return;
    s_pong_running = 0;
    pthread_join(s_pong_thread, NULL);
}

// ===================== FIN PONG GAME SERVER =====================

// Structure pour stocker les infos client
#define MAX_CLIENTS 100
struct client_info {
    char id[32];
    char token[65];  // Token unique par client
    char hostname[256];
    char os[128];
    char uptime[64];
    char cpu[32];
    char ram[32];
    char version[32];
    double last_update;
};
static struct client_info s_client_infos[MAX_CLIENTS];

// Target Rate limiting
#define TARGET_RL_COOLDOWN_SEC 10.0
#define MAX_TARGET_RL_CLIENTS 100

struct target_rl_entry {
    char id[32];
    double last_time;
};

static struct target_rl_entry s_target_rl_entries[MAX_TARGET_RL_CLIENTS];

static int is_target_rate_limited(const char *id) {
    double now = (double)mg_millis() / 1000.0;
    for (int i = 0; i < MAX_TARGET_RL_CLIENTS; i++) {
        if (strcmp(s_target_rl_entries[i].id, id) == 0) {
            if (now - s_target_rl_entries[i].last_time < TARGET_RL_COOLDOWN_SEC) {
                return 1; // Limited
            }
            s_target_rl_entries[i].last_time = now;
            return 0; // Allowed
        }
    }
    // Find empty slot
    for (int i = 0; i < MAX_TARGET_RL_CLIENTS; i++) {
        if (s_target_rl_entries[i].id[0] == '\0') {
            strncpy(s_target_rl_entries[i].id, id, sizeof(s_target_rl_entries[i].id) - 1);
            s_target_rl_entries[i].last_time = now;
            return 0;
        }
    }
    // Overwrite random (using time as random-ish index)
    int idx = (int)(now * 1000) % MAX_TARGET_RL_CLIENTS;
    strncpy(s_target_rl_entries[idx].id, id, sizeof(s_target_rl_entries[idx].id) - 1);
    s_target_rl_entries[idx].last_time = now;
    return 0;
}

// Cherche un token client dans la liste
static int find_client_by_token(const char *token, size_t token_len) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_client_infos[i].id[0] != '\0' && s_client_infos[i].token[0] != '\0') {
            if (strlen(s_client_infos[i].token) == token_len &&
                strncmp(s_client_infos[i].token, token, token_len) == 0) {
                return i;
            }
        }
    }
    return -1;
}

// Valide le token Bearer dans l'en-tête Authorization (utilisateur ou admin)
static int validate_bearer_token(struct mg_http_message *hm) {
    // Si aucun token activé = accès libre
    if (!s_user_token_enabled && !s_admin_token_enabled) {
        return 1;
    }
    
    struct mg_str *auth = mg_http_get_header(hm, "Authorization");
    if (auth == NULL || auth->len < 8) {
        return 0; // Pas d'en-tête Authorization
    }
    
    // Vérifier le préfixe "Bearer "
    if (strncmp(auth->buf, "Bearer ", 7) != 0) {
        return 0;
    }
    
    // Extraire le token
    size_t token_len = auth->len - 7;
    const char *token = auth->buf + 7;
    
    // Vérifier si c'est un token utilisateur valide
    if (s_user_token_enabled && find_client_by_token(token, token_len) >= 0) {
        return 1;
    }
    
    // Vérifier si c'est le token admin
    if (s_admin_token_enabled && strlen(s_admin_token) == token_len &&
        strncmp(token, s_admin_token, token_len) == 0) {
        return 1;
    }
    
    return 0;
}

// Valide que le token est le token ADMIN (pour uninstall)
static int validate_admin_token(struct mg_http_message *hm) {
    if (!s_admin_token_enabled) {
        return 0; // Pas de token admin = fonction désactivée
    }
    
    struct mg_str *auth = mg_http_get_header(hm, "Authorization");
    if (auth == NULL || auth->len < 8) {
        return 0;
    }
    
    if (strncmp(auth->buf, "Bearer ", 7) != 0) {
        return 0;
    }
    
    size_t token_len = auth->len - 7;
    if (token_len != strlen(s_admin_token)) {
        return 0;
    }
    
    return strncmp(auth->buf + 7, s_admin_token, token_len) == 0;
}

// Stocke les infos d'un client
static void store_client_info(const char *id, const char *hostname, const char *os, 
                               const char *uptime, const char *cpu, const char *ram, 
                               const char *version) {
    double now = (double)mg_millis() / 1000.0;
    
    // Chercher un slot existant
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (strcmp(s_client_infos[i].id, id) == 0) {
            if (hostname) strncpy(s_client_infos[i].hostname, hostname, sizeof(s_client_infos[i].hostname) - 1);
            if (os) strncpy(s_client_infos[i].os, os, sizeof(s_client_infos[i].os) - 1);
            if (uptime) strncpy(s_client_infos[i].uptime, uptime, sizeof(s_client_infos[i].uptime) - 1);
            if (cpu) strncpy(s_client_infos[i].cpu, cpu, sizeof(s_client_infos[i].cpu) - 1);
            if (ram) strncpy(s_client_infos[i].ram, ram, sizeof(s_client_infos[i].ram) - 1);
            if (version) strncpy(s_client_infos[i].version, version, sizeof(s_client_infos[i].version) - 1);
            s_client_infos[i].last_update = now;
            return;
        }
    }
    
    // Chercher un slot vide
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_client_infos[i].id[0] == '\0') {
            strncpy(s_client_infos[i].id, id, sizeof(s_client_infos[i].id) - 1);
            if (hostname) strncpy(s_client_infos[i].hostname, hostname, sizeof(s_client_infos[i].hostname) - 1);
            if (os) strncpy(s_client_infos[i].os, os, sizeof(s_client_infos[i].os) - 1);
            if (uptime) strncpy(s_client_infos[i].uptime, uptime, sizeof(s_client_infos[i].uptime) - 1);
            if (cpu) strncpy(s_client_infos[i].cpu, cpu, sizeof(s_client_infos[i].cpu) - 1);
            if (ram) strncpy(s_client_infos[i].ram, ram, sizeof(s_client_infos[i].ram) - 1);
            if (version) strncpy(s_client_infos[i].version, version, sizeof(s_client_infos[i].version) - 1);
            s_client_infos[i].last_update = now;
            return;
        }
    }
}

// Génère et stocke un token unique pour un client
static const char* generate_client_token(const char *id) {
    double now = (double)mg_millis() / 1000.0;
    
    // Chercher un slot existant ou vide
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (strcmp(s_client_infos[i].id, id) == 0) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (s_client_infos[i].id[0] == '\0') {
                slot = i;
                strncpy(s_client_infos[i].id, id, sizeof(s_client_infos[i].id) - 1);
                break;
            }
        }
    }
    
    if (slot >= 0) {
        generate_secure_token(s_client_infos[slot].token, sizeof(s_client_infos[slot].token));
        s_client_infos[slot].last_update = now;
        return s_client_infos[slot].token;
    }
    
    return NULL;
}

// Supprime un client (lors de la déconnexion)
static void remove_client(const char *id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (strcmp(s_client_infos[i].id, id) == 0) {
            memset(&s_client_infos[i], 0, sizeof(s_client_infos[i]));
            return;
        }
    }
}

// Récupère les infos d'un client
static struct client_info* get_client_info(const char *id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (strcmp(s_client_infos[i].id, id) == 0) {
            return &s_client_infos[i];
        }
    }
    return NULL;
}

// Vérifie si un client correspond à la cible (* = tous, sinon comparaison exacte)
static int match_target(const char *client_id, const char *target_id) {
    if (strcmp(target_id, "*") == 0) {
        return 1;  // Wildcard: correspond à tous
    }
    return strcmp(client_id, target_id) == 0;
}

// Fonction utilitaire pour récupérer un paramètre de la query string
// Mongoose a mg_http_get_var mais on va le faire simplement
void get_qs_var(const struct mg_str *query, const char *name, char *dst, size_t dst_len) {
    dst[0] = '\0';
    if (query && query->len > 0) {
        mg_http_get_var(query, name, dst, dst_len);
    }
}

// Vérifie le rate limit (bypass pour admin)
static int check_rate_limit(struct mg_http_message *hm, const char *target_id) {
    if (validate_admin_token(hm)) return 0;
    return is_target_rate_limited(target_id);
}

static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        
        if (mg_match(hm->method, mg_str("OPTIONS"), NULL)) {
            mg_http_reply(c, 200, s_cors_headers, "");
            return;
        }

        // 0. API pour se connecter en admin et obtenir le token
        if (mg_match(hm->uri, mg_str("/api/login"), NULL)) {
            char user[64] = {0};
            char pass[128] = {0};
            
            get_qs_var(&hm->query, "user", user, sizeof(user));
            get_qs_var(&hm->query, "pass", pass, sizeof(pass));
            
            if (strlen(user) == 0 || strlen(pass) == 0) {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'user' or 'pass' parameter\n");
                return;
            }
            
            if (!s_admin_token_enabled) {
                mg_http_reply(c, 503, s_cors_headers, "Admin authentication not enabled on this server\n");
                return;
            }
            
            if (verify_admin_credentials(user, pass)) {
                // Login réussi - retourner le token admin
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "status", "success");
                cJSON_AddStringToObject(json, "token", s_admin_token);
                cJSON_AddStringToObject(json, "type", "admin");
                char *json_str = cJSON_Print(json);
                
                mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", 
                              "%s", json_str);
                
                printf("🔓 Login admin réussi pour '%s'\n", user);
                free(json_str);
                cJSON_Delete(json);
            } else {
                printf("⚠️  Tentative de login échouée pour '%s'\n", user);
                mg_http_reply(c, 401, s_cors_headers, "Invalid username or password\n");
            }
            return;
        }

        // 1. API pour envoyer une image (Admin / Script)
        if (mg_match(hm->uri, mg_str("/api/send"), NULL)) {
            if (!validate_bearer_token(hm)) {
                mg_http_reply(c, 401, s_cors_headers, "Unauthorized: Invalid or missing token\n");
                return;
            }
            
            char target_id[32];
            char url[512];
            
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
            get_qs_var(&hm->query, "url", url, sizeof(url));
            
            // Wildcard * nécessite le token admin
            if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
                mg_http_reply(c, 403, s_cors_headers, "Forbidden: Admin token required for wildcard\n");
                return;
            }
            
            if (strlen(target_id) > 0 && strlen(url) > 0) {
                if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
                    mg_http_reply(c, 429, s_cors_headers, "Too Many Requests for this target\n");
                    return;
                }

                // Création du JSON
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "url", url);
                char *json_str = cJSON_PrintUnformatted(json);
                
                int found = 0;
                // On parcourt toutes les connexions pour trouver la bonne
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && match_target(t->data, target_id)) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }
                
                free(json_str);
                cJSON_Delete(json);
                
                mg_http_reply(c, 200, s_cors_headers, "Sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' or 'url' parameter\n");
            }
        }
        // 1.5 API pour demander une mise à jour au client
        else if (mg_match(hm->uri, mg_str("/api/update"), NULL)) {
            if (!validate_bearer_token(hm)) {
                mg_http_reply(c, 401, s_cors_headers, "Unauthorized: Invalid or missing token\n");
                return;
            }
            
            char target_id[32];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

            // Wildcard * nécessite le token admin
            if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
                mg_http_reply(c, 403, s_cors_headers, "Forbidden: Admin token required for wildcard\n");
                return;
            }

            if (strlen(target_id) > 0) {
                if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
                    mg_http_reply(c, 429, s_cors_headers, "Too Many Requests for this target\n");
                    return;
                }

                // Création du JSON
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "update");
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                printf("Recherche du client '%s' pour mise à jour...\n", target_id);
                // On parcourt toutes les connexions pour trouver la bonne
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket) {
                        printf(" - Client connecté: '%s'\n", (char *)t->data);
                        if (match_target(t->data, target_id)) {
                            mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                            found++;
                        }
                    }
                }

                free(json_str);
                cJSON_Delete(json);

                mg_http_reply(c, 200, s_cors_headers, "Update request sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' parameter\n");
            }
        }
        // 1.55 API pour obtenir la version du serveur
        else if (mg_match(hm->uri, mg_str("/api/version"), NULL)) {
            mg_http_reply(c, 200, "Content-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n", VERSION);
        }
        // 1.6 API pour lister les clients connectés
        else if (mg_match(hm->uri, mg_str("/api/list"), NULL)) {
            cJSON *json = cJSON_CreateArray();
            int count = 0;
            for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                if (t->is_websocket) {
                    const char *client_id = (char *)t->data;
                    cJSON *client_obj = cJSON_CreateObject();
                    cJSON_AddStringToObject(client_obj, "id", client_id);
                    
                    struct client_info *info = get_client_info(client_id);
                    if (info) {
                        if (info->hostname[0] != '\0') {
                            cJSON_AddStringToObject(client_obj, "hostname", info->hostname);
                        }
                        if (info->version[0] != '\0') {
                            cJSON_AddStringToObject(client_obj, "version", info->version);
                        }
                    }

                    cJSON_AddItemToArray(json, client_obj);
                    count++;
                }
            }
            char *json_str = cJSON_Print(json);
            mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", "%s", json_str);
            free(json_str);
            cJSON_Delete(json);
        }
        // 1.65 API pour désinstaller un client (admin uniquement pour autres clients)
        else if (mg_match(hm->uri, mg_str("/api/uninstall"), NULL)) {
            char target_id[32];
            char from_user[32];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
            get_qs_var(&hm->query, "from", from_user, sizeof(from_user));
            
            // Vérifier l'autorisation
            int is_admin = validate_admin_token(hm);
            int is_user = validate_bearer_token(hm);
            int is_self_uninstall = (strlen(target_id) > 0 && strlen(from_user) > 0 && 
                                     strcmp(target_id, from_user) == 0);
            
            // Admin peut tout faire, utilisateur peut seulement se désinstaller lui-même
            if (!is_admin && !is_self_uninstall) {
                if (!is_user) {
                    mg_http_reply(c, 401, s_cors_headers, "Unauthorized: Invalid or missing token\n");
                } else {
                    mg_http_reply(c, 403, s_cors_headers, "Forbidden: Admin token required to uninstall other clients\n");
                }
                return;
            }

            if (strlen(target_id) > 0 && strlen(from_user) > 0) {
                if (check_rate_limit(hm, target_id)) {
                    mg_http_reply(c, 429, s_cors_headers, "Too Many Requests for this target\n");
                    return;
                }

                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "uninstall");
                cJSON_AddStringToObject(json, "from", from_user);
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                printf("Recherche du client '%s' pour désinstallation (demandé par %s)...\n", 
                       target_id, from_user);
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket) {
                        if (strcmp(t->data, target_id) == 0) {
                            mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                            found++;
                        }
                    }
                }

                free(json_str);
                cJSON_Delete(json);
                mg_http_reply(c, 200, s_cors_headers, "Uninstall request sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' or 'from' parameter\n");
            }
        }
        // 1.7 API pour envoyer la commande showdesktop (Super+D)
        else if (mg_match(hm->uri, mg_str("/api/showdesktop"), NULL)) {
            if (!validate_bearer_token(hm)) {
                mg_http_reply(c, 401, s_cors_headers, "Unauthorized: Invalid or missing token\n");
                return;
            }
            
            char target_id[32];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

            // Wildcard * nécessite le token admin
            if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
                mg_http_reply(c, 403, s_cors_headers, "Forbidden: Admin token required for wildcard\n");
                return;
            }

            if (strlen(target_id) > 0) {
                if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
                    mg_http_reply(c, 429, s_cors_headers, "Too Many Requests for this target\n");
                    return;
                }

                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "showdesktop");
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && match_target(t->data, target_id)) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }

                free(json_str);
                cJSON_Delete(json);
                mg_http_reply(c, 200, s_cors_headers, "Showdesktop sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' parameter\n");
            }
        }
        // 1.75 API pour envoyer la commande reverse
        else if (mg_match(hm->uri, mg_str("/api/reverse"), NULL)) {
            if (!validate_bearer_token(hm)) {
                mg_http_reply(c, 401, s_cors_headers, "Unauthorized: Invalid or missing token\n");
                return;
            }
            
            char target_id[32];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

            // Wildcard * nécessite le token admin
            if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
                mg_http_reply(c, 403, s_cors_headers, "Forbidden: Admin token required for wildcard\n");
                return;
            }

            if (strlen(target_id) > 0) {
                if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
                    mg_http_reply(c, 429, s_cors_headers, "Too Many Requests for this target\n");
                    return;
                }

                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "reverse");
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && match_target(t->data, target_id)) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }

                free(json_str);
                cJSON_Delete(json);
                mg_http_reply(c, 200, s_cors_headers, "Reverse sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' parameter\n");
            }
        }
        // 1.8 API pour envoyer un raccourci clavier personnalisé
        else if (mg_match(hm->uri, mg_str("/api/key"), NULL)) {
            if (!validate_bearer_token(hm)) {
                mg_http_reply(c, 401, s_cors_headers, "Unauthorized: Invalid or missing token\n");
                return;
            }
            
            char target_id[32];
            char combo[128];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
            get_qs_var(&hm->query, "combo", combo, sizeof(combo));

            // Wildcard * nécessite le token admin
            if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
                mg_http_reply(c, 403, s_cors_headers, "Forbidden: Admin token required for wildcard\n");
                return;
            }

            if (strlen(target_id) > 0 && strlen(combo) > 0) {
                if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
                    mg_http_reply(c, 429, s_cors_headers, "Too Many Requests for this target\n");
                    return;
                }

                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "key");
                cJSON_AddStringToObject(json, "combo", combo);
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && match_target(t->data, target_id)) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }

                free(json_str);
                cJSON_Delete(json);
                mg_http_reply(c, 200, s_cors_headers, "Key '%s' sent to %d client(s)\n", combo, found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' or 'combo' parameter\n");
            }
        }
        // 1.9 API pour faire défiler une image (Marquee)
        else if (mg_match(hm->uri, mg_str("/api/marquee"), NULL)) {
            if (!validate_bearer_token(hm)) {
                mg_http_reply(c, 401, s_cors_headers, "Unauthorized: Invalid or missing token\n");
                return;
            }
            
            char target_id[32];
            char url[512];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
            get_qs_var(&hm->query, "url", url, sizeof(url));

            // Wildcard * nécessite le token admin
            if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
                mg_http_reply(c, 403, s_cors_headers, "Forbidden: Admin token required for wildcard\n");
                return;
            }

            if (strlen(target_id) > 0 && strlen(url) > 0) {
                if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
                    mg_http_reply(c, 429, s_cors_headers, "Too Many Requests for this target\n");
                    return;
                }

                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "marquee");
                cJSON_AddStringToObject(json, "url", url);
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && match_target(t->data, target_id)) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }

                free(json_str);
                cJSON_Delete(json);
                mg_http_reply(c, 200, s_cors_headers, "Marquee sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' or 'url' parameter\n");
            }
        }
        // 1.95 API pour les particules autour de la souris
        else if (mg_match(hm->uri, mg_str("/api/particles"), NULL)) {
            if (!validate_bearer_token(hm)) {
                mg_http_reply(c, 401, s_cors_headers, "Unauthorized: Invalid or missing token\n");
                return;
            }
            
            char target_id[32];
            char url[512];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
            get_qs_var(&hm->query, "url", url, sizeof(url));

            // Wildcard * nécessite le token admin
            if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
                mg_http_reply(c, 403, s_cors_headers, "Forbidden: Admin token required for wildcard\n");
                return;
            }

            if (strlen(target_id) > 0 && strlen(url) > 0) {
                if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
                    mg_http_reply(c, 429, s_cors_headers, "Too Many Requests for this target\n");
                    return;
                }

                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "particles");
                cJSON_AddStringToObject(json, "url", url);
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && match_target(t->data, target_id)) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }

                free(json_str);
                cJSON_Delete(json);
                mg_http_reply(c, 200, s_cors_headers, "Particles sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' or 'url' parameter\n");
            }
        }
        // 1.96 API pour les clones de souris
        else if (mg_match(hm->uri, mg_str("/api/clones"), NULL)) {
            if (!validate_bearer_token(hm)) {
                mg_http_reply(c, 401, s_cors_headers, "Unauthorized: Invalid or missing token\n");
                return;
            }
            
            char target_id[32];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

            // Wildcard * nécessite le token admin
            if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
                mg_http_reply(c, 403, s_cors_headers, "Forbidden: Admin token required for wildcard\n");
                return;
            }

            if (strlen(target_id) > 0) {
                if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
                    mg_http_reply(c, 429, s_cors_headers, "Too Many Requests for this target\n");
                    return;
                }

                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "clones");
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && match_target(t->data, target_id)) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }

                free(json_str);
                cJSON_Delete(json);
                mg_http_reply(c, 200, s_cors_headers, "Clones sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' parameter\n");
            }
        }
        // API pour le jeu Pong en réseau
        else if (mg_match(hm->uri, mg_str("/api/pong"), NULL)) {
            if (!validate_bearer_token(hm)) {
                mg_http_reply(c, 401, s_cors_headers, "Unauthorized: Invalid or missing token\n");
                return;
            }
            
            char target_id[32];
            char from_user[64];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
            get_qs_var(&hm->query, "from", from_user, sizeof(from_user));

            if (strlen(target_id) > 0 && strlen(from_user) > 0) {
                if (check_rate_limit(hm, target_id)) {
                    mg_http_reply(c, 429, s_cors_headers, "Too Many Requests for this target\n");
                    return;
                }

                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "pong");
                cJSON_AddStringToObject(json, "from", from_user);
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                printf("🏓 Envoi invitation Pong de '%s' vers '%s'...\n", from_user, target_id);
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && match_target(t->data, target_id)) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }

                free(json_str);
                cJSON_Delete(json);
                mg_http_reply(c, 200, s_cors_headers, "Pong invitation sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' or 'from' parameter\n");
            }
        }
        // 2. API pour uploader une image
        else if (mg_match(hm->uri, mg_str("/api/upload"), NULL)) {
            if (!validate_bearer_token(hm)) {
                mg_http_reply(c, 401, s_cors_headers, "Unauthorized: Invalid or missing token\n");
                return;
            }
            
            // On utilise mg_http_upload pour gérer le multipart
            // Il va sauvegarder le fichier dans le dossier uploads
            // On doit récupérer le nom du fichier pour construire l'URL
            
            // Note: mg_http_upload traite tout le body.
            // Pour simplifier, on va itérer sur les parts pour trouver le fichier et le sauver nous-même
            // car mg_http_upload ne retourne pas facilement le nom du fichier sauvegardé.
            
            struct mg_http_part part;
            size_t ofs = 0;
            int uploaded = 0;
            char saved_path[512] = {0};
            
            while ((ofs = mg_http_next_multipart(hm->body, ofs, &part)) > 0) {
                if (part.filename.len > 0) {
                    // C'est un fichier
                    snprintf(saved_path, sizeof(saved_path), "%s/%.*s", s_upload_dir, (int)part.filename.len, part.filename.buf);
                    
                    FILE *fp = fopen(saved_path, "wb");
                    if (fp) {
                        fwrite(part.body.buf, 1, part.body.len, fp);
                        fclose(fp);
                        printf("Fichier uploadé: %s\n", saved_path);
                        uploaded = 1;
                    }
                }
            }
            
            if (uploaded) {
                // Si on a un ID cible, on envoie la notif
                char target_id[32];
                char type[32] = {0};
                get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
                get_qs_var(&hm->query, "type", type, sizeof(type));
                
                if (strlen(target_id) > 0) {
                    // Wildcard * nécessite le token admin
                    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
                        mg_http_reply(c, 403, s_cors_headers, "Forbidden: Admin token required for wildcard\n");
                        return;
                    }
                    
                    if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
                        mg_http_reply(c, 429, s_cors_headers, "Too Many Requests for this target\n");
                        return;
                    }

                    // Construction de l'URL
                    char host[128];
                    struct mg_str *h = mg_http_get_header(hm, "Host");
                    if (h) snprintf(host, sizeof(host), "%.*s", (int)h->len, h->buf);
                    else strcpy(host, "localhost:8000");
                    
                    char full_url[1024];
                    snprintf(full_url, sizeof(full_url), "http://%s/%s", host, saved_path);
                    
                    // Envoi WebSocket
                    cJSON *json = cJSON_CreateObject();
                    if (strcmp(type, "marquee") == 0) {
                        cJSON_AddStringToObject(json, "command", "marquee");
                    } else if (strcmp(type, "particles") == 0) {
                        cJSON_AddStringToObject(json, "command", "particles");
                    }
                    cJSON_AddStringToObject(json, "url", full_url);
                    char *json_str = cJSON_PrintUnformatted(json);
                    
                    int found = 0;
                    for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                        if (t->is_websocket && match_target(t->data, target_id)) {
                            mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                            found++;
                        }
                    }
                    free(json_str);
                    cJSON_Delete(json);
                    
                    mg_http_reply(c, 200, s_cors_headers, "Uploaded and sent to %d client(s)\n", found);
                } else {
                    mg_http_reply(c, 200, s_cors_headers, "Uploaded but no target id provided\n");
                }
            } else {
                mg_http_reply(c, 400, s_cors_headers, "No file found in request\n");
            }
        }
        // 3. Servir les fichiers uploadés
        else if (mg_match(hm->uri, mg_str("/uploads/*"), NULL)) {
            struct mg_http_serve_opts opts = {
                .root_dir = ".",
                .extra_headers = "Access-Control-Allow-Origin: *\r\n"
            };
            mg_http_serve_dir(c, hm, &opts);
        }
        // 4. Gestion de la connexion WebSocket (Client)
        else if (hm->uri.len > 1) {
            char id[32];
            // On copie l'URI sans le premier slash
            snprintf(id, sizeof(id), "%.*s", (int)hm->uri.len - 1, hm->uri.buf + 1);
            
            // On stocke l'ID dans c->data
            snprintf(c->data, sizeof(c->data), "%s", id);
            printf("Nouveau client connecté: %s\n", id);
            mg_ws_upgrade(c, hm, NULL);
        }
        // Page d'accueil simple
        else {
            mg_http_reply(c, 200, "Content-Type: text/html\r\nAccess-Control-Allow-Origin: *\r\n", 
                "<h1>Wallchange Server</h1>"
                "<p>Utilisez /api/send?id=USER&url=URL pour changer le fond d'ecran.</p>");
        }
    } else if (ev == MG_EV_WS_OPEN) {
        // Connexion WS établie - générer et envoyer un token unique au client
        if (s_user_token_enabled) {
            const char *client_id = (char *)c->data;
            const char *token = generate_client_token(client_id);
            if (token) {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "type", "auth");
                cJSON_AddStringToObject(json, "token", token);
                char *json_str = cJSON_PrintUnformatted(json);
                mg_ws_send(c, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                free(json_str);
                cJSON_Delete(json);
                printf("🔑 Token unique généré pour %s: %.16s...\n", client_id, token);
            }
        }
    } else if (ev == MG_EV_WS_MSG) {
        // Message reçu du client - traiter les infos système
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        cJSON *json = cJSON_ParseWithLength(wm->data.buf, wm->data.len);
        if (json) {
            cJSON *type_item = cJSON_GetObjectItemCaseSensitive(json, "type");
            if (cJSON_IsString(type_item) && strcmp(type_item->valuestring, "info") == 0) {
                const char *client_id = (char *)c->data;
                cJSON *hostname = cJSON_GetObjectItemCaseSensitive(json, "hostname");
                cJSON *os = cJSON_GetObjectItemCaseSensitive(json, "os");
                cJSON *uptime = cJSON_GetObjectItemCaseSensitive(json, "uptime");
                cJSON *cpu = cJSON_GetObjectItemCaseSensitive(json, "cpu");
                cJSON *ram = cJSON_GetObjectItemCaseSensitive(json, "ram");
                cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
                
                store_client_info(client_id,
                    cJSON_IsString(hostname) ? hostname->valuestring : NULL,
                    cJSON_IsString(os) ? os->valuestring : NULL,
                    cJSON_IsString(uptime) ? uptime->valuestring : NULL,
                    cJSON_IsString(cpu) ? cpu->valuestring : NULL,
                    cJSON_IsString(ram) ? ram->valuestring : NULL,
                    cJSON_IsString(version) ? version->valuestring : NULL);
                
                printf("Info reçue de %s: Host=%s, OS=%s, Uptime=%s, CPU=%s, RAM=%s, Ver=%s\n", 
                       client_id,
                       cJSON_IsString(hostname) ? hostname->valuestring : "?",
                       cJSON_IsString(os) ? os->valuestring : "?",
                       cJSON_IsString(uptime) ? uptime->valuestring : "?",
                       cJSON_IsString(cpu) ? cpu->valuestring : "?",
                       cJSON_IsString(ram) ? ram->valuestring : "?",
                       cJSON_IsString(version) ? version->valuestring : "?");
            }
            cJSON_Delete(json);
        }
    } else if (ev == MG_EV_CLOSE) {
        if (c->is_websocket) {
            const char *client_id = (char *)c->data;
            printf("Client déconnecté: %s\n", client_id);
            // Supprimer le client et son token
            remove_client(client_id);
        }
    }
}

int main(int argc, char *argv[]) {
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    
    int port = 8000;
    
    // Parser les arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--token") == 0 || strcmp(argv[i], "-t") == 0) {
            s_user_token_enabled = 1;
        } else if (strcmp(argv[i], "--admin-token") == 0 || strcmp(argv[i], "-a") == 0) {
            s_admin_token_enabled = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [OPTIONS] [PORT]\n\n", argv[0]);
            printf("OPTIONS:\n");
            printf("  -t, --token        Active l'authentification utilisateur (génère un token)\n");
            printf("  -a, --admin-token  Active l'authentification admin (génère un token admin)\n");
            printf("  -h, --help         Affiche cette aide\n\n");
            printf("PORT:\n");
            printf("  Port d'écoute (défaut: 8000)\n\n");
            printf("EXEMPLES:\n");
            printf("  %s                    # API ouverte sur port 8000\n", argv[0]);
            printf("  %s -t                 # Avec token utilisateur\n", argv[0]);
            printf("  %s -t -a              # Avec tokens user + admin\n", argv[0]);
            printf("  %s -t -a 9000         # Sur port 9000\n", argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            port = atoi(argv[i]);
            if (port <= 0 || port > 65535) {
                printf("Erreur: Port invalide '%s'\n", argv[i]);
                return 1;
            }
        }
    }
    
    // Générer les tokens si activés
    if (s_user_token_enabled) {
        printf("\n🔐 \033[1;32mTokens Utilisateurs:\033[0m Activés (unique par client)\n");
    }
    if (s_admin_token_enabled) {
        generate_secure_token(s_admin_token, sizeof(s_admin_token));
        printf("👑 \033[1;33mToken Admin:\033[0m       %s\n", s_admin_token);
        
        // Charger les credentials admin depuis le fichier
        if (load_admin_credentials()) {
            printf("🔑 \033[1;35mLogin admin:\033[0m       %s (depuis %s)\n", s_admin_user, s_credentials_file);
        } else {
            printf("⚠️  Fichier %s non trouvé - login admin désactivé\n", s_credentials_file);
            printf("   Créez le fichier avec: echo 'user:sha256hash' > %s\n", s_credentials_file);
        }
    }
    if (!s_user_token_enabled && !s_admin_token_enabled) {
        printf("\n⚠️  \033[1;31mAttention: API ouverte à tous!\033[0m Utilisez --token pour activer l'auth.\n");
    }
    printf("\n");
    
    // Création du dossier uploads s'il n'existe pas
    mkdir(s_upload_dir, 0755);

    char listen_on[64];
    snprintf(listen_on, sizeof(listen_on), "ws://0.0.0.0:%d", port);

    printf("🚀 Serveur démarré sur \033[1;36m%s\033[0m\n", listen_on);
    
    // Démarrer le serveur Pong UDP
    start_pong_server();
    
    printf("\n");
    mg_http_listen(&mgr, listen_on, fn, NULL);
    
    for (;;) mg_mgr_poll(&mgr, 1000);
    
    stop_pong_server();
    mg_mgr_free(&mgr);
    return 0;

}
