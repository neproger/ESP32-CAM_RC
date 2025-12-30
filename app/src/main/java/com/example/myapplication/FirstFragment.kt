package com.example.myapplication

import android.os.Bundle
import android.os.Handler
import android.os.Looper
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
    private val handler = Handler(Looper.getMainLooper())

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
            val name = serviceInfo.serviceName
            if (ip != null) {
                stopScanningUI()
                navigateToControl(ip, name ?: getString(R.string.default_device_name))
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
                binding.buttonScan.isEnabled = true
            }
        }

        binding.buttonScan.setOnClickListener {
            if (mdnsHelper.isScanning()) return@setOnClickListener

            startScanningUI()
            mdnsHelper.startDiscovery()
            
            handler.postDelayed({
                stopScanningUI()
            }, 10000)
        }

        binding.buttonConnectManual.setOnClickListener {
            val ip = binding.editTextIp.text.toString().trim()
            if (ip.isNotEmpty()) {
                navigateToControl(ip, getString(R.string.manual_ip_name))
            } else {
                Toast.makeText(context, R.string.toast_enter_ip, Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun startScanningUI() {
        deviceAdapter.clear()
        binding.progressBar.visibility = View.VISIBLE
        binding.buttonScan.isEnabled = false
        binding.buttonScan.text = getString(R.string.button_scanning_label)
        Toast.makeText(context, R.string.toast_scan_started, Toast.LENGTH_SHORT).show()
    }

    private fun stopScanningUI() {
        activity?.runOnUiThread {
            if (_binding != null) {
                binding.progressBar.visibility = View.GONE
                binding.buttonScan.isEnabled = true
                binding.buttonScan.text = getString(R.string.button_scan_label)
                mdnsHelper.stopDiscovery()
            }
        }
    }

    private fun navigateToControl(ip: String, name: String) {
        val bundle = Bundle().apply {
            putString("device_ip", ip)
            putString("device_name", name)
        }
        findNavController().navigate(R.id.action_FirstFragment_to_SecondFragment, bundle)
    }

    override fun onPause() {
        super.onPause()
        handler.removeCallbacksAndMessages(null)
        if (::mdnsHelper.isInitialized) {
            mdnsHelper.stopDiscovery()
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        handler.removeCallbacksAndMessages(null)
        _binding = null
    }
}
