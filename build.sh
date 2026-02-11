#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_NAME="fluorine-builder"
CONTAINER_ENGINE="${CONTAINER_ENGINE:-podman}"

# ── Build the container image if it doesn't exist ──
if ! ${CONTAINER_ENGINE} image exists "${IMAGE_NAME}" 2>/dev/null; then
    echo "Building ${IMAGE_NAME} image (one-time)..."
    ${CONTAINER_ENGINE} build -t "${IMAGE_NAME}" -f "${SCRIPT_DIR}/docker/Dockerfile" "${SCRIPT_DIR}/docker"
fi

# ── Ensure build dir exists (Podman needs it before mounting) ──
mkdir -p "${SCRIPT_DIR}/build-container"

# ── Run the build inside the container ──
echo "Building ModOrganizer inside container..."
${CONTAINER_ENGINE} run --rm \
    -v "${SCRIPT_DIR}:/src:Z" \
    -v "${SCRIPT_DIR}/build-container:/src/build:Z" \
    -w /src \
    "${IMAGE_NAME}" \
    bash docker/build-inner.sh

echo ""
echo "Portable ZIP built: ${SCRIPT_DIR}/build-container/ModOrganizer-linux-x86_64.zip"
