#!/bin/bash
# =========================================================
# HEN Cheats Collection - PPSA Only Extractor
# =========================================================
#
# PURPOSE
# -------
# Downloads the latest HEN Cheats Collection repository
# and extracts ONLY PPSA entries from:
#
#   - mc4
#   - json
#   - shn
#
# OUTPUT
# ------
# Creates:
#
#   HEN-Cheats-Collection/
#   └── cheats/
#       ├── mc4/
#       ├── json/
#       ├── shn/
#       ├── mc4.txt
#       ├── json.txt
#       └── shn.txt
#
# All files and text entries are filtered to PPSA only.
#
# REQUIREMENTS
# ------------
#   wget
#   unzip
#   grep
#   find
#   cp
#   wc
#
# HOW TO USE
# ----------
# 1. Save this script as:
#
#      extract_ppsa.sh
#
# 2. Make it executable:
#
#      chmod +x extract_ppsa.sh
#
# 3. Run it from the directory where you want the
#    HEN-Cheats-Collection folder created:
#
#      ./extract_ppsa.sh
#
# 4. After completion, the filtered PPSA files will be in:
#
#      ./HEN-Cheats-Collection
#
# NOTES
# -----
# - Existing HEN-Cheats-Collection output folder will be
#   deleted and recreated.
# - Temporary download files are automatically removed.
# - Script stops immediately if an error occurs.
#
# =========================================================

set -Eeuo pipefail

# =========================================================
# Configuration
# =========================================================

URL="https://github.com/TeeKay87/HEN-Cheats-Collection/archive/refs/heads/master.zip"

BASE_DIR="$(pwd)"

WORKDIR="${BASE_DIR}/HEN_PPSA_TEMP"
ZIPFILE="${WORKDIR}/master.zip"
EXTRACTDIR="${WORKDIR}/extracted"

FINALDIR="${BASE_DIR}/HEN-Cheats-Collection"

SOURCE_REPO="${EXTRACTDIR}/HEN-Cheats-Collection-master"
SOURCE_CHEATS="${SOURCE_REPO}/cheats"

DEST="${FINALDIR}/cheats"

TARGETS=(
    mc4
    json
    shn
)

# =========================================================
# Cleanup Handler
# =========================================================

cleanup() {
    rm -rf "${WORKDIR}" 2>/dev/null || true
}

trap cleanup EXIT

# =========================================================
# Error Handler
# =========================================================

error_exit() {
    echo ""
    echo "========================================="
    echo " ERROR OCCURRED"
    echo "========================================="
    echo "Line: $1"
    echo "Command: $2"
    echo ""
    exit 1
}

trap 'error_exit ${LINENO} "$BASH_COMMAND"' ERR

# =========================================================
# Dependency Check
# =========================================================

echo "========================================="
echo " Checking dependencies..."
echo "========================================="

REQUIRED_CMDS=(
    wget
    unzip
    grep
    find
    cp
    wc
)

for cmd in "${REQUIRED_CMDS[@]}"; do
    if ! command -v "${cmd}" >/dev/null 2>&1; then
        echo "ERROR: Missing dependency: ${cmd}"
        exit 1
    fi
done

echo "✅ All dependencies found"

# =========================================================
# Cleanup Old Data
# =========================================================

echo ""
echo "========================================="
echo " Cleaning old directories..."
echo "========================================="

rm -rf "${WORKDIR}"
rm -rf "${FINALDIR}"

mkdir -p "${EXTRACTDIR}"
mkdir -p "${DEST}"

# =========================================================
# Download ZIP
# =========================================================

echo ""
echo "========================================="
echo " Downloading repository..."
echo "========================================="

wget \
    --quiet \
    --show-progress \
    --progress=bar:force \
    -O "${ZIPFILE}" \
    "${URL}"

# =========================================================
# Validate ZIP
# =========================================================

if [ ! -s "${ZIPFILE}" ]; then
    echo "ERROR: ZIP download failed or file is empty."
    exit 1
fi

echo "✅ ZIP downloaded successfully"

# =========================================================
# Extract ZIP
# =========================================================

echo ""
echo "========================================="
echo " Extracting ZIP..."
echo "========================================="

unzip -qq "${ZIPFILE}" -d "${EXTRACTDIR}"

# =========================================================
# Validate Repository Structure
# =========================================================

echo ""
echo "========================================="
echo " Validating repository structure..."
echo "========================================="

for target in "${TARGETS[@]}"; do

    if [ ! -d "${SOURCE_CHEATS}/${target}" ]; then
        echo "ERROR: ${target} directory not found."
        exit 1
    fi

    if [ ! -f "${SOURCE_CHEATS}/${target}.txt" ]; then
        echo "ERROR: ${target}.txt not found."
        exit 1
    fi

done

echo "✅ Repository structure validated"

# =========================================================
# Copy PPSA Files + Filter TXT Files
# =========================================================

for target in "${TARGETS[@]}"; do

    echo ""
    echo "========================================="
    echo " Processing ${target}"
    echo "========================================="

    SRC_DIR="${SOURCE_CHEATS}/${target}"
    SRC_TXT="${SOURCE_CHEATS}/${target}.txt"

    DEST_DIR="${DEST}/${target}"

    mkdir -p "${DEST_DIR}"

    FOUND_FILES=0

    while IFS= read -r -d '' file; do

        filename="$(basename "$file")"

        cp -f "$file" "${DEST_DIR}/${filename}"

        echo "Copied: ${target}/${filename}"

        FOUND_FILES=1

    done < <(
        find "${SRC_DIR}" \
            -type f \
            -name 'PPSA*' \
            -print0
    )

    if [ "${FOUND_FILES}" -eq 0 ]; then
        echo "WARNING: No PPSA files found in ${target}"
    else
        echo "✅ PPSA files copied for ${target}"
    fi

    grep '^PPSA' \
        "${SRC_TXT}" \
        > "${DEST}/${target}.txt" || true

    echo "✅ ${target}.txt filtered"

done

# =========================================================
# Validation Tests
# =========================================================

echo ""
echo "========================================="
echo " Running validation tests..."
echo "========================================="

for target in "${TARGETS[@]}"; do

    DEST_DIR="${DEST}/${target}"

    INVALID_FILES=$(
        find "${DEST_DIR}" \
            -type f \
            ! -name 'PPSA*'
    )

    if [ -n "${INVALID_FILES}" ]; then
        echo "❌ Non-PPSA files detected in ${target}:"
        echo "${INVALID_FILES}"
        exit 1
    else
        echo "✅ ${target}: only PPSA files exist"
    fi

    INVALID_LINES=$(
        grep -v '^PPSA' "${DEST}/${target}.txt" || true
    )

    if [ -n "${INVALID_LINES}" ]; then
        echo "❌ Invalid lines found in ${target}.txt:"
        echo "${INVALID_LINES}"
        exit 1
    else
        echo "✅ ${target}.txt only contains PPSA entries"
    fi

    FILE_COUNT=$(
        find "${DEST_DIR}" -type f | wc -l
    )

    TXT_LINES=$(
        wc -l < "${DEST}/${target}.txt"
    )

    echo "✅ ${target} PPSA file count: ${FILE_COUNT}"
    echo "✅ ${target}.txt entries : ${TXT_LINES}"

done

# =========================================================
# Final Output
# =========================================================

echo ""
echo "========================================="
echo " ALL TESTS PASSED"
echo "========================================="
echo ""
echo "Output location:"
echo "${FINALDIR}"
echo ""
