# WallChange API Documentation

Documentation complète de l'API REST du serveur WallChange.

## 🔐 Authentification

L'API utilise des tokens Bearer pour l'authentification.

### Types de tokens

| Type | Description | Obtention |
|------|-------------|-----------|
| **User Token** | Token unique par client | Reçu automatiquement à la connexion WebSocket |
| **Admin Token** | Token administrateur | Via `/api/login` ou affiché au démarrage du serveur |

### Utilisation

```bash
curl -H "Authorization: Bearer <token>" "http://server:port/api/endpoint"
```

---

## 📍 Endpoints

### `GET /api/login`

Authentification admin pour obtenir le token administrateur.

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `user` | string | Nom d'utilisateur admin |
| `pass` | string | Mot de passe admin |

**Réponse (200) :**
```json
{
  "status": "success",
  "token": "a5acaa6c7d717a280b0d0c168f92a77929c3ac1042c758598eaed01cb181b7d5",
  "type": "admin"
}
```

**Erreurs :**
- `400` - Paramètres manquants
- `401` - Identifiants invalides
- `503` - Auth admin non activée sur le serveur

**Exemple :**
```bash
curl "http://localhost:8000/api/login?user=admin&pass=monmotdepasse"
```

---

### `GET /api/version`

Retourne la version du serveur.

**Auth requise :** Non

**Réponse (200) :**
```
1.0.32
```

---

### `GET /api/list`

Liste tous les clients connectés.

**Auth requise :** Non

**Réponse (200) :**
```json
[
  {
    "id": "zekynux",
    "hostname": "pc-zekynux",
    "version": "1.0.41"
  },
  {
    "id": "alice",
    "hostname": "workstation-01",
    "version": "1.0.40"
  }
]
```

---

### `GET /api/send`

Envoie une image (URL) à un client pour changer son fond d'écran.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible |
| `url` | string | URL de l'image |

**Réponse (200) :**
```
Sent to 1 client(s)
```

**Erreurs :**
- `400` - Paramètres manquants
- `401` - Token invalide ou manquant
- `429` - Trop de requêtes (rate limit: 10s)

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/send?id=zekynux&url=https://example.com/image.jpg"
```

---

### `POST /api/upload`

Upload une image locale et l'envoie optionnellement à un client.

**Auth requise :** Oui (User ou Admin)

**Paramètres Query :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | (Optionnel) ID du client cible (ou `*` pour tous - admin) |
| `type` | string | (Optionnel) Type d'action : `wallpaper` (défaut), `marquee` ou `particles` |

**Body :** `multipart/form-data` avec le fichier image

**Réponse (200) :**
```
Uploaded and sent to 1 client(s)
```
ou
```
Uploaded but no target id provided
```

**Exemples curl :**

```bash
# Envoyer une image locale comme fond d'écran
curl -H "Authorization: Bearer $TOKEN" \
  -F "file=@/chemin/vers/image.jpg" \
  "http://localhost:8000/api/upload?id=zakburak"

# Envoyer une image locale en marquee (défilement)
curl -H "Authorization: Bearer $TOKEN" \
  -F "file=@/chemin/vers/image.png" \
  "http://localhost:8000/api/upload?id=zakburak&type=marquee"

# Envoyer un GIF local en marquee
curl -H "Authorization: Bearer $TOKEN" \
  -F "file=@./mon-gif.gif" \
  "http://localhost:8000/api/upload?id=zakburak&type=marquee"

# Envoyer une image pour l'effet particules
curl -H "Authorization: Bearer $TOKEN" \
  -F "file=@./particle.png" \
  "http://localhost:8000/api/upload?id=zakburak&type=particles"

# Envoyer à tous les clients (admin)
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  -F "file=@./wallpaper.jpg" \
  "http://localhost:8000/api/upload?id=*"

# Upload sans envoi (juste stocker sur le serveur)
curl -H "Authorization: Bearer $TOKEN" \
  -F "file=@./image.jpg" \
  "http://localhost:8000/api/upload"
```

**Notes :**
- Le fichier est uploadé dans le dossier `uploads/` du serveur
- L'URL générée est automatiquement envoyée au client cible
- Formats supportés : JPG, PNG, GIF (animé inclus), BMP, etc.

---

### `GET /api/update`

Demande à un client de se mettre à jour.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible |

**Réponse (200) :**
```
Update request sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/update?id=zekynux"
```

---

### `GET /api/reinstall`

Demande à un client de se réinstaller complètement (désinstallation + réinstallation via script).

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible |

**Réponse (200) :**
```
Reinstall request sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/reinstall?id=zekynux"
```

---

### `GET /api/uninstall`

Désinstalle le client WallChange sur une machine.

**Auth requise :** 
- **Admin** pour désinstaller n'importe quel client
- **User** pour se désinstaller soi-même uniquement

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible |
| `from` | string | ID de l'utilisateur qui fait la demande |

**Réponse (200) :**
```
Uninstall request sent to 1 client(s)
```

**Erreurs :**
- `401` - Token invalide ou manquant
- `403` - Token admin requis pour désinstaller un autre client

**Exemple (admin) :**
```bash
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/uninstall?id=alice&from=admin"
```

**Exemple (self) :**
```bash
curl -H "Authorization: Bearer $USER_TOKEN" \
  "http://localhost:8000/api/uninstall?id=zekynux&from=zekynux"
```

---

### `GET /api/key`

Envoie un raccourci clavier à un client.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible |
| `combo` | string | Combinaison de touches (ex: `ctrl+alt+t`) |

**Touches supportées :**
- Modificateurs : `ctrl`, `alt`, `shift`, `super`
- Lettres : `a-z`
- Chiffres : `0-9`
- Touches spéciales : `space`, `enter`, `tab`, `escape`, `backspace`, `delete`, `home`, `end`, `pageup`, `pagedown`, `left`, `right`, `up`, `down`, `f1-f12`

**Réponse (200) :**
```
Key 'ctrl+alt+t' sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/key?id=zekynux&combo=ctrl+alt+t"
```

---

### `GET /api/showdesktop`

Minimise toutes les fenêtres (Super+D) sur un client.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible |

**Réponse (200) :**
```
Showdesktop sent to 1 client(s)
```

---

### `GET /api/reverse`

Inverse l'écran du client pendant 3 secondes.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible |

**Réponse (200) :**
```
Reverse sent to 1 client(s)
```

---

### `GET /api/marquee`

Fait défiler une image de droite à gauche sur l'écran du client. Supporte les images statiques (PNG, JPG) et les GIFs animés.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous - admin uniquement) |
| `url` | string | URL de l'image |

**Réponse (200) :**
```
Marquee sent to 1 client(s)
```

**Exemples curl :**

```bash
# Envoyer un marquee à un utilisateur spécifique
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/marquee?id=zakburak&url=https://example.com/image.png"

# Envoyer un GIF animé
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/marquee?id=zakburak&url=https://media.tenor.com/xxx/among-us.gif"

# Envoyer à TOUS les clients (admin uniquement)
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/marquee?id=*&url=https://example.com/image.png"
```

---

### `GET /api/particles`

Affiche des particules autour du curseur de la souris pendant 5 secondes. L'image fournie est utilisée comme texture de particule (redimensionnée à 48x48 pixels).

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous - admin uniquement) |
| `url` | string | URL de l'image de la particule |

**Réponse (200) :**
```
Particles sent to 1 client(s)
```

**Exemples curl :**

```bash
# Envoyer des particules à un utilisateur
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/particles?id=zakburak&url=https://example.com/star.png"

# Envoyer à tous les clients (admin)
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/particles?id=*&url=https://example.com/heart.png"

# Envoyer une image locale comme particule
curl -H "Authorization: Bearer $TOKEN" \
  -F "file=@./star.png" \
  "http://localhost:8000/api/upload?id=zakburak&type=particles"
```

**Notes :**
- L'effet dure exactement 5 secondes
- Les particules suivent le curseur et sont émises avec une physique réaliste (gravité, vélocité)
- Supporte la transparence PNG (via XShape)
- Les images sont automatiquement redimensionnées à 48x48 pixels
- Pour envoyer un fichier local, utiliser `/api/upload` avec `type=particles`

---

### `GET /api/clones`

Affiche 100 clones du curseur de la souris qui suivent le vrai curseur avec un léger décalage, créant une confusion visuelle. Le vrai curseur est masqué pendant l'effet.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous - admin uniquement) |

**Réponse (200) :**
```
Clones sent to 1 client(s)
```

**Exemples curl :**

```bash
# Envoyer des clones à un utilisateur
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/clones?id=zakburak"

# Envoyer à tous les clients (admin)
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/clones?id=*"
```

**Notes :**
- L'effet dure exactement 5 secondes
- 100 clones du curseur apparaissent autour de la souris
- Les clones suivent le curseur avec un effet de traîne (interpolation)
- Le vrai curseur est masqué pendant l'effet
- L'image du curseur actuel est capturée automatiquement via XFixes

---

### `GET /api/drunk`

Rend le curseur de la souris "ivre" pendant 10 secondes. Le curseur bouge de manière aléatoire autour de sa position réelle, rendant le contrôle difficile.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous - admin uniquement) |

**Réponse (200) :**
```
Drunk mode sent to 1 client(s)
```

**Exemples curl :**

```bash
# Envoyer l'effet drunk à un utilisateur
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/drunk?id=zakburak"

# Envoyer à tous les clients (admin)
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/drunk?id=*"
```

**Notes :**
- L'effet dure 10 secondes
- Le curseur subit des déplacements aléatoires (jitter)
- L'utilisateur garde le contrôle global mais la précision est fortement réduite

---

### `GET /api/faketerminal`

Affiche un faux terminal en plein écran avec du texte vert qui défile (effet Matrix) pendant 10 secondes.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous - admin uniquement) |

**Réponse (200) :**
```
Faketerminal sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/faketerminal?id=zakburak"
```

---

### `GET /api/confetti`

Fait pleuvoir des confettis (ou une image personnalisée) sur l'écran pendant 10 secondes.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous - admin uniquement) |
| `url` | string | (Optionnel) URL d'une image à utiliser comme confetti |

**Réponse (200) :**
```
Confetti sent to 1 client(s)
```

**Exemples :**
```bash
# Confettis classiques (carrés colorés)
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/confetti?id=zakburak"

# Pluie d'images personnalisées
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/confetti?id=zakburak&url=https://example.com/trollface.png"
```

---

### `GET /api/spotlight`

Assombrit tout l'écran sauf un cercle lumineux autour du curseur de la souris (effet lampe torche) pendant 10 secondes.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous - admin uniquement) |

**Réponse (200) :**
```
Spotlight sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/spotlight?id=zakburak"
```

---

### `GET /api/textscreen`

Affiche un texte ou emoji en énorme au centre de l'écran pendant 5 secondes.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous - admin uniquement) |
| `text` | string | (Optionnel) Texte à afficher (défaut: "HELLO WORLD") |

**Réponse (200) :**
```
Textscreen sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/textscreen?id=zakburak&text=HACKED"
```

---

### `GET /api/wavescreen`

Applique un effet d'onde (distorsion sinusoïdale) sur tout l'écran pendant 10 secondes.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous - admin uniquement) |

**Réponse (200) :**
```
Wavescreen sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/wavescreen?id=zakburak"
```

---

### `GET /api/dvdbounce`

Fait rebondir un logo (ou image) sur les bords de l'écran comme le logo DVD pendant 15 secondes.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous - admin uniquement) |
| `url` | string | (Optionnel) URL de l'image à faire rebondir |

**Réponse (200) :**
```
DVD Bounce sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/dvdbounce?id=zakburak"
```

---

### `GET /api/fireworks`

Affiche des feux d'artifice à l'écran pendant 15 secondes. Les clics de souris déclenchent des explosions.

**Auth requise :** Oui (User ou Admin)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous - admin uniquement) |

**Réponse (200) :**
```
Fireworks sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/fireworks?id=zakburak"
```

---

### `GET /api/lock`

Verrouille la session de l'utilisateur (retour à l'écran de connexion).

**Auth requise :** Oui (Admin uniquement)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous) |

**Réponse (200) :**
```
Lock sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/lock?id=zakburak"
```

---

## 🌟 Wildcard (Admin)

L'admin peut utiliser `*` comme `id` pour envoyer une commande à **tous les clients connectés**.

**Endpoints supportés :** `send`, `upload`, `update`, `showdesktop`, `reverse`, `key`, `marquee`, `particles`, `clones`, `drunk`, `faketerminal`, `confetti`, `spotlight`, `textscreen`, `wavescreen`, `dvdbounce`, `fireworks`, `lock`

**Exemples :**

```bash
# Changer le fond d'écran de tout le monde
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/send?id=*&url=https://example.com/wallpaper.jpg"

# Inverser l'écran de tout le monde
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/reverse?id=*"

# Mettre à jour tous les clients
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/update?id=*"

# Envoyer un raccourci clavier à tous
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/key?id=*&combo=super+d"

# Effet particules sur tous les clients
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/particles?id=*&url=https://example.com/star.png"

# Clones de souris sur tous les clients
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/clones?id=*"
```

**Note :** Le wildcard `*` nécessite le token **admin**, pas un simple token utilisateur.

---

## 🌐 WebSocket

Les clients se connectent via WebSocket à `ws://server:port/{username}`.

### Messages reçus par le client

**Authentification (à la connexion) :**
```json
{
  "type": "auth",
  "token": "ee0e9738489d3313645f4328f49aa7f498f43765fa7389eb35d56a277d497b15"
}
```

**Changement de fond d'écran :**
```json
{
  "url": "https://example.com/image.jpg"
}
```

**Commandes :**
```json
{"command": "update"}
{"command": "uninstall", "from": "admin"}
{"command": "showdesktop"}
{"command": "reverse"}
{"command": "marquee", "url": "https://example.com/image.png"}
{"command": "particles", "url": "https://example.com/particle.png"}
{"command": "clones"}
{"command": "drunk"}
{"command": "faketerminal"}
{"command": "confetti", "url": "https://example.com/image.png"}
{"command": "spotlight"}
{"command": "textscreen", "text": "HELLO"}
{"command": "wavescreen"}
{"command": "dvdbounce", "url": "https://example.com/dvd.png"}
{"command": "fireworks"}
{"command": "key", "combo": "ctrl+alt+t"}
```

### Messages envoyés par le client

**Infos système (toutes les 60s) :**
```json
{
  "type": "info",
  "hostname": "pc-zekynux",
  "os": "Ubuntu 24.04.3 LTS",
  "uptime": "3h 27m",
  "cpu": "1.51, 1.49, 1.41",
  "ram": "9785/15623MB (62%)",
  "version": "1.0.41"
}
```

---

## 📡 Logs en direct (WebSocket)

Protocole spécifique pour visualiser les logs d'un client en temps réel.

**URL :** `ws://server:port/admin-watcher-{random_id}`

### 1. Authentification Admin

Dès la connexion établie, le client "watcher" doit s'authentifier en tant qu'admin.

**Envoi :**
```json
{
  "type": "auth_admin",
  "token": "ADMIN_TOKEN"
}
```

**Réponse :**
```json
{
  "type": "auth_success"
}
```

### 2. Abonnement (Subscribe)

Une fois authentifié, le watcher demande à recevoir les logs d'une cible.

**Envoi :**
```json
{
  "type": "subscribe",
  "target": "target_username"
}
```

Le serveur envoie alors la commande `start_logs` au client cible.

### 3. Réception des logs

Le serveur transfère les logs bruts reçus du client cible vers le watcher.

**Format :**
Les données sont envoyées telles quelles (texte brut) dans le payload WebSocket.

---

## 📸 Capture d'écran

Système de demande et de récupération de capture d'écran d'un client.

### `GET /api/screenshot`

Demande à un client de prendre une capture d'écran et de l'uploader sur le serveur.

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous) |

**Headers requis :**
`Authorization: Bearer <token>`

**Réponse (200) :**
```json
{
  "status": "success",
  "sent_to": 1
}
```

**Notes :**
- La commande est asynchrone. Le client reçoit l'ordre, capture l'écran, puis l'upload via `/api/upload_screenshot`.
- Il faut attendre quelques secondes avant que l'image ne soit disponible.

### `POST /api/upload_screenshot`

Endpoint utilisé par le client pour uploader sa capture d'écran.

**Paramètres URL :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client qui upload |

**Body (Multipart/form-data) :**
| Champ | Type | Description |
|-------|------|-------------|
| `file` | file | Le fichier image (JPG) |

**Headers requis :**
`Authorization: Bearer <token>` (Token du client ou admin)

**Réponse (200) :**
```text
Screenshot uploaded
```

### Accès aux captures

Les captures sont accessibles publiquement (via CORS) une fois uploadées.

**URL :**
`GET /uploads/screenshots/<client_id>.jpg`

**Exemple :**
`https://wallchange.codeky.fr/uploads/screenshots/zakburak.jpg`

**Headers de réponse :**
- `Access-Control-Allow-Origin: *`
- `Cross-Origin-Resource-Policy: cross-origin`
- `Cache-Control: no-cache` (recommandé d'ajouter un timestamp en query param pour forcer le rafraîchissement)

### `GET /api/screen-off`

Eteint l'écran du client pendant une durée déterminée.

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous) |
| `duration` | int | Durée en secondes (Admin seulement, défaut: 3) |

**Headers requis :**
`Authorization: Bearer <token>`

**Comportement :**
- **Admin** : Peut spécifier `duration`. Si omis, défaut 3s.
- **Utilisateur (Non-Admin)** : `duration` forcé à 3s, paramètre ignoré.

**Réponse (200) :**
```text
Screen off command sent to 1 client(s)
```

---

## ⚠️ Rate Limiting

- **Cooldown par cible :** 10 secondes entre chaque requête vers le même client
- **Réponse :** `429 Too Many Requests`

---

## 🚀 Démarrage du serveur

```bash
# API ouverte (pas d'auth)
./server

# Avec tokens utilisateurs uniques
./server -t

# Avec tokens users + admin
./server -t -a

# Sur un port spécifique
./server -t -a 4242

# Avec pm2
pm2 start ./server --name "wallchange" -- -t -a 4242

# Avec notifications Discord
export DISCORD_WEBHOOK_URL="https://discord.com/api/webhooks/YOUR_WEBHOOK_URL"
./server -t -a
```

**Options :**
| Option | Description |
|--------|-------------|
| `-t, --token` | Active les tokens utilisateurs (uniques par client) |
| `-a, --admin-token` | Active le token admin + login |
| `-h, --help` | Affiche l'aide |
| `PORT` | Port d'écoute (défaut: 8000) |

**Variables d'environnement :**
| Variable | Description |
|----------|-------------|
| `DISCORD_WEBHOOK_URL` | URL du webhook Discord pour les notifications de déconnexion |

---

## 🔔 Notifications Discord

Le serveur peut envoyer des notifications Discord lorsqu'un client se déconnecte.

**Configuration :**

1. Créez un webhook Discord dans les paramètres de votre serveur/canal
2. Exportez l'URL du webhook :
   ```bash
   export DISCORD_WEBHOOK_URL="https://discord.com/api/webhooks/XXXX/YYYY"
   ```
3. Démarrez le serveur normalement

**Contenu des notifications :**
- 🔴 Icône de déconnexion
- ID du client
- Hostname du client (si disponible)
- Horodatage de la déconnexion

**Exemple de notification :**
```
🔴 Client déconnecté

ID: zakburak
Hostname: pc-zakburak
Heure: 2024-02-08 14:30:45
```
