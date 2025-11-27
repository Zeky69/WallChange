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

Pour changer le fond d'écran de l'utilisateur `zakburak`, ouvrez votre navigateur ou utilisez `curl` :

```bash
curl "http://localhost:8000/api/send?id=zakburak&url=https://images.pexels.com/photos/1266808/pexels-photo-1266808.jpeg"
```

Remplacez `zakburak` par le nom de votre utilisateur Linux.
Remplacez l'URL par n'importe quel lien direct vers une image (JPG/PNG).

## Notes techniques

- Le serveur stocke l'ID du client dans la structure de connexion.
- L'API `/api/send` parcourt les connexions actives pour trouver celle qui correspond à l'ID demandé.

