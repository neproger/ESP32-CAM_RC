package com.example.myapplication

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import okhttp3.*
import okio.ByteString

class WebSocketClient(
    private val onMessage: (String) -> Unit,
    private val onImageReceived: (Bitmap) -> Unit,
    private val onStatusChange: (Boolean) -> Unit
) {
    private val client = OkHttpClient()
    private var webSocket: WebSocket? = null

    fun connect(url: String) {
        val request = Request.Builder().url(url).build()
        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                onStatusChange(true)
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                onMessage(text)
            }

            override fun onMessage(webSocket: WebSocket, bytes: ByteString) {
                val byteArray = bytes.toByteArray()
                val bitmap = BitmapFactory.decodeByteArray(byteArray, 0, byteArray.size)
                if (bitmap != null) {
                    onImageReceived(bitmap)
                }
            }

            override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
                onStatusChange(false)
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                onStatusChange(false)
            }
        })
    }

    fun send(message: String) {
        webSocket?.send(message)
    }

    fun disconnect() {
        webSocket?.close(1000, "Canceled by user")
    }
}
