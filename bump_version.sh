#!/bin/bash

# Script pour incrémenter manuellement la version
# Usage: ./bump_version.sh [major|minor|patch]

TYPE=${1:-patch}
CLIENT_FILE="src/client/main.c"
SERVER_FILE="src/server/main.c"

# Extraire version actuelle
CURRENT=$(grep -oP '#define VERSION "\K[^"]+' "$CLIENT_FILE")
IFS='.' read -r -a parts <<< "$CURRENT"
major="${parts[0]}"
minor="${parts[1]}"
patch="${parts[2]}"

# Incrémenter selon le type
case $TYPE in
  major)
    major=$((major + 1))
    minor=0
    patch=0
    ;;
  minor)
    minor=$((minor + 1))
    patch=0
    ;;
  patch)
    patch=$((patch + 1))
    ;;
  *)
    echo "Usage: $0 [major|minor|patch]"
    exit 1
    ;;
esac

NEW_VERSION="$major.$minor.$patch"

echo "📦 Mise à jour: $CURRENT → $NEW_VERSION ($TYPE)"

# Mettre à jour les fichiers
sed -i "s/#define VERSION \"$CURRENT\"/#define VERSION \"$NEW_VERSION\"/" "$CLIENT_FILE"
sed -i "s/\"$CURRENT\"/\"$NEW_VERSION\"/" "$SERVER_FILE"

echo "✓ Version mise à jour dans:"
echo "  - $CLIENT_FILE"
echo "  - $SERVER_FILE"
echo ""
echo "N'oubliez pas de commit et push:"
echo "  git add $CLIENT_FILE $SERVER_FILE"
echo "  git commit -m 'chore: bump version to $NEW_VERSION'"
echo "  git push"
