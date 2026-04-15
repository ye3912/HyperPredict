#!/bin/bash
set -e
echo "[*] Building tailored binary..."
NDK=${ANDROID_NDK:-$HOME/Android/Sdk/ndk/25.2.9519653}
if [ ! -d "$NDK" ]; then
    echo "❌ NDK not found. Set ANDROID_NDK env var."
    exit 1
fi
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
echo "[✓] Tailored binary ready: $(pwd)/hyperpredictd"