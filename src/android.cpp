/**
 * @file android.cpp
 * @brief See android.h.
 */
#include "android.h"

#include <string>
#include <thread>

#include <android/log.h>
#include <jni.h>
#include <unistd.h>

#include <SDL2/SDL_system.h>

namespace snap {

std::string android_pick_document() {
    std::string result;
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if ((env == nullptr) || (activity == nullptr)) {
        return result;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID pick = env->GetMethodID(cls, "pickDocument", "()Ljava/lang/String;");
    if (pick == nullptr) {
        // Not the port's activity: no picker.
        env->ExceptionClear();
    } else {
        jstring chosen = static_cast<jstring>(env->CallObjectMethod(activity, pick));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else if (chosen != nullptr) {
            if (const char* utf = env->GetStringUTFChars(chosen, nullptr)) {
                result = utf;
                env->ReleaseStringUTFChars(chosen, utf);
            }
            env->DeleteLocalRef(chosen);
        }
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return result;
}

int android_logcat_pipe() {
    int fds[2];
    if (pipe(fds) != 0) {
        return -1;
    }
    const int readEnd = fds[0];
    std::thread([readEnd] {
        std::string line;
        char buf[4096];
        for (;;) {
            const ssize_t n = read(readEnd, buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
            for (ssize_t i = 0; i < n; i++) {
                if (buf[i] == '\n') {
                    __android_log_write(ANDROID_LOG_INFO, "Snap64", line.c_str());
                    line.clear();
                } else {
                    line.push_back(buf[i]);
                }
            }
        }
        if (!line.empty()) {
            __android_log_write(ANDROID_LOG_INFO, "Snap64", line.c_str());
        }
        close(readEnd);
    }).detach();
    return fds[1];
}

} // namespace snap
