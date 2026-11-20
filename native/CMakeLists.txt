cmake_minimum_required(VERSION 3.22.1)
project(openra_android_native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Strict warning flags for our Android shim/JNI code
set(STRICT_FLAGS -Wall -Wextra -Werror=return-type)

set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ---------------------------------------------------------------------------
# 1. libSDL2 (matches OpenRA-SDL2-CS P/Invoke ABI: libSDL2.so)
# ---------------------------------------------------------------------------
if(EXISTS "${THIRD_PARTY_DIR}/SDL2/CMakeLists.txt")
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(HIDAPI OFF CACHE BOOL "" FORCE)
    add_subdirectory("${THIRD_PARTY_DIR}/SDL2" "${CMAKE_BINARY_DIR}/sdl2" EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------
# 2. Google Oboe + OpenAL-Soft (matches OpenRA-OpenAL-CS: libopenal.so)
#    Note: openal-soft adds OBOE_SOURCE internally via add_subdirectory().
# ---------------------------------------------------------------------------
if(EXISTS "${THIRD_PARTY_DIR}/openal-soft/CMakeLists.txt")
    set(LIBTYPE "SHARED" CACHE STRING "" FORCE)
    set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
    set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_OBOE ON CACHE BOOL "" FORCE)
    set(OBOE_SOURCE "${THIRD_PARTY_DIR}/oboe" CACHE PATH "" FORCE)
    add_subdirectory("${THIRD_PARTY_DIR}/openal-soft" "${CMAKE_BINARY_DIR}/openal" EXCLUDE_FROM_ALL)
    if(TARGET OpenAL)
        set_target_properties(OpenAL PROPERTIES OUTPUT_NAME "openal")
    endif()
endif()

# ---------------------------------------------------------------------------
# 3. FreeType 2 (matches OpenRA.Platforms.Default DllImport("freetype6"))
# ---------------------------------------------------------------------------
if(EXISTS "${THIRD_PARTY_DIR}/freetype/CMakeLists.txt")
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    add_subdirectory("${THIRD_PARTY_DIR}/freetype" "${CMAKE_BINARY_DIR}/freetype" EXCLUDE_FROM_ALL)
    set_target_properties(freetype PROPERTIES OUTPUT_NAME "freetype6")
endif()

# ---------------------------------------------------------------------------
# 4. Lua 5.1 (matches OpenRA-Eluant DllImport("lua51"))
# ---------------------------------------------------------------------------
if(EXISTS "${THIRD_PARTY_DIR}/lua-5.1.5/src")
    file(GLOB LUA_SOURCES "${THIRD_PARTY_DIR}/lua-5.1.5/src/*.c")
    list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac)\\.c$")
    add_library(lua51 SHARED ${LUA_SOURCES})
    target_include_directories(lua51 PUBLIC "${THIRD_PARTY_DIR}/lua-5.1.5/src")
    target_compile_definitions(lua51 PRIVATE LUA_USE_POSIX)
    target_link_libraries(lua51 PRIVATE m dl)
endif()

# ---------------------------------------------------------------------------
# 5. libchdr (static library for .chd streaming decompression in launcher)
# ---------------------------------------------------------------------------
if(EXISTS "${THIRD_PARTY_DIR}/libchdr/CMakeLists.txt")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(WITH_SYSTEM_ZLIB ON CACHE BOOL "" FORCE)
    add_subdirectory("${THIRD_PARTY_DIR}/libchdr" "${CMAKE_BINARY_DIR}/libchdr" EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------
# 6. liblauncher_jni.so (SAF FD streaming extractor + SHA-256 + ImGui config)
# ---------------------------------------------------------------------------
add_library(launcher_jni SHARED
    launcher_jni/launcher_jni.cpp
)

target_include_directories(launcher_jni PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/launcher_jni"
)

target_compile_options(launcher_jni PRIVATE ${STRICT_FLAGS})

target_link_libraries(launcher_jni PRIVATE
    android
    log
    z
)

if(TARGET chdr-static)
    target_compile_definitions(launcher_jni PRIVATE HAS_LIBCHDR=1)
    target_include_directories(launcher_jni PRIVATE "${THIRD_PARTY_DIR}/libchdr/include")
    target_link_libraries(launcher_jni PRIVATE chdr-static)
elseif(TARGET chdr)
    target_compile_definitions(launcher_jni PRIVATE HAS_LIBCHDR=1)
    target_include_directories(launcher_jni PRIVATE "${THIRD_PARTY_DIR}/libchdr/include")
    target_link_libraries(launcher_jni PRIVATE chdr)
endif()
