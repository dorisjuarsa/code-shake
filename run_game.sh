#!/bin/bash
# =============================================================================
# COCA-COLA BOTTLE SHAKE MONITOR - LAUNCHER SCRIPT
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Periksa apakah virtual environment sudah siap
if [ ! -d ".venv" ]; then
    echo "[!] Virtual environment (.venv) belum ada. Membuat venv..."
    python3 -m venv .venv
    echo "[!] Menginstal dependensi dari requirements.txt..."
    .venv/bin/pip install --upgrade pip
    .venv/bin/pip install -r requirements.txt
fi

echo "🚀 Menjalankan Coca-Cola Shake Monitor (Native Python GUI)..."
.venv/bin/python3 main_gui.py
