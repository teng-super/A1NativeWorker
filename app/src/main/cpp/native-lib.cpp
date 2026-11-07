#include <jni.h>
#include <android/log.h>
#include <thread>
#include <atomic>
#include <cstdint>
#include <new>
#include <chrono>

#define TAG "A1NativeWorker"

#define LOGD(...) \
    __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

#define LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static JavaVM* g_vm = nullptr;
static constexpr int kFrameSize = 1024 * 1024;

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

static void destroyWorkerResources(JNIEnv* env,NativeWorker* w) {//释放当前nativeWorker已经持有的资源
    if (w == nullptr) {
        return;
    }
    if (w->frameBuffer != nullptr) {
        env->DeleteGlobalRef(w->frameBuffer);
    }
    delete[] w->frameBuf;
    if (w->listener != nullptr) {
        env->DeleteGlobalRef(w->listener);
    }
    delete w;
}

struct ScopedJniEnv {

    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    bool attached = false;

    explicit ScopedJniEnv(JavaVM* javaVm): vm(javaVm) {
        if (vm == nullptr) {
            return;
        }
        jint result = vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6);

        if (result == JNI_EDETACHED) {//未接入线程
            if (vm->AttachCurrentThread(&env,nullptr) == JNI_OK) {//std::thread 需要通过 JNI 调用 Kotlin
                // 成功附加后env就会指向这个工作线程可以使用的 JNI 接口
                attached = true;
            }
            else {
                env = nullptr;
            }
        }
        else if (result != JNI_OK) {//JNI版本不支持
            env = nullptr;
        }
    }

    ~ScopedJniEnv() {
        if (attached && vm != nullptr) {
            vm->DetachCurrentThread();
        }
    }
};

static bool checkException(JNIEnv* env,const char* where){
    if (!env->ExceptionCheck()) {
        return false;
    }

    LOGE("JNI exception after %s", where);
    env->ExceptionDescribe();
    env->ExceptionClear();

    return true;
}

static jlong nativeCreate(JNIEnv* env,jobject thiz,jobject listener){
    NativeWorker* w = new NativeWorker();

    if (w == nullptr) {
        LOGE("Failed to allocate NativeWorker");
        return 0;
    }

    w->listener = env->NewGlobalRef(listener);

    if (w->listener == nullptr) {
        destroyWorkerResources(env, w);
        return 0;
    }

    jclass cls = env->GetObjectClass(listener);
    if (cls == nullptr) {
        destroyWorkerResources(env, w);
        return 0;
    }

    w->midStarted = env->GetMethodID(
            cls, "onStarted", "()V"
    );

    if (w->midStarted != nullptr) {
        w->midProgress = env->GetMethodID(
                cls, "onProgress", "(I)V"
        );
    }

    if (w->midProgress != nullptr) {
        w->midFrame = env->GetMethodID(
                cls, "onFrame",
                "(Ljava/nio/ByteBuffer;I)V"
        );
    }

    if (w->midFrame != nullptr) {
        w->midError = env->GetMethodID(
                cls, "onError",
                "(ILjava/lang/String;)V"
        );
    }

    if (w->midError != nullptr) {
        w->midFinished = env->GetMethodID(
                cls, "onFinished", "()V"
        );
    }

    env->DeleteLocalRef(cls);

    // 任一方法查找失败，终止创建
    if (w->midStarted == nullptr ||
            w->midProgress == nullptr ||
            w->midFrame == nullptr ||
            w->midError == nullptr ||
            w->midFinished == nullptr) {

        LOGE("GetMethodID failed");

        destroyWorkerResources(env, w);
        return 0;
    }

    //分配帧内存
    w->frameBuf = new uint8_t[kFrameSize];
    if (w->frameBuf == nullptr) {
        LOGE("Failed to allocate frame buffer");
        destroyWorkerResources(env, w);
        return 0;
    }

    jobject localBuf = env->NewDirectByteBuffer(
            w->frameBuf,
            kFrameSize
    );
    if (localBuf == nullptr) {
        LOGE("NewDirectByteBuffer failed");
        destroyWorkerResources(env, w);
        return 0;
    }

    w->frameBuffer = env->NewGlobalRef(localBuf);
    env->DeleteLocalRef(localBuf);

    if (w->frameBuffer == nullptr) {
        LOGE("NewGlobalRef for frameBuffer failed");
        destroyWorkerResources(env, w);
        return 0;
    }

    LOGD("create worker: %p", static_cast<void*>(w));
    return reinterpret_cast<jlong>(w);
}

static void nativeStart(JNIEnv* env,jobject thiz,jlong handle) {
    // 1. 从 handle 找回 C++ 对象
    auto* w = reinterpret_cast<NativeWorker*>(handle);

    if (w == nullptr) {
        LOGE("nativeStart: null handle");
        return;
    }

    // 2. 防止重复启动正在运行的线程
    if (w->running.load()) {
        LOGD("worker is already running");
        return;
    }

    // 3. 回收上一次已经结束的线程
    if (w->worker.joinable()) {
        w->worker.join();
    }

    // 4. 设置运行状态
    w->running.store(true);

    //5.设置工作线程
    w->worker = std::thread([w] {
        ScopedJniEnv scoped(g_vm);
        JNIEnv *threadEnv = scoped.env;

        threadEnv->CallVoidMethod(w->listener, w->midStarted);
        //c++通过这个来调用kotlin对象里对应方法id的那个方法

        bool completed = true;
        for (int p = 0; p <= 100; p += 5){
            if (!w->running.load()) {
                completed = false;
                break;
            }

            for (int i = 0; i < kFrameSize; ++i) {
                w->frameBuf[i] = static_cast<uint8_t>((p + i) & 0xFF);
            }
            threadEnv->CallVoidMethod(
                    w->listener,
                    w->midFrame,
                    w->frameBuffer,
                    static_cast<jint>(kFrameSize)
               );
            if (checkException(threadEnv, "onFrame")) {
                break;
            }

            // ③ 回调进度
            threadEnv->CallVoidMethod(
                    w->listener,
                    w->midProgress,
                    (jint)p
            );
            if (checkException(threadEnv, "onProgress")) {
                break;
            }

            // ④ 模拟处理时间
            std::this_thread::sleep_for(
                    std::chrono::milliseconds(100)
            );
            }
        }
    );
}

static void nativeStop(JNIEnv* env,jobject thiz,jlong handle) {
    auto* w = reinterpret_cast<NativeWorker*>(handle);

    if (w == nullptr) {
        return;
    }

    LOGD("stopping worker");

    w->running.store(false);

    if (w->worker.joinable()) {
        w->worker.join();
    }

    LOGD("worker stopped");
}

static void nativeRelease(JNIEnv* env,jobject thiz,jlong handle) {
    LOGD("nativeRelease called");
    // 1. 将 Kotlin Long 还原为 C++ 指针
    auto* w = reinterpret_cast<NativeWorker*>(handle);

    // 2. 防止空指针
    if (w == nullptr) {
        return;
    }

    //3.暂停工作线程
    w->running.store(false);

    // 4. 等待工作线程退出
    if (w->worker.joinable()) {
        w->worker.join();
    }

    // 5. 释放 JNI 引用与 native 内存
    LOGD("release worker: %p", static_cast<void*>(w));

    destroyWorkerResources(env, w);
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
