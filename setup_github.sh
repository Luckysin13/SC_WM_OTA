#!/bin/bash
# OTA Repository Push Script
# This script securely pushes the OTA repository to GitHub

# Required: GitHub Personal Access Token
# Create at: https://github.com/settings/tokens
# Scopes needed: repo (full control of private repositories)

echo "======================================"
echo "SC_WM OTA Repository - GitHub Setup"
echo "======================================"
echo ""

# Check if token is provided
if [ -z "$1" ]; then
    echo "ERROR: GitHub Personal Access Token required"
    echo ""
    echo "Usage: ./setup_github.sh <YOUR_GITHUB_PAT>"
    echo ""
    echo "To create a Personal Access Token:"
    echo "  1. Go to https://github.com/settings/tokens"
    echo "  2. Click 'Generate new token (classic)'"
    echo "  3. Name it 'SC_WM_OTA_Deploy'"
    echo "  4. Check scope: 'repo' (full control)"
    echo "  5. Copy the token and use as argument"
    echo ""
    exit 1
fi

GITHUB_TOKEN="$1"
GITHUB_USER="Luckysin13"
REPO_NAME="SC_WM_OTA"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Setting up remote with Personal Access Token..."
echo ""

# Configure git to use token-based authentication
cd "$REPO_DIR"

# Set the remote URL with token authentication
git remote set-url origin "https://${GITHUB_USER}:${GITHUB_TOKEN}@github.com/${GITHUB_USER}/${REPO_NAME}.git"

# Verify remote is set correctly
echo "Remote URL configured:"
git remote -v
echo ""

# Push to GitHub
echo "Pushing to GitHub..."
git push -u origin main

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Successfully pushed to GitHub!"
    echo ""
    echo "Repository is now available at:"
    echo "  https://github.com/${GITHUB_USER}/${REPO_NAME}"
    echo ""
    echo "Next: Set repository to PUBLIC in GitHub Settings"
    echo "  https://github.com/${GITHUB_USER}/${REPO_NAME}/settings"
    echo ""
else
    echo ""
    echo "❌ Push failed. Check errors above."
    exit 1
fi

# Clean up - remove token from git config
echo "Cleaning up credentials..."
git remote set-url origin "https://github.com/${GITHUB_USER}/${REPO_NAME}.git"

echo "✅ Setup complete!"
