#!/bin/bash

# Script pour incrémenter manuellement la version
# Usage: ./bump_version.sh [major|minor|patch]

TYPE=${1:-patch}
VERSION_FILE="VERSION"

if [ ! -f "$VERSION_FILE" ]; then
    echo "Error: $VERSION_FILE not found!"
    exit 1
fi

# Extraire version actuelle
CURRENT=$(cat "$VERSION_FILE")
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

# Mettre à jour le fichier
echo "$NEW_VERSION" > "$VERSION_FILE"

echo "✓ Version mise à jour dans:"
echo "  - $VERSION_FILE"
echo ""
echo "N'oubliez pas de commit et push:"
echo "  git add $VERSION_FILE"
echo "  git commit -m 'chore: bump version to $NEW_VERSION'"
echo "  git push"
