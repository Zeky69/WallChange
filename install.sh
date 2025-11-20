#!/bin/bash

# Définition des variables
INSTALL_DIR="$HOME/.local/bin"
AUTOSTART_DIR="$HOME/.config/autostart"
PROJECT_DIR=$(pwd)

echo "=== Installation de Wallchange ==="

# 1. Compilation
echo "[1/4] Compilation du projet..."
if make; then
    echo "Compilation réussie."
else
    echo "Erreur lors de la compilation."
    exit 1
fi

# 2. Création des dossiers nécessaires
echo "[2/4] Création des dossiers d'installation..."
mkdir -p "$INSTALL_DIR"
mkdir -p "$AUTOSTART_DIR"

# 3. Installation des binaires
echo "[3/4] Installation des exécutables dans $INSTALL_DIR..."
cp wallchange "$INSTALL_DIR/"
# On n'installe pas forcément le serveur en autostart client, mais on le copie au cas où
cp server "$INSTALL_DIR/"

# Vérification du PATH
if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
    echo "ATTENTION: $HOME/.local/bin n'est pas dans votre PATH."
    echo "Ajoutez la ligne suivante à votre .bashrc ou .zshrc :"
    echo "export PATH=\"\$HOME/.local/bin:\$PATH\""
fi

# 4. Configuration du lancement automatique (Client uniquement)
echo "[4/4] Configuration du lancement automatique..."
cat > "$AUTOSTART_DIR/wallchange.desktop" <<EOF
[Desktop Entry]
Type=Application
Exec=$INSTALL_DIR/wallchange
Hidden=false
NoDisplay=false
X-GNOME-Autostart-enabled=true
Name[fr_FR]=Wallchange Client
Name=Wallchange Client
Comment[fr_FR]=Change le fond d'écran via WebSocket
Comment=Change wallpaper via WebSocket
EOF

echo "=== Installation terminée ! ==="
echo "Le client Wallchange se lancera automatiquement à votre prochaine connexion."
echo "Pour le lancer maintenant, tapez : $INSTALL_DIR/wallchange &"
