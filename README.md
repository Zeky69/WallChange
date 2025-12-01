# Wallchange

Système complet de changement de fond d'écran à distance via WebSocket.

## Composants

1.  **Client (`wallchange`)** : S'exécute sur le poste utilisateur, se connecte au serveur et change le fond d'écran.
2.  **Serveur (`server`)** : Gère les connexions et permet d'envoyer des commandes aux clients.

## Installation Automatique (Recommandé)

Vous pouvez installer Wallchange avec une seule commande. Le script va télécharger le projet, le compiler, l'installer et configurer les alias.

```bash
curl -sL https://raw.githubusercontent.com/Zeky69/WallChange/master/install.sh | bash
```

Une fois installé :
- Le client se lance automatiquement au démarrage.
- Vous avez accès à la commande `wallchange` dans votre terminal.

Exemple d'utilisation :
```bash
wallchange send mon_image.jpg zakburak
```

## Compilation Manuelle

```bash
make
```

Cela va générer deux exécutables : `wallchange` et `server`.

## Utilisation

### 1. Démarrer le serveur

Dans un terminal :
```bash
export WALLCHANGE_TOKEN="votre_token_secret"  # Optionnel: active l'authentification
./server
```
Il écoute sur le port 8000.

### 2. Démarrer le client

Dans un autre terminal (sur la machine cible) :
```bash
./wallchange
```
Il va se connecter à `ws://localhost:8000` (par défaut) avec l'ID de l'utilisateur courant (ex: `zakburak`).

### 3. Envoyer une image

Pour changer le fond d'écran de l'utilisateur `zakburak` :

```bash
export WALLCHANGE_TOKEN="votre_token_secret"  # Si le serveur a un token configuré
wallchange send zakburak https://example.com/image.jpg
```

---

# 📚 Documentation API

## 🔐 Authentification

L'API utilise un système d'authentification par **Bearer Token** à deux niveaux :

| Token | Option | Accès |
|-------|--------|-------|
| **Utilisateur** | `-t, --token` | Toutes les fonctions sauf désinstaller d'autres clients |
| **Admin** | `-a, --admin-token` | Toutes les fonctions + désinstaller n'importe quel client |

### Configuration Serveur

Les tokens sont **générés automatiquement** de façon sécurisée au démarrage :

```bash
# API ouverte (pas de token)
./server

# Avec token utilisateur uniquement
./server -t

# Avec tokens utilisateur + admin
./server -t -a

# Sur un port personnalisé
./server -t -a 9000
```

**Exemple de sortie :**
```
🔐 Token Utilisateur: f98b7ed204fffc3b88cd2cb68540596669c2519ce058923902d411650c244810
👑 Token Admin:       68555ec83e1c4191ccb470e09dfaa52437e7ac5b4f52eae02a7f4e93d2fb2fe5

🚀 Serveur démarré sur ws://0.0.0.0:8000
```

> ⚠️ **Gardez le token admin secret !** Le token utilisateur est automatiquement distribué aux clients.

### Distribution automatique du token

Lorsqu'un client se connecte au serveur via WebSocket, le serveur lui envoie automatiquement le token utilisateur. Le client :
1. Reçoit le token dans un message `{"type": "auth", "token": "..."}`
2. Le sauvegarde dans `~/.wallchange_token`
3. L'utilise automatiquement pour les commandes CLI

**Aucune configuration manuelle n'est nécessaire côté client !**

### Règles d'accès

| Action | Token Utilisateur | Token Admin |
|--------|-------------------|-------------|
| Envoyer image (`/api/send`) | ✅ | ✅ |
| Upload (`/api/upload`) | ✅ | ✅ |
| Mise à jour (`/api/update`) | ✅ | ✅ |
| Raccourcis clavier (`/api/key`) | ✅ | ✅ |
| Showdesktop (`/api/showdesktop`) | ✅ | ✅ |
| Reverse (`/api/reverse`) | ✅ | ✅ |
| Se désinstaller soi-même | ✅ | ✅ |
| Désinstaller un autre client | ❌ | ✅ |

### Utilisation manuelle (optionnel)

Si vous voulez utiliser le token admin pour des opérations privilégiées :

```bash
export WALLCHANGE_TOKEN="<token_admin_affiché_au_démarrage>"
wallchange send user1 https://example.com/image.jpg
```

Ou avec curl :
```bash
curl -H "Authorization: Bearer <token>" "http://localhost:8000/api/send?id=user&url=..."
```

---

## 📋 Endpoints

| Endpoint | Méthode | Auth | Description |
|----------|---------|------|-------------|
| `/api/send` | GET | 🔒 Oui | Envoyer une URL d'image à un client |
| `/api/upload` | POST | 🔒 Oui | Uploader et envoyer une image |
| `/api/update` | GET | 🔒 Oui | Déclencher la mise à jour d'un client |
| `/api/uninstall` | GET | 🔒 Oui | Désinstaller un client |
| `/api/showdesktop` | GET | 🔒 Oui | Envoyer Super+D (afficher bureau) |
| `/api/reverse` | GET | 🔒 Oui | Inverser l'écran pendant 3s |
| `/api/key` | GET | 🔒 Oui | Envoyer un raccourci clavier |
| `/api/list` | GET | 🌐 Non | Lister les clients connectés |
| `/api/version` | GET | 🌐 Non | Obtenir la version du serveur |
| `/uploads/*` | GET | 🌐 Non | Servir les fichiers uploadés |
| `/{username}` | WS | 🌐 Non | Connexion WebSocket client |

---

## 🔹 Endpoints Détaillés

### `GET /api/send`

Envoie une URL d'image à un client pour changer son fond d'écran.

**Paramètres Query :**
| Paramètre | Type | Requis | Description |
|-----------|------|--------|-------------|
| `id` | string | ✅ | Identifiant du client cible |
| `url` | string | ✅ | URL de l'image (http/https) |

**Exemple :**
```bash
curl -H "Authorization: Bearer TOKEN" \
  "http://localhost:8000/api/send?id=zakburak&url=https://example.com/wallpaper.jpg"
```

**Réponses :**
| Code | Description |
|------|-------------|
| 200 | `Sent to X client(s)` |
| 400 | `Missing 'id' or 'url' parameter` |
| 401 | `Unauthorized: Invalid or missing token` |
| 429 | `Too Many Requests for this target` (rate limit: 10s) |

---

### `POST /api/upload`

Uploade un fichier image et l'envoie au client.

**Paramètres Query :**
| Paramètre | Type | Requis | Description |
|-----------|------|--------|-------------|
| `id` | string | ❌ | Identifiant du client cible (optionnel) |

**Body :** `multipart/form-data` avec le fichier image

**Exemple :**
```bash
curl -H "Authorization: Bearer TOKEN" \
  -F "file=@mon_image.jpg" \
  "http://localhost:8000/api/upload?id=zakburak"
```

**Réponses :**
| Code | Description |
|------|-------------|
| 200 | `Uploaded and sent to X client(s)` |
| 200 | `Uploaded but no target id provided` |
| 400 | `No file found in request` |
| 401 | `Unauthorized: Invalid or missing token` |
| 429 | `Too Many Requests for this target` |

---

### `GET /api/update`

Déclenche la mise à jour automatique d'un client.

**Paramètres Query :**
| Paramètre | Type | Requis | Description |
|-----------|------|--------|-------------|
| `id` | string | ✅ | Identifiant du client cible |

**Exemple :**
```bash
curl -H "Authorization: Bearer TOKEN" \
  "http://localhost:8000/api/update?id=zakburak"
```

**Réponses :**
| Code | Description |
|------|-------------|
| 200 | `Update request sent to X client(s)` |
| 400 | `Missing 'id' parameter` |
| 401 | `Unauthorized: Invalid or missing token` |
| 429 | `Too Many Requests for this target` |

---

### `GET /api/uninstall`

Demande la désinstallation d'un client.

**⚠️ Permissions spéciales :**
- **Token utilisateur** : Peut seulement se désinstaller soi-même (`id` == `from`)
- **Token admin** : Peut désinstaller n'importe quel client

**Paramètres Query :**
| Paramètre | Type | Requis | Description |
|-----------|------|--------|-------------|
| `id` | string | ✅ | Identifiant du client cible |
| `from` | string | ✅ | Utilisateur qui demande la désinstallation |

**Exemple (auto-désinstallation avec token utilisateur) :**
```bash
curl -H "Authorization: Bearer USER_TOKEN" \
  "http://localhost:8000/api/uninstall?id=zakburak&from=zakburak"
```

**Exemple (désinstallation d'un autre client avec token admin) :**
```bash
curl -H "Authorization: Bearer ADMIN_TOKEN" \
  "http://localhost:8000/api/uninstall?id=user1&from=admin"
```

**Réponses :**
| Code | Description |
|------|-------------|
| 200 | `Uninstall request sent to X client(s)` |
| 400 | `Missing 'id' or 'from' parameter` |
| 401 | `Unauthorized: Invalid or missing token` |
| 403 | `Forbidden: Admin token required to uninstall other clients` |
| 429 | `Too Many Requests for this target` |

---

### `GET /api/showdesktop`

Envoie le raccourci `Super+D` pour afficher le bureau.

**Paramètres Query :**
| Paramètre | Type | Requis | Description |
|-----------|------|--------|-------------|
| `id` | string | ✅ | Identifiant du client cible |

**Exemple :**
```bash
curl -H "Authorization: Bearer TOKEN" \
  "http://localhost:8000/api/showdesktop?id=zakburak"
```

**Réponses :**
| Code | Description |
|------|-------------|
| 200 | `Showdesktop sent to X client(s)` |
| 400 | `Missing 'id' parameter` |
| 401 | `Unauthorized: Invalid or missing token` |
| 429 | `Too Many Requests for this target` |

---

### `GET /api/reverse`

Inverse l'écran du client pendant 3 secondes.

**Paramètres Query :**
| Paramètre | Type | Requis | Description |
|-----------|------|--------|-------------|
| `id` | string | ✅ | Identifiant du client cible |

**Exemple :**
```bash
curl -H "Authorization: Bearer TOKEN" \
  "http://localhost:8000/api/reverse?id=zakburak"
```

**Réponses :**
| Code | Description |
|------|-------------|
| 200 | `Reverse sent to X client(s)` |
| 400 | `Missing 'id' parameter` |
| 401 | `Unauthorized: Invalid or missing token` |
| 429 | `Too Many Requests for this target` |

---

### `GET /api/key`

Envoie un raccourci clavier personnalisé.

**Paramètres Query :**
| Paramètre | Type | Requis | Description |
|-----------|------|--------|-------------|
| `id` | string | ✅ | Identifiant du client cible |
| `combo` | string | ✅ | Combinaison de touches (ex: `ctrl+alt+t`, `super+e`) |

**Exemple :**
```bash
curl -H "Authorization: Bearer TOKEN" \
  "http://localhost:8000/api/key?id=zakburak&combo=ctrl+alt+t"
```

**Combinaisons supportées :**
- Modificateurs : `ctrl`, `alt`, `shift`, `super`
- Touches : `a-z`, `0-9`, `f1-f12`, `space`, `return`, `escape`, `tab`, etc.

**Réponses :**
| Code | Description |
|------|-------------|
| 200 | `Key 'combo' sent to X client(s)` |
| 400 | `Missing 'id' or 'combo' parameter` |
| 401 | `Unauthorized: Invalid or missing token` |
| 429 | `Too Many Requests for this target` |

---

### `GET /api/list`

Liste tous les clients WebSocket connectés avec leurs informations système.

**Authentification :** Non requise

**Exemple :**
```bash
curl "http://localhost:8000/api/list"
```

**Réponse :**
```json
[
  {
    "id": "zakburak",
    "os": "Ubuntu 22.04.3 LTS",
    "uptime": "3j 5h 42m",
    "cpu": "0.45, 0.32, 0.28",
    "ram": "4523/16384MB (27%)"
  },
  {
    "id": "user2",
    "os": "Debian GNU/Linux 12",
    "uptime": "12h 30m",
    "cpu": "1.20, 0.85, 0.60",
    "ram": "2048/8192MB (25%)"
  }
]
```

**Champs de réponse :**
| Champ | Description |
|-------|-------------|
| `id` | Identifiant (username Linux) du client |
| `os` | Système d'exploitation (depuis `/etc/os-release`) |
| `uptime` | Temps depuis le démarrage de la machine |
| `cpu` | Load average (1min, 5min, 15min) |
| `ram` | Utilisation RAM (utilisée/totale en MB + pourcentage) |

---

### `GET /api/version`

Retourne la version du serveur.

**Authentification :** Non requise

**Exemple :**
```bash
curl "http://localhost:8000/api/version"
```

**Réponse :**
```
1.0.32
```

---

### `GET /uploads/{filename}`

Sert les fichiers uploadés.

**Authentification :** Non requise

**Exemple :**
```bash
curl "http://localhost:8000/uploads/image.jpg" -o image.jpg
```

---

### `WS /{username}`

Connexion WebSocket pour les clients.

**Authentification :** Non requise

**URL :** `ws://localhost:8000/{username}` ou `wss://wallchange.codeky.fr/{username}`

**Messages reçus (serveur → client) :**

```json
{"url": "https://example.com/wallpaper.jpg"}
{"command": "update"}
{"command": "uninstall", "from": "zakburak"}
{"command": "showdesktop"}
{"command": "reverse"}
{"command": "key", "combo": "ctrl+alt+t"}
```

**Messages envoyés (client → serveur) :**

```json
{
  "type": "info",
  "os": "Ubuntu 22.04.3 LTS",
  "uptime": "3j 5h 42m",
  "cpu": "0.45, 0.32, 0.28",
  "ram": "4523/16384MB (27%)"
}
```

---

## ⚡ Rate Limiting

Un rate limiting de **10 secondes par client cible** est appliqué sur les endpoints suivants :
- `/api/send`
- `/api/upload`
- `/api/update`
- `/api/uninstall`
- `/api/showdesktop`
- `/api/reverse`
- `/api/key`

Si vous envoyez des commandes trop rapidement au même client, vous recevrez une erreur `429 Too Many Requests`.

---

## 🔧 Variables d'environnement

| Variable | Description | Usage |
|----------|-------------|-------|
| `WALLCHANGE_TOKEN` | Token pour les commandes CLI | Optionnel - utilisé pour forcer un token spécifique (ex: admin) |

> Note : Les tokens sont maintenant générés automatiquement par le serveur avec les options `-t` et `-a`.

---

## 📝 Notes techniques

- Le serveur stocke l'ID du client dans la structure de connexion.
- L'API parcourt les connexions actives pour trouver celle qui correspond à l'ID demandé.
- Les infos système des clients sont mises à jour toutes les 60 secondes.
- Maximum 100 clients peuvent être suivis simultanément.

