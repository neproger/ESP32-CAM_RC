package com.example.myapplication

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.net.wifi.WifiManager
import android.util.Log

class MdnsHelper(context: Context, private val onDeviceFound: (NsdServiceInfo) -> Unit) {

    private val nsdManager = context.getSystemService(Context.NSD_SERVICE) as NsdManager
    private val wifiManager = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
    private var multicastLock: WifiManager.MulticastLock? = null
    
    private val serviceType = "_esp_rc._tcp." 
    private var isDiscoveryRunning = false

    private val discoveryListener = object : NsdManager.DiscoveryListener {
        override fun onDiscoveryStarted(regType: String) {
            isDiscoveryRunning = true
            Log.d("mDNS", "Поиск запущен: $regType")
        }

        override fun onServiceFound(service: NsdServiceInfo) {
            if (service.serviceType.contains("esp_rc")) {
                nsdManager.resolveService(service, object : NsdManager.ResolveListener {
                    override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                        Log.e("mDNS", "Ошибка разрешения адреса: $errorCode")
                    }

                    override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                        Log.d("mDNS", "Устройство найдено: ${serviceInfo.host.hostAddress}")
                        onDeviceFound(serviceInfo)
                    }
                })
            }
        }

        override fun onServiceLost(service: NsdServiceInfo) {
            Log.d("mDNS", "Устройство потеряно")
        }

        override fun onDiscoveryStopped(regType: String) {
            isDiscoveryRunning = false
            Log.d("mDNS", "Поиск остановлен")
        }

        override fun onStartDiscoveryFailed(regType: String, errorCode: Int) {
            isDiscoveryRunning = false
            stopDiscovery()
        }

        override fun onStopDiscoveryFailed(regType: String, errorCode: Int) {
            isDiscoveryRunning = false
        }
    }

    fun isScanning(): Boolean = isDiscoveryRunning

    fun startDiscovery() {
        if (isDiscoveryRunning) return

        if (multicastLock == null) {
            multicastLock = wifiManager.createMulticastLock("MdnsLock")
            multicastLock?.setReferenceCounted(true)
        }
        multicastLock?.acquire()
        
        try {
            nsdManager.discoverServices(serviceType, NsdManager.PROTOCOL_DNS_SD, discoveryListener)
        } catch (e: Exception) {
            Log.e("mDNS", "Ошибка при запуске: ${e.message}")
            isDiscoveryRunning = false
        }
    }

    fun stopDiscovery() {
        if (!isDiscoveryRunning) return
        try {
            nsdManager.stopServiceDiscovery(discoveryListener)
        } catch (e: Exception) {
            Log.e("mDNS", "Ошибка при остановке: ${e.message}")
        } finally {
            if (multicastLock?.isHeld == true) multicastLock?.release()
            isDiscoveryRunning = false
        }
    }
}
