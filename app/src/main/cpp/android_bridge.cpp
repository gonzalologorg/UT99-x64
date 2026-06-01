#include <jni.h>
#include <android/log.h>
#include "SDL.h"
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>

#define LOG_TAG "UT99Bridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static SDL_Scancode android_key_to_sdl_scancode(int keyCode) {
    if (keyCode >= 29 && keyCode <= 54) {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (keyCode - 29));
    }
    if (keyCode >= 8 && keyCode <= 16) {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (keyCode - 8));
    }
    switch (keyCode) {
        case 7:   return SDL_SCANCODE_0; // KEYCODE_0
        case 4:   // KEYCODE_BACK
        case 82:  // KEYCODE_MENU
        case 111: // KEYCODE_ESCAPE
        case 108: // KEYCODE_BUTTON_START
        case 109: // KEYCODE_BUTTON_SELECT
        case 110: // KEYCODE_BUTTON_MODE
            return SDL_SCANCODE_ESCAPE;
        case 19:  return SDL_SCANCODE_UP;
        case 20:  return SDL_SCANCODE_DOWN;
        case 21:  return SDL_SCANCODE_LEFT;
        case 22:  return SDL_SCANCODE_RIGHT;
        case 23:  // KEYCODE_DPAD_CENTER
        case 66:  // KEYCODE_ENTER
        case 96:  // KEYCODE_BUTTON_A
            return SDL_SCANCODE_RETURN;
        case 97:  return SDL_SCANCODE_ESCAPE; // KEYCODE_BUTTON_B
        case 62:  return SDL_SCANCODE_SPACE;
        case 61:  return SDL_SCANCODE_TAB;
        case 67:  return SDL_SCANCODE_BACKSPACE;
        case 112: return SDL_SCANCODE_DELETE;
        case 92:  return SDL_SCANCODE_PAGEUP;
        case 93:  return SDL_SCANCODE_PAGEDOWN;
        case 122: return SDL_SCANCODE_HOME;
        case 123: return SDL_SCANCODE_END;
        case 69:  return SDL_SCANCODE_MINUS;
        case 70:  return SDL_SCANCODE_EQUALS;
        case 71:  return SDL_SCANCODE_LEFTBRACKET;
        case 72:  return SDL_SCANCODE_RIGHTBRACKET;
        case 73:  return SDL_SCANCODE_BACKSLASH;
        case 74:  return SDL_SCANCODE_SEMICOLON;
        case 75:  return SDL_SCANCODE_APOSTROPHE;
        case 55:  return SDL_SCANCODE_COMMA;
        case 56:  return SDL_SCANCODE_PERIOD;
        case 76:  return SDL_SCANCODE_SLASH;
        case 59:  return SDL_SCANCODE_LSHIFT;
        case 60:  return SDL_SCANCODE_RSHIFT;
        case 113: return SDL_SCANCODE_LCTRL;
        case 114: return SDL_SCANCODE_RCTRL;
        case 57:  return SDL_SCANCODE_LALT;
        case 58:  return SDL_SCANCODE_RALT;
        case 99:  return SDL_SCANCODE_N;      // KEYCODE_BUTTON_X
        case 100: return SDL_SCANCODE_Y;      // KEYCODE_BUTTON_Y
        default:  return SDL_SCANCODE_UNKNOWN;
    }
}

static int android_key_to_sdl_controller_button(int keyCode) {
    switch (keyCode) {
        case 96:  return SDL_CONTROLLER_BUTTON_A;
        case 97:  return SDL_CONTROLLER_BUTTON_B;
        case 99:  return SDL_CONTROLLER_BUTTON_X;
        case 100: return SDL_CONTROLLER_BUTTON_Y;
        case 109: return SDL_CONTROLLER_BUTTON_BACK;
        case 110: return SDL_CONTROLLER_BUTTON_GUIDE;
        case 108: return SDL_CONTROLLER_BUTTON_START;
        case 102: return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        case 103: return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
        case 19:  return SDL_CONTROLLER_BUTTON_DPAD_UP;
        case 20:  return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        case 21:  return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
        case 22:  return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        default:  return SDL_CONTROLLER_BUTTON_INVALID;
    }
}

static void push_android_button_event(int keyCode, bool down) {
    SDL_Scancode scancode = android_key_to_sdl_scancode(keyCode);
    if (scancode != SDL_SCANCODE_UNKNOWN) {
        SDL_Event event;
        SDL_memset(&event, 0, sizeof(event));
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        SDL_Window* focus = SDL_GetKeyboardFocus();
        event.key.windowID = focus ? SDL_GetWindowID(focus) : 0;
        event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
        event.key.repeat = 0;
        event.key.keysym.scancode = scancode;
        event.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
        event.key.keysym.mod = KMOD_NONE;
        SDL_PushEvent(&event);
    }

    int button = android_key_to_sdl_controller_button(keyCode);
    if (button != SDL_CONTROLLER_BUTTON_INVALID) {
        SDL_Event event;
        SDL_memset(&event, 0, sizeof(event));
        event.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
        event.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
        event.cbutton.button = static_cast<Uint8>(button);
        SDL_PushEvent(&event);
    }
}

static bool mkdir_p(const char* path) {
    if (!path || !*path) {
        return false;
    }

    char tmp[1024];
    ::snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = ::strlen(tmp);
    if (len == 0 || len >= sizeof(tmp)) {
        return false;
    }

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (::mkdir(tmp, 0775) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }

    return (::mkdir(tmp, 0775) == 0 || errno == EEXIST);
}

static bool make_child_path(char* out, size_t outSize, const char* root, const char* child) {
    if (!out || outSize == 0 || !root || !child) {
        return false;
    }

    int written = ::snprintf(out, outSize, "%s/%s", root, child);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

static jboolean prepare_process_common(
        JNIEnv* env,
        jstring dataRootString,
        jstring homeDirString) {
    if (!env || !dataRootString || !homeDirString) {
        LOGE("nativePrepareProcess called with null JNI arguments");
        return JNI_FALSE;
    }

    const char* dataRoot = env->GetStringUTFChars(dataRootString, nullptr);
    const char* homeDir = env->GetStringUTFChars(homeDirString, nullptr);

    if (!dataRoot || !homeDir) {
        LOGE("GetStringUTFChars failed: dataRoot=%p homeDir=%p", dataRoot, homeDir);
        if (dataRoot) {
            env->ReleaseStringUTFChars(dataRootString, dataRoot);
        }
        if (homeDir) {
            env->ReleaseStringUTFChars(homeDirString, homeDir);
        }
        return JNI_FALSE;
    }

    bool ok = true;

    char systemDir[1024];
    char cacheDir[1024];
    char saveDir[1024];
    char logsDir[1024];
    if (!make_child_path(systemDir, sizeof(systemDir), dataRoot, "System") ||
        !make_child_path(cacheDir, sizeof(cacheDir), dataRoot, "Cache") ||
        !make_child_path(saveDir, sizeof(saveDir), dataRoot, "Save") ||
        !make_child_path(logsDir, sizeof(logsDir), dataRoot, "Logs")) {
        LOGE("Failed to build UT99 child paths from data root: %s", dataRoot ? dataRoot : "<null>");
        ok = false;
    }

    if (!mkdir_p(dataRoot)) {
        LOGE("Failed to create data root: %s", dataRoot);
        ok = false;
    }
    if (!mkdir_p(homeDir)) {
        LOGE("Failed to create home dir: %s", homeDir);
        ok = false;
    }
    if (ok && !mkdir_p(systemDir)) {
        LOGE("Failed to create System dir: %s", systemDir);
        ok = false;
    }
    if (ok && !mkdir_p(cacheDir)) {
        LOGE("Failed to create Cache dir: %s", cacheDir);
        ok = false;
    }
    if (ok && !mkdir_p(saveDir)) {
        LOGE("Failed to create Save dir: %s", saveDir);
        ok = false;
    }
    if (ok && !mkdir_p(logsDir)) {
        LOGE("Failed to create Logs dir: %s", logsDir);
        ok = false;
    }

    if (::setenv("HOME", homeDir, 1) != 0) {
        LOGE("setenv(HOME) failed: %s", ::strerror(errno));
        ok = false;
    }

    if (::setenv("UT99_ANDROID_DATA", dataRoot, 1) != 0) {
        LOGE("setenv(UT99_ANDROID_DATA) failed: %s", ::strerror(errno));
        ok = false;
    }

    // appBaseDir() in the Unix/SDL platform layer is used very early by logging.
    // SDL_GetBasePath() can return null on Android/SDL2 in this embedded launch path,
    // so provide a stable System directory before SDL_main enters the Unreal launcher.
    if (::setenv("UT99_ANDROID_BASEDIR", systemDir, 1) != 0) {
        LOGE("setenv(UT99_ANDROID_BASEDIR) failed: %s", ::strerror(errno));
        ok = false;
    }

    // UT/UE1 normally runs from the System directory. This keeps relative paths such as
    // ../Maps, ../Textures and local User.ini/UnrealTournament.ini behaviour sane.
    if (::chdir(systemDir) != 0) {
        LOGE("chdir(%s) failed: %s", systemDir, ::strerror(errno));
        ok = false;
    } else {
        LOGI("Working directory set to %s", systemDir);
    }

    env->ReleaseStringUTFChars(dataRootString, dataRoot);
    env->ReleaseStringUTFChars(homeDirString, homeDir);

    return ok ? JNI_TRUE : JNI_FALSE;
}

// v14 moved the SDL entry point from MainActivity to GameActivity. Keep both JNI
// names exported so older Java files and the current GameActivity both work.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_ast_ut99_GameActivity_nativePrepareProcess(
        JNIEnv* env,
        jclass,
        jstring dataRootString,
        jstring homeDirString) {
    return prepare_process_common(env, dataRootString, homeDirString);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ast_ut99_MainActivity_nativePrepareProcess(
        JNIEnv* env,
        jclass,
        jstring dataRootString,
        jstring homeDirString) {
    return prepare_process_common(env, dataRootString, homeDirString);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ast_ut99_GameActivity_nativeAndroidButtonV47(
        JNIEnv*,
        jclass,
        jint keyCode,
        jboolean down) {
    static int logCount = 0;
    if (logCount < 16 || (logCount % 120) == 0) {
        LOGI("UT99_ANDROID_V240_NATIVE_BUTTON key=%d down=%d count=%d",
             static_cast<int>(keyCode),
             down == JNI_TRUE ? 1 : 0,
             logCount);
    }
    push_android_button_event(static_cast<int>(keyCode), down == JNI_TRUE);
    if (logCount < 16 || (logCount % 120) == 0) {
        LOGI("UT99_ANDROID_V243_INPUT_PUSH key=%d down=%d scancode=%d button=%d count=%d",
             static_cast<int>(keyCode),
             down == JNI_TRUE ? 1 : 0,
             static_cast<int>(android_key_to_sdl_scancode(static_cast<int>(keyCode))),
             static_cast<int>(android_key_to_sdl_controller_button(static_cast<int>(keyCode))),
             logCount);
    }
    ++logCount;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ast_ut99_GameActivity_nativeAndroidIsMenuV92(
        JNIEnv*,
        jclass) {
    // The standalone bridge library cannot safely inspect Unreal's UWindow state.
    // Returning false keeps the Java touch overlay from throwing UnsatisfiedLinkError;
    // native input/menu handling remains inside libUnrealTournament.
    return JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ast_ut99_GameActivity_nativeAndroidAxisV47(
        JNIEnv*,
        jclass,
        jint axis,
        jfloat value) {
    static int logCount = 0;
    if (logCount < 16 || (logCount % 240) == 0) {
        LOGI("UT99_ANDROID_V240_NATIVE_AXIS axis=%d value=%f count=%d",
             static_cast<int>(axis),
             static_cast<float>(value),
             logCount);
    }
    ++logCount;
}
