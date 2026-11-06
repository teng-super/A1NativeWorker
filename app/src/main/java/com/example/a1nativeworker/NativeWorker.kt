package com.example.a1nativeworker

import java.nio.ByteBuffer

class NativeWorker (listener: Listener)  {// 回调接口：native 线程会调这些方法
    interface Listener {
        fun onStarted()
        fun onProgress(percent: Int)
        fun onFrame(buffer: ByteBuffer, size: Int) // 零拷贝：buffer 直接映射native 内存
        fun onError(code: Int, message: String)
        fun onFinished()
    }
    private var handle: Long = nativeCreate(listener)//这个保存的是传入的native端创建的指针的值
    fun start() { if (handle != 0L) nativeStart(handle) }//对外接口
    fun stop() { if (handle != 0L) nativeStop(handle) }
    fun release() {
        if (handle != 0L) {
            nativeRelease(handle)
            handle = 0L
        }
    }
    private external fun nativeCreate(listener: Listener): Long
    private external fun nativeStart(handle: Long)
    private external fun nativeStop(handle: Long)
    private external fun nativeRelease(handle: Long)

    companion object {
        init {
            System.loadLibrary("native-lib")
        }
    }


}