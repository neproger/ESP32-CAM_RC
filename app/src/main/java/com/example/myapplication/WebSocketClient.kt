package com.example.myapplication

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Handler
import android.os.Looper
import okhttp3.*
import okio.ByteString
import okio.ByteString.Companion.toByteString
import java.nio.ByteBuffer
import java.nio.ByteOrder
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

    private data class RawHeader(
        val width: Int,
        val height: Int,
        val format: Int,
        val payloadLen: Int
    )

    private var pendingRawHeader: RawHeader? = null

    private fun isJpeg(data: ByteArray): Boolean =
        data.size >= 2 && data[0] == 0xFF.toByte() && data[1] == 0xD8.toByte()

    private fun parseRawHeader(data: ByteArray): RawHeader? {
        // magic "RAWH" + version(1) + format(1) + w(u16 LE) + h(u16 LE) + len(u32 LE)
        if (data.size < 14) return null
        if (data[0] != 'R'.code.toByte() || data[1] != 'A'.code.toByte() ||
            data[2] != 'W'.code.toByte() || data[3] != 'H'.code.toByte()
        ) return null

        val buf = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
        buf.position(4)
        val version = buf.get().toInt() and 0xFF
        val format = buf.get().toInt() and 0xFF
        val width = buf.short.toInt() and 0xFFFF
        val height = buf.short.toInt() and 0xFFFF
        val payloadLen = buf.int

        if (version != 1) return null
        if (width <= 0 || height <= 0) return null
        if (payloadLen <= 0) return null
        if (format != 0 && format != 1) return null
        return RawHeader(width, height, format, payloadLen)
    }

    private fun decodeRgb565(width: Int, height: Int, payload: ByteArray): Bitmap? {
        val expected = width * height * 2
        if (payload.size < expected) return null

        // ESP32 camera buffers are commonly RGB565 big-endian (MSB first), while Android's RGB_565
        // pixel storage is typically little-endian. Swap bytes per pixel to match Android.
        var i = 0
        while (i + 1 < expected) {
            val b0 = payload[i]
            payload[i] = payload[i + 1]
            payload[i + 1] = b0
            i += 2
        }
        
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565)
        val buffer = ByteBuffer.wrap(payload, 0, expected)
        // Если цвета инвертированы или искажены, здесь может понадобиться .order(ByteOrder.LITTLE_ENDIAN)
        bitmap.copyPixelsFromBuffer(buffer)
        return bitmap
    }

    private fun decodeGray8(width: Int, height: Int, payload: ByteArray): Bitmap? {
        val expected = width * height
        if (payload.size < expected) return null

        val rgb565 = ByteArray(expected * 2)
        var src = 0
        var dst = 0
        while (src < expected) {
            val y = payload[src].toInt() and 0xFF
            val r = (y shr 3) and 0x1F
            val g = (y shr 2) and 0x3F
            val b = (y shr 3) and 0x1F
            val pix = (r shl 11) or (g shl 5) or b
            // little-endian for Android RGB_565 buffer
            rgb565[dst] = (pix and 0xFF).toByte()
            rgb565[dst + 1] = ((pix shr 8) and 0xFF).toByte()
            src++
            dst += 2
        }

        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565)
        bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(rgb565))
        return bitmap
    }

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
                val data = bytes.toByteArray()

                // 1. Check for JPEG
                if (isJpeg(data)) {
                    pendingRawHeader = null
                    val bitmap = BitmapFactory.decodeByteArray(data, 0, data.size)
                    if (bitmap != null) onImageReceived(bitmap)
                    return
                }

                // 2. Check for RAWH Header
                val header = parseRawHeader(data)
                if (header != null) {
                    pendingRawHeader = header
                    return
                }

                // 3. Process Payload if we have a header from previous packet
                val pending = pendingRawHeader
                if (pending != null) {
                    pendingRawHeader = null
                    val bitmap = when (pending.format) {
                        0 -> decodeRgb565(pending.width, pending.height, data)
                        1 -> decodeGray8(pending.width, pending.height, data)
                        else -> null
                    }
                    if (bitmap != null) onImageReceived(bitmap)
                    return
                }
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

    fun send(bytes: ByteArray) = webSocket?.send(bytes.toByteString())

    fun disconnect() {
        isManualClose = true
        webSocket?.close(1000, "User disconnect")
        webSocket = null
        handler.removeCallbacksAndMessages(null)
    }
}
