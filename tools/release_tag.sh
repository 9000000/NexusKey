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

# 1. Read current version from CMakeLists.txt
current_version=$(grep -oP 'project\(NexusKey VERSION \K[0-9]+\.[0-9]+\.[0-9]+' "$CMAKE_FILE")
if [ -z "$current_version" ]; then
    echo "Error: Could not read current version from $CMAKE_FILE"
    exit 1
fi

# 2. Ask for version
read -p "Current version: $current_version. Enter new version: " version
if [ -z "$version" ]; then
    echo "Version cannot be empty."
    exit 1
fi

if ! [[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Invalid version format. Expected X.Y.Z"
    exit 1
fi

# 3. Confirm
branch=$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD)
echo -e "\033[33mYou are on branch: $branch\033[0m"
read -p "Bump version to $version and create tag v$version? (y/n) " confirm
if [[ ! "$confirm" =~ ^[yY]$ ]]; then
    echo "Aborted."
    exit 0
fi

# 4. Check if version bump is needed
if [ "$current_version" = "$version" ]; then
    echo -e "\033[32mCMakeLists.txt already at v$version — skipping bump.\033[0m"
    need_commit=false
else
    if [ ! -f "$CMAKE_FILE" ]; then
        echo "Error: Could not find CMakeLists.txt at $CMAKE_FILE"
        exit 1
    fi

    echo -e "\033[36mUpdating $CMAKE_FILE...\033[0m"
    perl -i -pe "s/^(project\\(NexusKey VERSION )[0-9]+\\.[0-9]+\\.[0-9]+/\${1}$version/" "$CMAKE_FILE"
    echo -e "\033[32mCMakeLists.txt updated to $version.\033[0m"
    echo -e "\033[90m  (Version.h will be regenerated automatically on next cmake configure)\033[0m"
    need_commit=true
fi

# 5. Stage any dirty files (CMakeLists.txt + RELEASE_NOTES.md)
if [ "$need_commit" = true ] || ! git -C "$REPO_ROOT" diff --quiet "$CMAKE_FILE" 2>/dev/null; then
    need_commit=true
fi
if [ -f "$RELEASE_NOTES" ] && ! git -C "$REPO_ROOT" diff --quiet "$RELEASE_NOTES" 2>/dev/null; then
    need_commit=true
fi

if [ "$need_commit" = true ]; then
    echo -e "\033[33mChanges to commit:\033[0m"
    git -C "$REPO_ROOT" diff "$CMAKE_FILE" "$RELEASE_NOTES" 2>/dev/null
    read -p "Looks good? Commit and push? (y/n) " confirm2
    if [[ ! "$confirm2" =~ ^[yY]$ ]]; then
        echo "Aborted. Changes are on disk but not committed."
        exit 0
    fi

    echo -e "\033[36mCommitting...\033[0m"
    git -C "$REPO_ROOT" add "$CMAKE_FILE"
    if [ -f "$RELEASE_NOTES" ] && ! git -C "$REPO_ROOT" diff --cached --quiet "$RELEASE_NOTES" 2>/dev/null || ! git -C "$REPO_ROOT" diff --quiet "$RELEASE_NOTES" 2>/dev/null; then
        git -C "$REPO_ROOT" add "$RELEASE_NOTES"
        echo -e "\033[36mStaged RELEASE_NOTES.md too.\033[0m"
    fi
    git -C "$REPO_ROOT" commit -m "Bump version to v$version"

    echo -e "\033[36mPushing to origin ($branch)...\033[0m"
    git -C "$REPO_ROOT" push origin "$branch"
else
    echo -e "\033[32mNo file changes — skipping commit.\033[0m"
    # Still push branch in case there are unpushed commits
    if ! git -C "$REPO_ROOT" diff --quiet "origin/$branch" "$branch" 2>/dev/null; then
        echo -e "\033[36mPushing unpushed commits to origin ($branch)...\033[0m"
        git -C "$REPO_ROOT" push origin "$branch"
    fi
fi

# 6. Create tag and push
echo -e "\033[36mCreating tag v$version...\033[0m"
git -C "$REPO_ROOT" tag -a "v$version" -m "Release v$version"

echo -e "\033[36mPushing tag v$version...\033[0m"
git -C "$REPO_ROOT" push origin "v$version"

echo -e "\033[32mDone! v$version released on branch $branch.\033[0m"
