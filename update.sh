#!/usr/bin/env bash
#
# Atualiza a cópia vendorizada do jsonfylinx (vendor/jsonfylinx) a partir do
# GitHub — branch main — e reconstrói o binário do JsonfyPoll.
#
# Se o build falhar (ex.: a ABI em jsonfylinx.h mudou e src/ffi.rs ficou
# desatualizado), o vendor é revertido e o binário anterior é preservado.
#
# Uso:  ./update.sh
set -euo pipefail

REPO_RAW="https://raw.githubusercontent.com/clerio95/jsonfylinx/main"
FILES=(jsonfylinx.c jsonfylinx.h)

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENDOR="$ROOT/vendor/jsonfylinx"

TMP="$(mktemp -d)"
BACKUP="$(mktemp -d)"
trap 'rm -rf "$TMP" "$BACKUP"' EXIT

echo "==> Baixando jsonfylinx de $REPO_RAW (branch main)"
for f in "${FILES[@]}"; do
    if ! curl -fsSL "$REPO_RAW/$f" -o "$TMP/$f"; then
        echo "ERRO: falha ao baixar $f" >&2
        exit 1
    fi
done

# Há alguma mudança em relação ao que já está vendorizado?
changed=0
mkdir -p "$VENDOR"
for f in "${FILES[@]}"; do
    if ! cmp -s "$TMP/$f" "$VENDOR/$f" 2>/dev/null; then
        changed=1
    fi
done

if [ "$changed" -eq 0 ]; then
    echo "==> Já está na versão mais recente (nenhuma mudança)."
    exit 0
fi

# Backup do vendor atual antes de substituir.
cp -a "$VENDOR/." "$BACKUP/" 2>/dev/null || true

echo "==> Atualizando vendor/jsonfylinx"
for f in "${FILES[@]}"; do
    cp "$TMP/$f" "$VENDOR/$f"
done

echo "==> Recompilando (cargo build --release)"
if cargo build --release --manifest-path "$ROOT/Cargo.toml"; then
    echo "==> Pronto. Binário atualizado: target/release/jsonfypoll"
else
    echo "ERRO: build falhou — revertendo vendor/jsonfylinx." >&2
    echo "      (provável mudança de ABI: ajuste src/ffi.rs e rode de novo.)" >&2
    rm -rf "${VENDOR:?}/"
    mkdir -p "$VENDOR"
    cp -a "$BACKUP/." "$VENDOR/" 2>/dev/null || true
    echo "Vendor revertido; binário anterior preservado." >&2
    exit 1
fi
