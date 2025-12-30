package com.example.myapplication

import android.net.nsd.NsdServiceInfo
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView

class DeviceAdapter(private val onClick: (NsdServiceInfo) -> Unit) :
    RecyclerView.Adapter<DeviceAdapter.ViewHolder>() {

    private val devices = mutableListOf<NsdServiceInfo>()

    fun addDevice(service: NsdServiceInfo) {
        if (devices.none { it.serviceName == service.serviceName }) {
            devices.add(service)
            notifyDataSetChanged()
        }
    }

    fun clear() {
        devices.clear()
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(android.R.layout.simple_list_item_2, parent, false)
        return ViewHolder(view)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val device = devices[position]
        holder.name.text = device.serviceName
        holder.ip.text = device.host?.hostAddress ?: "Resolving..."
        holder.itemView.setOnClickListener { onClick(device) }
    }

    override fun getItemCount() = devices.size

    class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        val name: TextView = view.findViewById(android.R.id.text1)
        val ip: TextView = view.findViewById(android.R.id.text2)
    }
}
