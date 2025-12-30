package com.example.myapplication

import android.net.nsd.NsdServiceInfo
import android.os.Bundle
import android.util.Log
import com.google.android.material.snackbar.Snackbar
import androidx.appcompat.app.AppCompatActivity
import androidx.navigation.findNavController
import androidx.navigation.ui.AppBarConfiguration
import androidx.navigation.ui.navigateUp
import androidx.navigation.ui.setupActionBarWithNavController
import android.view.Menu
import android.view.MenuItem
import com.example.myapplication.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var appBarConfiguration: AppBarConfiguration
    private lateinit var binding: ActivityMainBinding
    private lateinit var mdnsHelper: MdnsHelper

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setSupportActionBar(binding.toolbar)

        val navController = findNavController(R.id.nav_host_fragment_content_main)
        appBarConfiguration = AppBarConfiguration(navController.graph)
        setupActionBarWithNavController(navController, appBarConfiguration)

        mdnsHelper = MdnsHelper(this) { serviceInfo ->
            runOnUiThread {
                val ip = serviceInfo.host.hostAddress
                val name = serviceInfo.serviceName
                Snackbar.make(binding.root, "Найдено: $name ($ip)", Snackbar.LENGTH_LONG)
                    .setAnchorView(R.id.fab).show()
                Log.d("MainActivity", "ESP32 found at $ip")
            }
        }

        binding.fab.setOnClickListener { view ->
            Snackbar.make(view, "Поиск ESP32...", Snackbar.LENGTH_SHORT)
                .setAnchorView(R.id.fab).show()
            mdnsHelper.startDiscovery()
        }
    }

    override fun onPause() {
        super.onPause()
        mdnsHelper.stopDiscovery()
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menuInflater.inflate(R.menu.menu_main, menu)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        return when (item.itemId) {
            R.id.action_settings -> true
            else -> super.onOptionsItemSelected(item)
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        val navController = findNavController(R.id.nav_host_fragment_content_main)
        return navController.navigateUp(appBarConfiguration)
                || super.onSupportNavigateUp()
    }
}
