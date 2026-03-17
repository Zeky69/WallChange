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

### `POST /api/login`

Authentification admin pour obtenir le token administrateur.

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `user` | string | Nom d'utilisateur admin |
| `pass` | string | Mot de passe admin |

**Content-Type :** `application/x-www-form-urlencoded`

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
- `405` - Méthode invalide (GET non autorisé)
- `429` - Trop de tentatives de login
- `503` - Auth admin non activée sur le serveur

**Exemple :**
```bash
curl -X POST "http://localhost:8000/api/login" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  --data-urlencode "user=admin" \
  --data-urlencode "pass=monmotdepasse"
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

**Auth requise :** Oui (User ou Admin)

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
Uploaded (hash=9f8d8a4f...) and sent to 1 client(s)
```
ou
```
Uploaded unique image (hash=9f8d8a4f...), no target id provided
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
- Le serveur calcule un hash SHA-256 du contenu de chaque image uploadée
- Les images sont stockées en exemplaire unique dans `uploads/unique/` (même image = même hash)
- Si le même contenu est envoyé avec un nom différent, aucun doublon disque n'est créé
- L'URL générée est automatiquement envoyée au client cible
- Formats supportés : JPG, PNG, GIF (animé inclus), BMP, etc.

---

### `GET /api/stats`

Retourne les statistiques complètes : images + toutes les fonctionnalités + leaderboard utilisateurs.

**Auth requise :** Oui (User ou Admin)

**Réponse (200) :**
```json
{
  "version": 1,
  "created_at": 1760000000,
  "updated_at": 1760000100,
  "last_upload_at": 1760000095,
  "total_uploads": 42,
  "total_unique_images": 18,
  "total_duplicate_uploads": 24,
  "total_bytes_uploaded": 12345678,
  "total_client_deliveries": 205,
  "summary": {
    "total_uploads": 42,
    "total_unique_images": 18,
    "total_duplicate_uploads": 24,
    "total_bytes_uploaded": 12345678,
    "duplicate_ratio": 0.5714,
    "average_upload_size": 293944.7
  },
  "top_images": [
    {
      "hash": "9f8d8a4f...",
      "stored_path": "uploads/unique/9f8d8a4f....jpg",
      "upload_count": 12
    }
  ],
  "feature_stats": {
    "version": 1,
    "created_at": 1760000000,
    "updated_at": 1760000120,
    "total_commands": 330,
    "summary": {
      "total_commands": 330,
      "unique_users": 9,
      "feature_kinds": 24,
      "recent_events_count": 330
    },
    "leaderboards": {
      "top_users": [
        {
          "user": "admin",
          "total_commands": 121,
          "last_command": "upload"
        }
      ],
      "top_features": [
        {
          "feature": "upload",
          "count": 68
        }
      ]
    },
    "commands": {
      "upload": 68,
      "send": 55,
      "update": 21
    },
    "users": {
      "admin": {
        "display_name": "admin",
        "total_commands": 121
      }
    },
    "recent_events": [
      {
        "timestamp": 1760000115,
        "user": "admin",
        "command": "upload",
        "details": "Target: *"
      }
    ]
  ],
  "images": [
    {
      "hash": "9f8d8a4f...",
      "stored_path": "uploads/unique/9f8d8a4f....jpg",
      "original_name": "wallpaper.jpg",
      "mime": "image/jpeg",
      "size_bytes": 354120,
      "first_seen_at": 1760000002,
      "last_seen_at": 1760000095,
      "upload_count": 12
    }
  ]
}
```

**Ce qui est stocké en persistant :**
- Toutes les commandes API journalisées via `log_command`
- Les compteurs globaux par fonctionnalité
- Les compteurs par utilisateur
- Le leaderboard utilisateurs et fonctionnalités (calculé depuis le stockage)
- Un historique d'événements récents (`recent_events`)

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/stats"
```

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
  "http://localhost:8000/api/uninstall?id=alice"
```

**Exemple (self) :**
```bash
curl -H "Authorization: Bearer $USER_TOKEN" \
  "http://localhost:8000/api/uninstall?id=zekynux"
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

### `GET /api/blackout`

Éteint l'écran du client (brightness 0) pendant 20 minutes, puis rallume et verrouille la session.

**Auth requise :** Oui (Admin uniquement)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous) |

**Comportement :**
1. `xrandr --output eDP --brightness 0` → écran noir immédiat
2. Attente de **20 minutes**
3. `xrandr --output eDP --brightness 1` → écran rallumé
4. `dm-tool switch-to-greeter` → verrouillage de la session

**Réponse (200) :**
```
Blackout sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/blackout?id=zakburak"
```

---

### `GET /api/fakelock`

Affiche l'écran de verrouillage (codam-web-greeter) en mode visuel sans réellement verrouiller la session.

**Auth requise :** Oui (Admin uniquement)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous) |

**Comportement :**
- Lance `nody-greeter --mode debug --theme codam`
- Passe la fenêtre en fullscreen + always-on-top via `xprop`
- La session reste active (pas de vrai lock)

**Réponse (200) :**
```
Fakelock sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:8000/api/fakelock?id=zakburak"
```

---

### `GET /api/nyancat`

Affiche l'animation Nyan Cat sur l'écran du client.

**Auth requise :** Oui (Bearer token)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous) |

**Réponse (200) :**
```
Nyancat sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/nyancat?id=zakburak"
```

---

### `GET /api/fly`

Affiche une mouche/insecte qui se déplace aléatoirement sur l'écran du client.

**Auth requise :** Oui (Bearer token)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous) |

**Réponse (200) :**
```
Fly sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/fly?id=zakburak"
```

---

### `GET /api/invert`

Inverse les couleurs de l'écran du client (via gamma ramp X11).

**Auth requise :** Oui (Bearer token)

**Paramètres :**
| Param | Type | Description |
|-------|------|-------------|
| `id` | string | ID du client cible (ou `*` pour tous) |

**Réponse (200) :**
```
Invert sent to 1 client(s)
```

**Exemple :**
```bash
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8000/api/invert?id=zakburak"
```

---

## 🌟 Wildcard (Admin)

L'admin peut utiliser `*` comme `id` pour envoyer une commande à **tous les clients connectés**.

**Endpoints supportés :** `send`, `upload`, `update`, `showdesktop`, `reverse`, `key`, `marquee`, `particles`, `clones`, `drunk`, `faketerminal`, `confetti`, `spotlight`, `textscreen`, `wavescreen`, `dvdbounce`, `fireworks`, `lock`, `blackout`, `fakelock`, `nyancat`, `fly`, `invert`

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
{"command": "cover", "url": "https://example.com/cover.png"}
{"command": "clones"}
{"command": "drunk"}
{"command": "faketerminal"}
{"command": "confetti", "url": "https://example.com/image.png"}
{"command": "spotlight"}
{"command": "textscreen", "text": "HELLO"}
{"command": "wavescreen"}
{"command": "dvdbounce", "url": "https://example.com/dvd.png"}
{"command": "fireworks"}
{"command": "lock"}
{"command": "blackout"}
{"command": "fakelock"}
{"command": "nyancat"}
{"command": "fly"}
{"command": "invert"}
{"command": "screen-off", "duration": 5}
{"command": "screen_off", "duration": 5}
{"command": "key", "combo": "ctrl+alt+t"}
{"command": "start_logs"}
{"command": "stop_logs"}
{"command": "shutdown"}
{"command": "reinstall"}
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
  "version": "1.0.145"
}
```

**Heartbeat (toutes les 10s) :**
```json
{
  "type": "heartbeat",
  "locked": false
}
```

> Le champ `locked` indique si l'écran est verrouillé côté client (détection via greeter process, loginctl, gnome-screensaver).
> Le serveur utilise aussi le timeout du heartbeat (>15s sans heartbeat = verrouillé) comme détection secondaire.

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
```

**Options :**
| Option | Description |
|--------|-------------|
| `-t, --token` | Active les tokens utilisateurs (uniques par client) |
| `-a, --admin-token` | Active le token admin + login |
| `-h, --help` | Affiche l'aide |
| `PORT` | Port d'écoute (défaut: 8000) |
