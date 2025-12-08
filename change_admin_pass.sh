#!/bin/bash

# Script pour changer le mot de passe admin de WallChange

CREDENTIALS_FILE=".admin_credentials"

# Vérifier les dépendances
if ! command -v sha256sum &> /dev/null; then
    echo "Erreur: sha256sum n'est pas installé."
    exit 1
fi

echo "=== Changement du mot de passe Admin WallChange ==="
echo "Ce script va mettre à jour le fichier $CREDENTIALS_FILE"

# Demander le nouveau mot de passe
read -s -p "Entrez le nouveau mot de passe admin: " PASSWORD
echo
read -s -p "Confirmez le mot de passe: " PASSWORD_CONFIRM
echo

if [ "$PASSWORD" != "$PASSWORD_CONFIRM" ]; then
    echo "Erreur: Les mots de passe ne correspondent pas."
    exit 1
fi

if [ -z "$PASSWORD" ]; then
    echo "Erreur: Le mot de passe ne peut pas être vide."
    exit 1
fi

# Calculer le hash SHA256
# echo -n pour ne pas inclure le saut de ligne dans le hash
HASH=$(echo -n "admin:$PASSWORD" | sha256sum | awk '{print $1}')

# Écrire dans le fichier
echo "admin:$HASH" > "$CREDENTIALS_FILE"

echo "✅ Mot de passe mis à jour avec succès !"
echo "Le nouveau hash a été écrit dans $CREDENTIALS_FILE"
