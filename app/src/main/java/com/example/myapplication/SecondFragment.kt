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
import androidx.fragment.app.Fragment
import com.example.myapplication.databinding.FragmentSecondBinding

class SecondFragment : Fragment() {

    private var _binding: FragmentSecondBinding? = null
    private val binding get() = _binding!!

    private var webSocketClient: WebSocketClient? = null
    
    // Состояние кнопок: [Вперед, Назад, Влево, Вправо, Стоп]
    private val controlState = ByteArray(5) { 0 }
    
    private val handler = Handler(Looper.getMainLooper())
    private val sendRunnable = object : Runnable {
        override fun run() {
            webSocketClient?.send(controlState)
            handler.postDelayed(this, 50) // Отправка каждые 50мс (20 Гц)
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
        val wsUrl = "ws://$ip:8888"

        webSocketClient = WebSocketClient(
            onMessage = { text ->
                activity?.runOnUiThread { 
                    _binding?.textViewStatus?.text = "Телеметрия: $text" 
                }
            },
            onImageReceived = { bitmap ->
                activity?.runOnUiThread { 
                    _binding?.imageViewVideo?.setImageBitmap(bitmap) 
                }
            },
            onStatusChange = { isConnected ->
                activity?.runOnUiThread {
                    _binding?.textViewStatus?.text = if (isConnected) "Статус: Подключено ✅" else "Статус: Поиск... ⏳"
                }
            }
        )
        webSocketClient?.connect(wsUrl)

        // Настраиваем обработку зажатия кнопок
        setupButton(binding.btnUp, 0)
        setupButton(binding.btnDown, 1)
        setupButton(binding.btnLeft, 2)
        setupButton(binding.btnRight, 3)
        setupButton(binding.btnStop, 4)

        // Запускаем цикл непрерывной отправки
        handler.post(sendRunnable)
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupButton(button: View, index: Int) {
        button.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> controlState[index] = 1
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> controlState[index] = 0
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
