package com.example.myapplication

import android.annotation.SuppressLint
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.SeekBar
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import com.example.myapplication.databinding.FragmentSecondBinding

class SecondFragment : Fragment() {

    private var _binding: FragmentSecondBinding? = null
    private val binding get() = _binding!!

    private var webSocketClient: WebSocketClient? = null
    
    // Состояние управления: [Вперед, Назад, Влево, Вправо, Стоп, Руль(0-10)]
    private val controlState = ByteArray(6) { 0 }.apply { this[5] = 5 } // Центр — 5
    
    private val handler = Handler(Looper.getMainLooper())
    private val sendRunnable = object : Runnable {
        override fun run() {
            webSocketClient?.send(controlState)
            handler.postDelayed(this, 50)
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        activity?.window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        _binding = FragmentSecondBinding.inflate(inflater, container, false)
        return binding.root
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        val ip = arguments?.getString("device_ip") ?: return
        val deviceName = arguments?.getString("device_name") ?: getString(R.string.default_device_name)
        
        (activity as? AppCompatActivity)?.supportActionBar?.title = deviceName

        val wsUrl = "ws://$ip:8888"

        webSocketClient = WebSocketClient(
            onMessage = { text ->
                activity?.runOnUiThread { 
                    _binding?.textViewStatus?.text = getString(R.string.status_telemetry, text) 
                }
            },
            onImageReceived = { bitmap ->
                activity?.runOnUiThread { 
                    _binding?.imageViewVideo?.setImageBitmap(bitmap) 
                }
            },
            onStatusChange = { isConnected ->
                activity?.runOnUiThread {
                    _binding?.textViewStatus?.text = if (isConnected) 
                        getString(R.string.status_connected) 
                    else 
                        getString(R.string.status_searching)
                }
            }
        )
        webSocketClient?.connect(wsUrl)

        // Настройка SeekBar (Руль)
        binding.seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                controlState[5] = progress.toByte()
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) {}

            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                // Возврат в центр при отпускании
                seekBar?.progress = 5
                controlState[5] = 5
            }
        })

        // Настраиваем кнопки
        setupButton(binding.btnUp, 0)
        setupButton(binding.btnDown, 1)
        setupButton(binding.btnLeft, 2)
        setupButton(binding.btnRight, 3)
        setupButton(binding.btnStop, 4)

        handler.post(sendRunnable)
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupButton(button: View, index: Int) {
        button.setOnTouchListener { v, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    controlState[index] = 1
                    v.isPressed = true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    controlState[index] = 0
                    v.isPressed = false
                }
            }
            true
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        handler.removeCallbacks(sendRunnable)
        activity?.window?.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        webSocketClient?.disconnect()
        _binding = null
    }
}
