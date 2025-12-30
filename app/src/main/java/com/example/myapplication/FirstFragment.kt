package com.example.myapplication

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import com.example.myapplication.databinding.FragmentFirstBinding

class FirstFragment : Fragment() {

    private var _binding: FragmentFirstBinding? = null
    private val binding get() = _binding!!

    private lateinit var mdnsHelper: MdnsHelper
    private lateinit var deviceAdapter: DeviceAdapter

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentFirstBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        deviceAdapter = DeviceAdapter { serviceInfo ->
            val ip = serviceInfo.host?.hostAddress
            if (ip != null) {
                navigateToControl(ip)
            }
        }

        binding.recyclerViewDevices.apply {
            layoutManager = LinearLayoutManager(context)
            adapter = deviceAdapter
        }

        mdnsHelper = MdnsHelper(requireContext()) { serviceInfo ->
            activity?.runOnUiThread {
                deviceAdapter.addDevice(serviceInfo)
                binding.progressBar.visibility = View.GONE
            }
        }

        binding.buttonScan.setOnClickListener {
            deviceAdapter.clear()
            binding.progressBar.visibility = View.VISIBLE
            mdnsHelper.startDiscovery()
            Toast.makeText(context, "Поиск запущен...", Toast.LENGTH_SHORT).show()
        }

        // РУЧНОЙ ВВОД IP
        binding.buttonConnectManual.setOnClickListener {
            val ip = binding.editTextIp.text.toString().trim()
            if (ip.isNotEmpty()) {
                navigateToControl(ip)
            } else {
                Toast.makeText(context, "Введите IP адрес", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun navigateToControl(ip: String) {
        val bundle = Bundle().apply {
            putString("device_ip", ip)
        }
        findNavController().navigate(R.id.action_FirstFragment_to_SecondFragment, bundle)
    }

    override fun onPause() {
        super.onPause()
        if (::mdnsHelper.isInitialized) {
            mdnsHelper.stopDiscovery()
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}
