package com.example.a1nativeworker

import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import java.nio.ByteBuffer

class MainActivity : AppCompatActivity() {

    private var worker: NativeWorker? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // 获取界面上的控件
        val statusText = findViewById<TextView>(R.id.statusText)
        val btnStart = findViewById<Button>(R.id.btnStart)
        val btnStop = findViewById<Button>(R.id.btnStop)

        // 创建回调对象
        val listener = object : NativeWorker.Listener {

            override fun onStarted() {
                runOnUiThread {
                    statusText.text = "任务开始"
                }
            }

            override fun onProgress(percent: Int) {
                runOnUiThread {
                    statusText.text = "进度：$percent%"
                }
            }

            override fun onFrame(buffer: ByteBuffer, size: Int) {
                if (size > 0) {
                    val firstByte = buffer.get(0).toInt() and 0xFF
                    Log.d("A1NativeWorker", "size=$size, byte=$firstByte")
                }
            }

            override fun onError(code: Int, message: String) {
                runOnUiThread {
                    statusText.text = "错误：$message"
                }
            }

            override fun onFinished() {
                runOnUiThread {
                    statusText.text = "任务完成！"
                }
            }
        }

        // 创建 Worker
        worker = NativeWorker(listener)

        // 开始按钮
        btnStart.setOnClickListener {//当用户点击这个按钮时，执行指定的操作
            worker?.start()
        }

        // 停止按钮
        btnStop.setOnClickListener {
            worker?.stop()
            statusText.text = "已停止"
        }
    }

    override fun onStop() {
        worker?.stop()
        super.onStop()
    }

    override fun onDestroy() {
        worker?.release()
        worker = null
        super.onDestroy()
    }
}