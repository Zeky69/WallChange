#include "discord_notify.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <time.h>

// URL du webhook Discord (configurable via variable d'environnement)
static const char *get_discord_webhook_url() {
    static const char *webhook_url = NULL;
    if (webhook_url == NULL) {
        webhook_url = getenv("DISCORD_WEBHOOK_URL");
        if (webhook_url == NULL) {
            // URL par défaut fournie par l'utilisateur
            webhook_url = "https://discordapp.com/api/webhooks/1469883921360748616/eQ9UZyWJhvJ38WFEgE_PhPAMh9dFQ7880Csu1835iKCDT6643yZrMFV5MEJvsuYqe8L6";
        }
    }
    return webhook_url;
}

// Callback pour ignorer la réponse de Discord
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    (void)contents;
    (void)userp;
    return size * nmemb;
}

void send_discord_notification(const char *client_id, const char *hostname) {
    const char *webhook_url = get_discord_webhook_url();
    
    // Ne pas envoyer de notification si aucun webhook n'est configuré
    if (webhook_url == NULL || strlen(webhook_url) == 0) {
        return;
    }
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Erreur: Impossible d'initialiser CURL pour la notification Discord\n");
        return;
    }
    
    // Créer le message JSON pour Discord
    cJSON *root = cJSON_CreateObject();
    
    // Obtenir l'horodatage actuel
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Créer le contenu du message
    char content[512];
    if (hostname && strlen(hostname) > 0) {
        snprintf(content, sizeof(content), 
                 "🔴 **Client déconnecté**\n\n**ID:** %s\n**Hostname:** %s\n**Heure:** %s",
                 client_id, hostname, timestamp);
    } else {
        snprintf(content, sizeof(content), 
                 "🔴 **Client déconnecté**\n\n**ID:** %s\n**Heure:** %s",
                 client_id, timestamp);
    }
    
    cJSON_AddStringToObject(root, "content", content);
    
    // Convertir en chaîne JSON
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_string) {
        curl_easy_cleanup(curl);
        return;
    }
    
    // Configurer la requête CURL
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, webhook_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_string);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    // Envoyer la requête
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Erreur lors de l'envoi de la notification Discord: %s\n", 
                curl_easy_strerror(res));
    }
    
    // Nettoyage
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(json_string);
}
