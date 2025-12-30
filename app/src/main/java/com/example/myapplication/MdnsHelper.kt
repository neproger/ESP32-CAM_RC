package com.example.myapplication

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.util.Log

class MdnsHelper(context: Context, private val onDeviceFound: (NsdServiceInfo) -> Unit) {

    private val nsdManager = context.getSystemService(Context.NSD_SERVICE) as NsdManager
    private val serviceType = "_http._tcp." // Стандарт для ESP32 web-сервера

    private val discoveryListener = object : NsdManager.DiscoveryListener {
        override fun onDiscoveryStarted(regType: String) {
            Log.d("MdnsHelper", "Discovery started")
        }

        override fun onServiceFound(service: NsdServiceInfo) {
            Log.d("MdnsHelper", "Service found: ${service.serviceName}")
            if (service.serviceType == serviceType || service.serviceType == "$serviceType.") {
                nsdManager.resolveService(service, object : NsdManager.ResolveListener {
                    override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                        Log.e("MdnsHelper", "Resolve failed: $errorCode")
                    }

                    override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                        Log.d("MdnsHelper", "Service resolved: ${serviceInfo.host.hostAddress}")
                        onDeviceFound(serviceInfo)
                    }
                })
            }
        }

        override fun onServiceLost(service: NsdServiceInfo) {
            Log.d("MdnsHelper", "Service lost: ${service.serviceName}")
        }

        override fun onDiscoveryStopped(regType: String) {
            Log.d("MdnsHelper", "Discovery stopped")
        }

        override fun onStartDiscoveryFailed(regType: String, errorCode: Int) {
            Log.e("MdnsHelper", "Start discovery failed: $errorCode")
            stopDiscovery()
        }

        override fun onStopDiscoveryFailed(regType: String, errorCode: Int) {
            Log.e("MdnsHelper", "Stop discovery failed: $errorCode")
            nsdManager.stopServiceDiscovery(this)
        }
    }

    fun startDiscovery() {
        nsdManager.discoverServices(serviceType, NsdManager.PROTOCOL_DNS_SD, discoveryListener)
    }

    fun stopDiscovery() {
        try {
            nsdManager.stopServiceDiscovery(discoveryListener)
        } catch (e: Exception) {
            Log.e("MdnsHelper", "Error stopping discovery", e)
        }
    }
}
