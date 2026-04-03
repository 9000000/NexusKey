#!/bin/bash
set -e

# NexusKey release script — bumps version, commits, tags, and pushes.
#
# Version is defined ONCE in project(NexusKey VERSION ...) in CMakeLists.txt.
# CMake auto-generates src/core/Version.h from Version.h.in at configure time.
# No other files need manual version updates.

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."

CMAKE_FILE="$REPO_ROOT/CMakeLists.txt"
RELEASE_NOTES="$REPO_ROOT/RELEASE_NOTES.md"

# --- Main ---

# 1. Ask for version
read -p "Enter new version (e.g. 1.0.8): " version
if [ -z "$version" ]; then
    echo "Version cannot be empty."
    exit 1
fi

if ! [[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Invalid version format. Expected X.Y.Z"
    exit 1
fi

# 2. Confirm
branch=$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD)
echo -e "\033[33mYou are on branch: $branch\033[0m"
read -p "Bump version to $version and create tag v$version? (y/n) " confirm
if [[ ! "$confirm" =~ ^[yY]$ ]]; then
    echo "Aborted."
    exit 0
fi

# 3. Update CMakeLists.txt — single source of truth
if [ ! -f "$CMAKE_FILE" ]; then
    echo "Error: Could not find CMakeLists.txt at $CMAKE_FILE"
    exit 1
fi

echo -e "\033[36mUpdating $CMAKE_FILE...\033[0m"
perl -i -pe "s/^(project\\(NexusKey VERSION )[0-9]+\\.[0-9]+\\.[0-9]+/\${1}$version/" "$CMAKE_FILE"
echo -e "\033[32mCMakeLists.txt updated to $version.\033[0m"
echo -e "\033[90m  (Version.h will be regenerated automatically on next cmake configure)\033[0m"

# 4. Show diff and confirm
echo -e "\033[33mChanges to commit:\033[0m"
git -C "$REPO_ROOT" diff "$CMAKE_FILE"

read -p "Looks good? Commit and push? (y/n) " confirm2
if [[ ! "$confirm2" =~ ^[yY]$ ]]; then
    echo "Aborted. Changes are on disk but not committed."
    exit 0
fi

# 5. Git operations
echo -e "\033[36mPerforming Git operations...\033[0m"

git -C "$REPO_ROOT" add "$CMAKE_FILE"

# Also stage RELEASE_NOTES if it was modified
if [ -f "$RELEASE_NOTES" ] && ! git -C "$REPO_ROOT" diff --quiet "$RELEASE_NOTES"; then
    git -C "$REPO_ROOT" add "$RELEASE_NOTES"
    echo -e "\033[36mStaged RELEASE_NOTES.md too.\033[0m"
fi

git -C "$REPO_ROOT" commit -m "Bump version to v$version"
git -C "$REPO_ROOT" tag -a "v$version" -m "Release v$version"

echo -e "\033[36mPushing to origin ($branch)...\033[0m"
git -C "$REPO_ROOT" push origin "$branch"
git -C "$REPO_ROOT" push origin "v$version"

echo -e "\033[32mDone! v$version released on branch $branch.\033[0m"
