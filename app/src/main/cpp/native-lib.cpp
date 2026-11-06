#include <jni.h>
#include <android/log.h>
#include <thread>
#include <atomic>
#include <cstdint>

#define TAG "A1NativeWorker"

#define LOGD(...) \
    __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

#define LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static JavaVM* g_vm = nullptr;

struct NativeWorker {
    jobject listener = nullptr;

    jmethodID midStarted = nullptr;
    jmethodID midProgress = nullptr;
    jmethodID midFrame = nullptr;
    jmethodID midError = nullptr;
    jmethodID midFinished = nullptr;

    uint8_t* frameBuf = nullptr;
    jobject frameBuffer = nullptr;

    std::thread worker;
    std::atomic<bool> running{false};
};

static jlong nativeCreate(JNIEnv* env,jobject thiz,jobject listener){
    LOGD("nativeCreate called");
    return 0;
}

static void nativeStart(JNIEnv* env,jobject thiz,jlong handle) {
    LOGD("nativeStart called");
}

static void nativeStop(JNIEnv* env,jobject thiz,jlong handle) {
    LOGD("nativeStop called");
}

static void nativeRelease(JNIEnv* env,jobject thiz,jlong handle) {
    LOGD("nativeRelease called");
}

static const JNINativeMethod kMethods[] = {

        {
                "nativeCreate",//kotlin方法名
                "(Lcom/example/a1nativeworker/NativeWorker$Listener;)J",//JNI类型签名
                (void*) nativeCreate//c++函数地址
        },

        {
                "nativeStart",
                "(J)V",
                (void*) nativeStart
        },

        {
                "nativeStop",
                "(J)V",
                (void*) nativeStop
        },

        {
                "nativeRelease",
                "(J)V",
                (void*) nativeRelease
        }
};

extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* reserved){
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(
            reinterpret_cast<void**>(&env),
            JNI_VERSION_1_6
    ) != JNI_OK) {

        LOGE("GetEnv failed");

        return JNI_ERR;
    }//从当前线程找到env并赋值
    
    jclass cls = env->FindClass("com/example/a1nativeworker/NativeWorker");
    if (cls == nullptr) {
        LOGE("NativeWorker class not found");
        return JNI_ERR;
    }

    const jint count =
            sizeof(kMethods) / sizeof(kMethods[0]);//计算kotlin方法数

    if (env->RegisterNatives(
            cls,
            kMethods,
            count
    ) != JNI_OK) {
        LOGE("RegisterNatives failed");
        env->DeleteLocalRef(cls);
        return JNI_ERR;
    }

    env->DeleteLocalRef(cls);

    LOGD("RegisterNatives success: %d methods", count);

    return JNI_VERSION_1_6;
}
