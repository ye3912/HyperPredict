# AGENTS.md — HyperPredict (beta)

## Project Overview

HyperPredict is an ML-based Android/Linux CPU scheduling prediction daemon (C++17/20). It collects system load features in real-time, uses multi-model prediction (linear regression + MLP neural network + hybrid), and dynamically adjusts CPU frequency and core binding. Deployed as a Magisk/KernelSU/APatch module.

**Current version:** v4.3.0 (beta branch)

## Architecture

**Two main components:**
- **C++ daemon** (`hyperpredictd`): Core scheduling engine, runs as root on Android
- **React WebUI** (`webui-react/`): Material Design management interface, builds to `webroot/`

**Static libraries (all linked into hyperpredictd):**
- `hp_core` — EventLoop, SystemCollector, Logger, LockFreeQueue, FramePacer
- `hp_device` — CpuFreqManager, MigrationEngineV2, CoreBinder, SoC Database, EnergyModel, ClusterPolicy
- `hp_sched` — PolicyEngine (FreqMapTable O(1) lookup)
- `hp_predict` — Predictor (linear + neural + hybrid + FTRL online learning), FeatureExtractor, FallbackManager
- `hp_cache` — LRUCache
- `hp_net` — WebServer (HTTP + WebSocket)
- `hp_kernel` — SysfsWriter

**Entry point:** `src/main.cpp` → `EventLoop::start()`

## Beta Branch vs Main

Key differences from `main`:
- **MigrationEngine V2**: `src/device/migration_engine_v2.cpp` replaces `migration_engine.cpp` (old version backed up in `backup/`)
- **EnergyModel**: New `src/device/energy_model.cpp` + `include/device/energy_model.h`
- **ClusterPolicy**: New `include/device/cluster_policy.h`
- **FTRL online learning**: Predictor now supports real-time weight updates
- **HYBRID model**: Linear + neural network cooperative prediction
- **Task classification**: COMPUTE/MEMORY/IO categories for migration decisions
- **Multi-scale EMA**: 10ms/50ms/200ms/500ms time windows

## Build Commands

### Android (primary target)
```bash
export ANDROID_NDK=$HOME/Android/Sdk/ndk/26.1.10909131
mkdir build && cd build
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29 \
    -DCMAKE_BUILD_TYPE=Release \
    -GNinja
ninja
```

### Linux (testing only)
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### WebUI
```bash
cd webui-react
npm install        # first time only
npm run dev        # dev server on :3000 (proxies /api and /ws to :8081)
npm run build      # outputs to ../webroot/
```

## Testing

**Custom test framework** (not Google Test). Tests in `tests/` use `test_framework.h` with `TEST_ASSERT` / `RUN_TEST` macros.

```bash
# Build tests (separate CMakeLists)
mkdir build_test && cd build_test
cmake .. -DCMAKE_BUILD_TYPE=Debug
make hp_tests
./hp_tests
```

**Test coverage:** LockFreeQueue, FTRL Predictor, LRUCache only. No integration tests for EventLoop, PolicyEngine, or WebServer.

## Code Style

- **Format:** `.clang-format` — Google base, 4-space indent, 120 col limit, C++20 standard
- **Naming:** `snake_case` for functions/variables, `PascalCase` for classes
- **No exceptions/RTTI:** `-fno-exceptions -fno-rtti` (Android build)
- **Header-only patterns:** Many classes use pimpl (`std::unique_ptr<Impl>`)
- **`noexcept` everywhere:** All public methods are `noexcept`

## Key Quirks

1. **Dual CMakeLists:** `CMakeLists.txt` (main) vs `CMakeLists_tests.txt` (tests). Tests must be built separately.
2. **WebUI build overwrites webroot/:** `vite.config.ts` sets `outDir: '../webroot'`, `emptyOutDir: true`. The `webroot/` directory is both source (legacy HTML) and build output (React).
3. **Device probe generates headers:** `scripts/device_probe.sh` writes `include/device/hardware.h` with SoC-specific defines. This file IS in git on beta branch.
4. **Version in three places:** `module.prop`, `update.json`, and `CMakeLists.txt` (project VERSION). Keep in sync.
5. **service.sh uses `--mod-dir`:** The daemon accepts `--mod-dir` to locate logs. Without it, defaults to `/data/adb/modules/hyperpredict/logs/`.
6. **Build artifacts committed:** `build_test/` directory and `.o` files are in the repo. Don't treat them as source.
7. **CI builds on push to main only:** `.github/workflows/magisk.yml` triggers on `push` to `main` + `workflow_dispatch`. Beta branch changes won't trigger CI.
8. **Trashed files in webui-react:** `.trashed-*` files exist — these are Android trash artifacts, not source.
9. **backup/ directory:** Contains old migration engine versions (v1_orig, v2_complete). Reference only, not built.

## Directory Ownership

| Directory | Purpose |
|-----------|---------|
| `include/` | All headers, organized by module (core/, device/, predict/, sched/, net/, cache/, kernel/) |
| `src/` | All implementations, mirrors include/ structure |
| `backup/` | Old migration engine versions (reference only) |
| `webroot/` | Legacy vanilla JS WebUI + React build output |
| `webui-react/` | React + TypeScript WebUI source |
| `scripts/` | Build, packaging, install, version scripts |
| `docs/` | Architecture docs, Magisk module spec, optimization proposals |
| `tests/` | Unit tests (custom framework) |
| `build_test/` | Committed build artifacts (not source) |
| `.github/workflows/` | CI (single magisk.yml, main branch only) |

## Common Mistakes to Avoid

- Don't edit `webroot/` directly for React changes — edit `webui-react/src/` and rebuild
- Don't assume Google Test — tests use a custom framework
- Don't forget `-fno-exceptions -fno-rtti` in Android builds
- Don't modify `CMakeLists.txt` without checking `CMakeLists_tests.txt` compatibility
- Don't use `migration_engine.cpp` — it's been replaced by `migration_engine_v2.cpp`
- Don't treat `build_test/` or `.o` files as source code
- Don't trigger CI expecting beta branch to build — CI only runs on `main`
- The daemon requires root (reads `/proc/stat`, writes to `/sys/devices/system/cpu/`)
