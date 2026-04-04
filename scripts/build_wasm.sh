#!/usr/bin/env bash
# ==============================================================================
# QuanChan — WASM Build Script
# Compiles liboqs Dilithium5 verification to WebAssembly using Emscripten
#
# Usage: bash scripts/build_wasm.sh
# Requirements: Docker
# Output: frontend/public/wasm/dilithium_wasm.{js,wasm}
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

LIBOQS_VERSION="0.10.1"
EMSDK_IMAGE="emscripten/emsdk:3.1.51"

OUTPUT_DIR="$PROJECT_ROOT/frontend/public/wasm"
mkdir -p "$OUTPUT_DIR"

echo "============================================"
echo "QuanChan WASM Build — Dilithium5 Verifier"
echo "============================================"
echo "liboqs version : $LIBOQS_VERSION"
echo "Emscripten image: $EMSDK_IMAGE"
echo "Output dir      : $OUTPUT_DIR"
echo ""

# --- Create the C wrapper that exposes verify to JavaScript ---
WRAPPER_C=$(cat <<'WRAPPER_EOF'
#include <oqs/oqs.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten/emscripten.h>

// ─── Exported: Verify a Dilithium5 signature ─────────────────────────────────
// Returns: 0 = valid, -1 = invalid signature, -2 = OQS init error
EMSCRIPTEN_KEEPALIVE
int dilithium5_verify(
    const uint8_t *message, size_t message_len,
    const uint8_t *signature, size_t signature_len,
    const uint8_t *public_key, size_t public_key_len
) {
    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_5);
    if (sig == NULL) {
        return -2;
    }

    // Validate public key length
    if (public_key_len != sig->length_public_key) {
        OQS_SIG_free(sig);
        return -2;
    }

    OQS_STATUS status = OQS_SIG_verify(
        sig,
        message, message_len,
        signature, signature_len,
        public_key
    );

    OQS_SIG_free(sig);
    return (status == OQS_SUCCESS) ? 0 : -1;
}

// ─── Exported: Get expected sizes for JS buffer allocation ───────────────────
EMSCRIPTEN_KEEPALIVE
size_t dilithium5_public_key_len(void) {
    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_5);
    if (!sig) return 0;
    size_t len = sig->length_public_key;
    OQS_SIG_free(sig);
    return len;
}

EMSCRIPTEN_KEEPALIVE
size_t dilithium5_signature_len(void) {
    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_5);
    if (!sig) return 0;
    size_t len = sig->length_signature;
    OQS_SIG_free(sig);
    return len;
}
WRAPPER_EOF
)

echo "[1/3] Building liboqs for WASM inside Docker..."
echo ""

docker run --rm \
    -v "$OUTPUT_DIR:/output" \
    "$EMSDK_IMAGE" \
    /bin/bash -c "
        set -euo pipefail

        echo '>>> Cloning liboqs ${LIBOQS_VERSION}...'
        git clone --branch ${LIBOQS_VERSION} --single-branch --depth 1 \
            https://github.com/open-quantum-safe/liboqs.git /tmp/liboqs

        echo '>>> Configuring liboqs for Emscripten...'
        cd /tmp/liboqs
        mkdir build && cd build
        emcmake cmake .. \
            -DCMAKE_INSTALL_PREFIX=/tmp/liboqs-install \
            -DBUILD_SHARED_LIBS=OFF \
            -DOQS_BUILD_ONLY_LIB=ON \
            -DOQS_USE_OPENSSL=OFF \
            -DOQS_DIST_BUILD=OFF \
            -DOQS_PERMIT_UNSUPPORTED_ARCHITECTURE=ON \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_FLAGS='-pthread' \
            -DOQS_OPT_TARGET=generic

        echo '>>> Compiling liboqs (this may take a few minutes)...'
        emmake make -j\$(nproc)
        emmake make install

        echo '>>> Writing C wrapper...'
        cat > /tmp/dilithium_wrapper.c << 'CWEOF'
$(echo "$WRAPPER_C")
CWEOF

        echo '>>> Compiling WASM module...'
        emcc /tmp/dilithium_wrapper.c \
            -I/tmp/liboqs-install/include \
            -L/tmp/liboqs-install/lib \
            -loqs \
            -O3 \
            -s WASM=1 \
            -s MODULARIZE=1 \
            -s EXPORT_NAME='DilithiumModule' \
            -s EXPORTED_FUNCTIONS='[\"_dilithium5_verify\",\"_dilithium5_public_key_len\",\"_dilithium5_signature_len\",\"_malloc\",\"_free\"]' \
            -s EXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\",\"HEAPU8\"]' \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s INITIAL_MEMORY=16777216 \
            -s STACK_SIZE=1048576 \
            -s NO_EXIT_RUNTIME=1 \
            -s FILESYSTEM=0 \
            -o /output/dilithium_wasm.js

        echo '>>> WASM build complete!'
        ls -la /output/dilithium_wasm.*
    "

echo ""
echo "[2/3] Verifying output files..."

if [ -f "$OUTPUT_DIR/dilithium_wasm.js" ] && [ -f "$OUTPUT_DIR/dilithium_wasm.wasm" ]; then
    JS_SIZE=$(stat -f%z "$OUTPUT_DIR/dilithium_wasm.js" 2>/dev/null || stat -c%s "$OUTPUT_DIR/dilithium_wasm.js")
    WASM_SIZE=$(stat -f%z "$OUTPUT_DIR/dilithium_wasm.wasm" 2>/dev/null || stat -c%s "$OUTPUT_DIR/dilithium_wasm.wasm")
    echo "✅ dilithium_wasm.js  : ${JS_SIZE} bytes"
    echo "✅ dilithium_wasm.wasm: ${WASM_SIZE} bytes"
else
    echo "❌ Build failed — output files not found!"
    exit 1
fi

echo ""
echo "[3/3] Done!"
echo ""
echo "Files ready at: $OUTPUT_DIR/"
echo "Next: import DilithiumModule from '/wasm/dilithium_wasm.js' in your React app"
