#!/bin/bash

# Variables
REPO_URL="https://github.com/Zeky69/WallChange.git"
INSTALL_DIR="$HOME/.local/bin"
AUTOSTART_DIR="$HOME/.config/autostart"
CLONE_DIR="$HOME/.wallchange_source"

echo "=== Installation Automatisée de Wallchange ==="

# 1. Récupération du projet
# Si on est déjà dans un dépôt git WallChange, on l'utilise, sinon on clone
if [ -d ".git" ] && grep -q "WallChange" .git/config; then
    echo "[1/5] Utilisation du dossier courant..."
    PROJECT_DIR=$(pwd)
else
    echo "[1/5] Récupération du projet depuis GitHub..."
    if [ -d "$CLONE_DIR" ]; then
        echo "Mise à jour du projet existant..."
        cd "$CLONE_DIR" && git pull
    else
        echo "Clonage du dépôt..."
        git clone "$REPO_URL" "$CLONE_DIR"
        cd "$CLONE_DIR"
    fi
    PROJECT_DIR="$CLONE_DIR"
fi

# 2. Compilation
echo "[2/5] Compilation..."
cd "$PROJECT_DIR"
if make; then
    echo "Compilation réussie."
else
    echo "Erreur lors de la compilation."
    exit 1
fi

# 3. Installation des binaires
echo "[3/5] Installation dans $INSTALL_DIR..."
mkdir -p "$INSTALL_DIR"
mkdir -p "$AUTOSTART_DIR"

cp wallchange "$INSTALL_DIR/"
cp server "$INSTALL_DIR/"

# 4. Configuration du lancement automatique
echo "[4/5] Configuration du démarrage automatique..."
cat > "$AUTOSTART_DIR/wallchange.desktop" <<EOF
[Desktop Entry]
Type=Application
Exec=$INSTALL_DIR/wallchange
Hidden=false
NoDisplay=false
X-GNOME-Autostart-enabled=true
Name=Wallchange Client
Comment=Change wallpaper via WebSocket
EOF

# 5. Création de l'alias
echo "[5/5] Configuration de l'alias..."

# Détection du fichier de config shell (zsh ou bash)
SHELL_RC="$HOME/.bashrc"
if [[ "$SHELL" == */zsh ]]; then
    SHELL_RC="$HOME/.zshrc"
fi

# Ajout de l'alias si nécessaire
if ! grep -q "alias wallchange=" "$SHELL_RC"; then
    echo "Ajout de l'alias 'wallchange' dans $SHELL_RC"
    echo "" >> "$SHELL_RC"
    echo "# Alias Wallchange" >> "$SHELL_RC"
    echo "alias wallchange='$INSTALL_DIR/wallchange'" >> "$SHELL_RC"
    echo "alias wallserver='$INSTALL_DIR/server'" >> "$SHELL_RC"
else
    echo "L'alias 'wallchange' existe déjà."
fi

echo "=== Installation terminée ! ==="
echo "1. Le client se lancera automatiquement au prochain démarrage."
echo "2. Pour utiliser la commande tout de suite, tapez :"
echo "   source $SHELL_RC"
echo "   Puis : wallchange send image.jpg user"
