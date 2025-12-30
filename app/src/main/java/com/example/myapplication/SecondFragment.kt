package com.example.myapplication

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import com.example.myapplication.databinding.FragmentSecondBinding

/**
 * A simple [Fragment] subclass as the second destination in the navigation.
 */
class SecondFragment : Fragment() {

    private var _binding: FragmentSecondBinding? = null
    private val binding get() = _binding!!

    private var webSocketClient: WebSocketClient? = null

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentSecondBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        // Получаем IP из аргументов
        val ip = arguments?.getString("device_ip")
        if (ip == null) {
            binding.textViewStatus.text = "Ошибка: IP не получен"
            return
        }

        val wsUrl = "ws://$ip:81"
        binding.textViewStatus.text = "Подключение к $ip..."

        // Инициализируем WebSocket
        webSocketClient = WebSocketClient(
            onMessage = { text ->
                activity?.runOnUiThread {
                    binding.textViewStatus.text = "Телеметрия: $text"
                }
            },
            onImageReceived = { bitmap ->
                activity?.runOnUiThread {
                    binding.imageViewVideo.setImageBitmap(bitmap)
                }
            },
            onStatusChange = { isConnected ->
                activity?.runOnUiThread {
                    binding.textViewStatus.text =
                        if (isConnected) "Статус: Подключено" else "Статус: Отключено"
                }
            }
        )
        webSocketClient?.connect(wsUrl)

        // Кнопки управления
        binding.btnUp.setOnClickListener { webSocketClient?.send("up") }
        binding.btnDown.setOnClickListener { webSocketClient?.send("down") }
        binding.btnLeft.setOnClickListener { webSocketClient?.send("left") }
        binding.btnRight.setOnClickListener { webSocketClient?.send("right") }
        binding.btnStop.setOnClickListener { webSocketClient?.send("stop") }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        webSocketClient?.disconnect()
        _binding = null
    }
}
