idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS "."
  PRIV_REQUIRES esp_http_server esp_wifi nvs_flash esp_netif esp_event esp_psram esp_timer mdns esp32-camera
)
