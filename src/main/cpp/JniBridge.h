#pragma once

#include <jni.h>

// JNI 桥接：封装 JavaVM 和 Activity 全局引用
// 所有跨线程 JNI 调用都通过此类访问 Java 层
class JniBridge {
public:
    static void init(JavaVM* jvm, jobject activity) {
        s_jvm = jvm;
        s_activity = activity;
    }

    static void cleanup(JNIEnv* env = nullptr) {
        if (env && s_activity) {
            env->DeleteGlobalRef(s_activity);
        }
        s_activity = nullptr;
        s_jvm = nullptr;
    }

    static JavaVM* getJvm() { return s_jvm; }
    static jobject getActivity() { return s_activity; }

    static void detachCurrentThread() {
        if (s_jvm) s_jvm->DetachCurrentThread();
    }

private:
    static inline JavaVM* s_jvm = nullptr;
    static inline jobject s_activity = nullptr;
};
