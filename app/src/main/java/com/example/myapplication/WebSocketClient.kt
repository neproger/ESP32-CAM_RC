package com.example.myapplication

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Handler
import android.os.Looper
import android.util.Log
import okhttp3.*
import okio.ByteString
import okio.ByteString.Companion.toByteString
import java.util.concurrent.TimeUnit

class WebSocketClient(
    private val onMessage: (String) -> Unit,
    private val onImageReceived: (Bitmap) -> Unit,
    private val onStatusChange: (Boolean) -> Unit
) {
    private var client = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .pingInterval(10, TimeUnit.SECONDS)
        .build()

    private var webSocket: WebSocket? = null
    private var isManualClose = false
    private var currentUrl: String? = null
    private val handler = Handler(Looper.getMainLooper())

    fun connect(url: String) {
        currentUrl = url
        isManualClose = false
        val request = Request.Builder().url(url).build()
        
        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                onStatusChange(true)
            }

            override fun onMessage(webSocket: WebSocket, text: String) = onMessage(text)

            override fun onMessage(webSocket: WebSocket, bytes: ByteString) {
                val bitmap = BitmapFactory.decodeByteArray(bytes.toByteArray(), 0, bytes.size)
                if (bitmap != null) onImageReceived(bitmap)
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                onStatusChange(false)
                attemptReconnect()
            }

            override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
                onStatusChange(false)
                if (!isManualClose) attemptReconnect()
            }
        })
    }

    private fun attemptReconnect() {
        if (isManualClose) return
        handler.postDelayed({ currentUrl?.let { connect(it) } }, 2000)
    }

    fun send(message: String) = webSocket?.send(message)

    // НОВЫЙ МЕТОД: Отправка байтов
    fun send(bytes: ByteArray) = webSocket?.send(bytes.toByteString())

    fun disconnect() {
        isManualClose = true
        webSocket?.close(1000, "User disconnect")
        webSocket = null
        handler.removeCallbacksAndMessages(null)
    }
}
