#!/bin/bash
set -e

# Sciter JS SDK GitLab repository and API
API_URL="https://gitlab.com/api/v4/projects/sciter-engine%2Fsciter-js-sdk/releases"

# Get absolute path to the project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
EXTERN_SCITER_DIR="$PROJECT_DIR/extern/sciter"

echo "Fetching latest Sciter JS SDK release info..."
LATEST_TAG=$(curl -s "$API_URL" | grep -o '"tag_name":"[^"]*' | head -n 1 | cut -d'"' -f4)

if [ -z "$LATEST_TAG" ]; then
    echo "Error: Could not retrieve the latest release tag."
    exit 1
fi

echo "Latest version found: $LATEST_TAG"
ZIP_URL="https://gitlab.com/sciter-engine/sciter-js-sdk/-/archive/$LATEST_TAG/sciter-js-sdk-$LATEST_TAG.zip"
TMP_DIR=$(mktemp -d)
ZIP_FILE="$TMP_DIR/sciter.zip"

echo "Downloading $ZIP_URL..."
curl -L -o "$ZIP_FILE" "$ZIP_URL"

echo "Extracting..."
unzip -q "$ZIP_FILE" -d "$TMP_DIR"

# The extracted folder is usually named like the repo-tag
EXTRACTED_DIR="$TMP_DIR/sciter-js-sdk-$LATEST_TAG-$(echo "$LATEST_TAG" | tr -d '.')"
# However, let's just find the root directory of the extracted zip
EXTRACTED_DIR=$(find "$TMP_DIR" -mindepth 1 -maxdepth 1 -type d | head -n 1)

if [ ! -d "$EXTRACTED_DIR/include" ]; then
    echo "Error: Unexpected zip structure. Could not find 'include' dir."
    rm -rf "$TMP_DIR"
    exit 1
fi

echo "Updating $EXTERN_SCITER_DIR..."
# Ensure directories exist
mkdir -p "$EXTERN_SCITER_DIR/bin"
mkdir -p "$EXTERN_SCITER_DIR/include"

# Copy includes
echo "Copying include files..."
cp -r "$EXTRACTED_DIR/include/"* "$EXTERN_SCITER_DIR/include/"

# Copy binaries (assuming Windows x64 is the target)
echo "Copying binaries..."

# Find and copy packfolder.exe and sciter.dll
find "$EXTRACTED_DIR/bin" -name "packfolder.exe" -exec cp {} "$EXTERN_SCITER_DIR/bin/" \;
find "$EXTRACTED_DIR/bin/windows.d2d/x64" -name "sciter.dll" -exec cp {} "$EXTERN_SCITER_DIR/bin/" \;

# Clean up
rm -rf "$TMP_DIR"

echo "Sciter SDK updated to $LATEST_TAG successfully."
exit 0
