# OpenRA Android ARM64 (`arm64-v8a`)

Native ARM64 port of **OpenRA-bleed** (.NET 10 + SDL2 + OpenGL ES 3.0 + OpenAL/Oboe + FreeType + Lua 5.1) with an Android SAF (Storage Access Framework) BYOF Launcher for `.iso`, `.chd`, and `.zip` game archives.

Targeted first for **ARMv8 / `arm64-v8a`** devices (e.g., Redmi Note 13 4G / Snapdragon 685 / Adreno 610).

## Project Layout
- `engine/` — OpenRA-bleed C# source tree with minimal Android/GLES3/Touch guards (`Platform.cs`, `Sdl2PlatformWindow.cs`, `Sdl2Input.cs`)
- `native/` — CMake NDK (`arm64-v8a`) build for `liblauncher_jni.so` (SAF FD streaming `.iso`/`.chd`/`.zip` extractor + SHA-256 + `SDL_main` bridge), `libSDL2.so`, `libopenal.so` (Oboe), `libfreetype6.so`, `liblua51.so`
- `OpenRA.Android/` — `.NET 10 Android` (`net10.0-android`, `android-arm64`) app + Material dark Launcher UI + `OpenRAGameActivity`
- `.github/workflows/build-apk.yml` — Cloud CI build on `ubuntu-latest` that compiles native `.so` libraries, verifies `llvm-readelf -d`, builds the ARM64 APK, and uploads it to Artifacts.

## Cloud Build (GitHub Actions)
1. Initialize git in this folder and push to a GitHub repository:
   ```bash
   git init
   git add .
   git commit -m "Initial OpenRA Android ARM64 port"
   git branch -M main
   git remote add origin https://github.com/<USER>/<REPO>.git
   git push -u origin main
   ```
2. Open the **Actions** tab on GitHub -> **Build OpenRA Android ARM64 APK** -> download `openra-arm64-v8a-apk`.
