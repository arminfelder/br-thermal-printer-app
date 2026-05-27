#!/bin/bash
# Extract entries for VERSION from CHANGELOG.md and call dch
set -euo pipefail

VERSION=$(cat VERSION)
DEB_VERSION="${VERSION}-1"

# Extract lines between first ## heading and next ##
CHANGES=$(awk '/^## v'"${VERSION}"'/{found=1; next} found && /^## /{exit} found && /^###/{next} found && /^- /{print}' CHANGELOG.md)

if [ -z "$CHANGES" ]; then
    CHANGES="Release ${VERSION}"
fi

# First entry
FIRST=1
while IFS= read -r line; do
    if [ "$FIRST" -eq 1 ]; then
        dch --newversion "${DEB_VERSION}" --distribution unstable -- "${line#- }"
        FIRST=0
    else
        dch --append -- "${line#- }"
    fi
done <<< "$CHANGES"
