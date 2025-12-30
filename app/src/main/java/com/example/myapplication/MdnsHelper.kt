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
    
    // Стандартный тип сервиса для вашей машинки
    private val serviceType = "_esp_rc._tcp." 

    private val discoveryListener = object : NsdManager.DiscoveryListener {
        override fun onDiscoveryStarted(regType: String) {
            Log.d("mDNS", "Поиск запущен: $regType")
        }

        override fun onServiceFound(service: NsdServiceInfo) {
            // Резолвим только наш тип сервиса
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

        override fun onDiscoveryStopped(regType: String) {}
        override fun onStartDiscoveryFailed(regType: String, errorCode: Int) { stopDiscovery() }
        override fun onStopDiscoveryFailed(regType: String, errorCode: Int) {}
    }

    fun startDiscovery() {
        if (multicastLock == null) {
            multicastLock = wifiManager.createMulticastLock("MdnsLock")
            multicastLock?.setReferenceCounted(true)
        }
        multicastLock?.acquire()
        nsdManager.discoverServices(serviceType, NsdManager.PROTOCOL_DNS_SD, discoveryListener)
    }

    fun stopDiscovery() {
        try {
            nsdManager.stopServiceDiscovery(discoveryListener)
            if (multicastLock?.isHeld == true) multicastLock?.release()
        } catch (e: Exception) {}
    }
}
