package com.example.myapplication

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean

class TcpClient(private val ip: String, private val port: Int) {
    private var socket: Socket? = null
    private var outputStream: OutputStream? = null
    private val isConnected = AtomicBoolean(false)

    suspend fun connect(): Boolean = withContext(Dispatchers.IO) {
        try {
            socket = Socket()
            socket?.connect(InetSocketAddress(ip, port), 5000)
            outputStream = socket?.getOutputStream()
            isConnected.set(true)
            true
        } catch (e: Exception) {
            e.printStackTrace()
            false
        }
    }

    suspend fun sendCommand(command: String) = withContext(Dispatchers.IO) {
        try {
            if (isConnected.get()) {
                outputStream?.write(command.toByteArray())
                outputStream?.flush()
            }
        } catch (e: Exception) {
            e.printStackTrace()
            disconnect()
        }
    }

    fun disconnect() {
        try {
            isConnected.set(false)
            outputStream?.close()
            socket?.close()
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    fun isConnected(): Boolean = isConnected.get()
}
