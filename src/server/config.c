#include "config.h"
#include <stdlib.h>

struct default_env_item {
    const char *key;
    const char *value;
};

static const struct default_env_item g_default_envs[] = {
    {"WALLCHANGE_CORS_ORIGIN", "https://wall.codeky.fr"},
    {"WALLCHANGE_TOKEN_TTL_SECONDS", "86400"},
    {"WALLCHANGE_REQUIRE_TLS", "0"},
    {"WALLCHANGE_ALLOW_WEAK_WS_ID", "0"},
    {"WALLCHANGE_CLIENT_SECRET", ""},
    {"WALLCHANGE_DISCORD_WEBHOOK_URL", ""},
    {"WALLCHANGE_UPDATE_PINNED_COMMIT", ""}
};

const char *wc_getenv_default(const char *key, const char *default_value) {
    const char *value = getenv(key);
    if (value && value[0] != '\0') {
        return value;
    }
    return default_value;
}

void wc_apply_default_environment(void) {
    for (size_t i = 0; i < sizeof(g_default_envs) / sizeof(g_default_envs[0]); i++) {
        const char *current = getenv(g_default_envs[i].key);
        if (!current || current[0] == '\0') {
            setenv(g_default_envs[i].key, g_default_envs[i].value, 0);
        }
    }
}