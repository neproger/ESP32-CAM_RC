#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_camera.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "img_converters.h"

#include "rc_config.h"

static const char *TAG = MDNS_INSTANCE;

static httpd_handle_t httpServer = NULL;
static httpd_handle_t provisionServer = NULL;
static uint8_t controlState[RC_CONTROL_LEN] = {0};

static volatile bool camera_ok = false;
static bool wifi_handlers_registered = false;
static bool wifi_stack_initialized = false;
static esp_netif_t *wifi_netif_sta = NULL;
static esp_netif_t *wifi_netif_ap = NULL;

static EventGroupHandle_t wifi_event_group = NULL;
static const EventBits_t WIFI_CONNECTED_BIT = BIT0;
static const EventBits_t WIFI_FAIL_BIT = BIT1;
static int sta_retry_count = 0;

static esp_timer_handle_t restart_timer = NULL;
static bool mdns_started = false;
static bool mdns_service_added_esp_rc = false;
static bool mdns_service_added_rcws = false;
static bool mdns_service_added_http = false;

static uint32_t frame_interval_ms(void)
{
	if (STREAM_FPS <= 0)
		return 200;
	return 1000U / (uint32_t)STREAM_FPS;
}

static void ensure_mdns_started(void)
{
	if (mdns_started)
		return;
	mdns_started = true;

	ESP_ERROR_CHECK(mdns_init());
	ESP_ERROR_CHECK(mdns_hostname_set(MDNS_HOSTNAME));
	ESP_ERROR_CHECK(mdns_instance_name_set(MDNS_INSTANCE));
	ESP_LOGI(TAG, "mDNS started: %s.local", MDNS_HOSTNAME);
}

static void mdns_advertise_rc_ws(void)
{
	ensure_mdns_started();
	mdns_txt_item_t txt[] = {{"path", "/"}, {"proto", "ws"}};

	// Android app is searching for: _esp_rc._tcp.
	if (!mdns_service_added_esp_rc)
	{
		mdns_service_added_esp_rc = true;
		(void)mdns_service_add(NULL, "_esp_rc", "_tcp", RC_WS_PORT, txt,
							   sizeof(txt) / sizeof(txt[0]));
	}

	// Backward-compatible alias.
	if (!mdns_service_added_rcws)
	{
		mdns_service_added_rcws = true;
		(void)mdns_service_add(NULL, "_rcws", "_tcp", RC_WS_PORT, txt, sizeof(txt) / sizeof(txt[0]));
	}
}

static void mdns_advertise_provision_http(void)
{
	ensure_mdns_started();
	if (mdns_service_added_http)
		return;
	mdns_service_added_http = true;

	mdns_txt_item_t txt[] = {{"path", "/"}, {"role", "provision"}};
	(void)mdns_service_add(NULL, "_http", "_tcp", 80, txt, sizeof(txt) / sizeof(txt[0]));
}

static void log_wifi_password(const char *context, const char *pass)
{
	if (!pass)
		pass = "";
#if PROVISION_LOG_SENSITIVE
	ESP_LOGI(TAG, "%s: pass='%s' (len=%u)", context, pass, (unsigned)strlen(pass));
#else
	const size_t len = strlen(pass);
	if (len == 0)
	{
		ESP_LOGI(TAG, "%s: pass_len=0", context);
	}
	else if (len == 1)
	{
		ESP_LOGI(TAG, "%s: pass_len=1 pass_mask='%c'", context, pass[0]);
	}
	else
	{
		ESP_LOGI(TAG, "%s: pass_len=%u pass_mask='%c***%c'", context, pass[0],
				 pass[len - 1]);
	}
#endif
}

static void restart_timer_cb(void *arg)
{
	(void)arg;
	esp_restart();
}

static void schedule_restart_ms(uint32_t delay_ms)
{
	if (!restart_timer)
	{
		const esp_timer_create_args_t args = {
			.callback = &restart_timer_cb,
			.arg = NULL,
			.dispatch_method = ESP_TIMER_TASK,
			.name = "restart_timer",
			.skip_unhandled_events = true,
		};
		ESP_ERROR_CHECK(esp_timer_create(&args, &restart_timer));
	}

	(void)esp_timer_stop(restart_timer);
	ESP_ERROR_CHECK(esp_timer_start_once(restart_timer, (uint64_t)delay_ms * 1000ULL));
}

static esp_err_t nvs_load_wifi_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
	if (!ssid || ssid_len == 0 || !pass || pass_len == 0)
		return ESP_ERR_INVALID_ARG;
	ssid[0] = '\0';
	pass[0] = '\0';

	nvs_handle_t nvs = 0;
	esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs);
	if (err != ESP_OK)
		return err;

	size_t ssid_size = ssid_len;
	err = nvs_get_str(nvs, "ssid", ssid, &ssid_size);
	if (err != ESP_OK)
	{
		nvs_close(nvs);
		return err;
	}

	size_t pass_size = pass_len;
	err = nvs_get_str(nvs, "pass", pass, &pass_size);
	if (err == ESP_ERR_NVS_NOT_FOUND)
		err = ESP_OK;

	nvs_close(nvs);
	if (err == ESP_OK)
	{
		ESP_LOGI(TAG, "Loaded Wi-Fi creds from NVS: ssid='%s'", ssid);
		log_wifi_password("Loaded Wi-Fi creds from NVS", pass);
	}
	return err;
}

static esp_err_t nvs_save_wifi_creds(const char *ssid, const char *pass)
{
	if (!ssid || strlen(ssid) == 0)
		return ESP_ERR_INVALID_ARG;
	nvs_handle_t nvs = 0;
	esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs);
	if (err != ESP_OK)
		return err;

	err = nvs_set_str(nvs, "ssid", ssid);
	if (err == ESP_OK)
		err = nvs_set_str(nvs, "pass", pass ? pass : "");
	if (err == ESP_OK)
		err = nvs_commit(nvs);
	nvs_close(nvs);
	if (err == ESP_OK)
	{
		ESP_LOGI(TAG, "Saved Wi-Fi creds to NVS: ssid='%s'", ssid);
		log_wifi_password("Saved Wi-Fi creds to NVS", pass ? pass : "");
	}
	return err;
}

static void nvs_clear_wifi_creds(void)
{
	nvs_handle_t nvs = 0;
	if (nvs_open("wifi", NVS_READWRITE, &nvs) != ESP_OK)
		return;
	(void)nvs_erase_key(nvs, "ssid");
	(void)nvs_erase_key(nvs, "pass");
	(void)nvs_commit(nvs);
	nvs_close(nvs);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
							   void *event_data)
{
	(void)arg;

	if (event_base == WIFI_EVENT)
	{
		switch (event_id)
		{
		case WIFI_EVENT_AP_START:
			ESP_LOGI(TAG, "WiFi AP started");
			ensure_mdns_started();
			break;
		case WIFI_EVENT_AP_STOP:
			ESP_LOGW(TAG, "WiFi AP stopped");
			break;
		case WIFI_EVENT_AP_STACONNECTED:
		{
			const wifi_event_ap_staconnected_t *e = (const wifi_event_ap_staconnected_t *)event_data;
			ESP_LOGI(TAG, "AP station connected: " MACSTR " (aid=%d)", MAC2STR(e->mac), e->aid);
			break;
		}
		case WIFI_EVENT_AP_STADISCONNECTED:
		{
			const wifi_event_ap_stadisconnected_t *e =
				(const wifi_event_ap_stadisconnected_t *)event_data;
			ESP_LOGI(TAG, "AP station disconnected: " MACSTR " (aid=%d)", MAC2STR(e->mac), e->aid);
			memset(controlState, 0, sizeof(controlState));
			break;
		}
		case WIFI_EVENT_STA_START:
			ESP_LOGI(TAG, "WiFi STA start");
			break;
		case WIFI_EVENT_STA_CONNECTED:
			ESP_LOGI(TAG, "WiFi STA connected");
			sta_retry_count = 0;
			break;
		case WIFI_EVENT_STA_DISCONNECTED:
		{
			const wifi_event_sta_disconnected_t *e = (const wifi_event_sta_disconnected_t *)event_data;
			ESP_LOGW(TAG, "WiFi STA disconnected (reason=%d)", (int)e->reason);
			if (sta_retry_count < WIFI_STA_MAX_RETRY)
			{
				sta_retry_count++;
				ESP_LOGI(TAG, "Retrying STA connect (%d/%d)...", sta_retry_count, WIFI_STA_MAX_RETRY);
				(void)esp_wifi_connect();
			}
			else if (wifi_event_group)
			{
				xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
			}
			break;
		}
		default:
			break;
		}
	}
	else if (event_base == IP_EVENT)
	{
		switch (event_id)
		{
		case IP_EVENT_STA_GOT_IP:
		{
			const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)event_data;
			ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&e->ip_info.ip));
			if (wifi_event_group)
				xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
			ensure_mdns_started();
			break;
		}
		default:
			break;
		}
	}
}

static esp_err_t init_camera(void)
{
	camera_config_t config = {
		.ledc_channel = LEDC_CHANNEL_0,
		.ledc_timer = LEDC_TIMER_0,
		.pin_d0 = CAM_PIN_Y2,
		.pin_d1 = CAM_PIN_Y3,
		.pin_d2 = CAM_PIN_Y4,
		.pin_d3 = CAM_PIN_Y5,
		.pin_d4 = CAM_PIN_Y6,
		.pin_d5 = CAM_PIN_Y7,
		.pin_d6 = CAM_PIN_Y8,
		.pin_d7 = CAM_PIN_Y9,
		.pin_xclk = CAM_PIN_XCLK,
		.pin_pclk = CAM_PIN_PCLK,
		.pin_vsync = CAM_PIN_VSYNC,
		.pin_href = CAM_PIN_HREF,
		.pin_sccb_sda = CAM_PIN_SIOD,
		.pin_sccb_scl = CAM_PIN_SIOC,
		.pin_pwdn = CAM_PIN_PWDN,
		.pin_reset = CAM_PIN_RESET,
		.xclk_freq_hz = 20000000,
		.pixel_format =
#if (CAM_STREAM_MODE == CAM_STREAM_MODE_RGB565_RAW) || (CAM_STREAM_MODE == CAM_STREAM_MODE_GRAY8)
			PIXFORMAT_RGB565,
#else
			PIXFORMAT_JPEG,
#endif
		.frame_size = FRAMESIZE_QVGA,
		.jpeg_quality = 12,
		.fb_count = 2,
		.fb_location = CAMERA_FB_IN_PSRAM,
		.grab_mode = CAMERA_GRAB_WHEN_EMPTY,
	};

	esp_err_t err = esp_camera_init(&config);
#if (CAM_STREAM_MODE != CAM_STREAM_MODE_RGB565_RAW) && (CAM_STREAM_MODE != CAM_STREAM_MODE_GRAY8)
	if (err == ESP_ERR_NOT_SUPPORTED && config.pixel_format == PIXFORMAT_JPEG)
	{
		ESP_LOGW(TAG, "Sensor does not support JPEG, retrying with RGB565 + software JPEG");
		(void)esp_camera_deinit();
		config.pixel_format = PIXFORMAT_RGB565;
		err = esp_camera_init(&config);
	}
#endif
	if (err != ESP_OK && config.fb_location == CAMERA_FB_IN_PSRAM)
	{
		ESP_LOGW(TAG, "Camera init failed with PSRAM fb, retrying with DRAM fb");
		(void)esp_camera_deinit();
		config.fb_location = CAMERA_FB_IN_DRAM;
		config.fb_count = 1;
		if (config.frame_size > FRAMESIZE_QQVGA)
			config.frame_size = FRAMESIZE_QQVGA;
		err = esp_camera_init(&config);
	}
	return err;
}

static void wifi_init_common(void)
{
	if (!wifi_stack_initialized)
	{
		wifi_stack_initialized = true;
		ESP_ERROR_CHECK(esp_netif_init());

		esp_err_t loop_err = esp_event_loop_create_default();
		if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE)
			ESP_ERROR_CHECK(loop_err);

		ESP_ERROR_CHECK(esp_wifi_init(&(wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT()));
		ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	}

	if (!wifi_handlers_registered)
	{
		wifi_handlers_registered = true;
		ESP_ERROR_CHECK(
			esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
		ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler,
												   NULL));
	}
}

static void wifi_ensure_netifs(bool need_ap, bool need_sta)
{
	if (need_ap && !wifi_netif_ap)
		wifi_netif_ap = esp_netif_create_default_wifi_ap();
	if (need_sta && !wifi_netif_sta)
		wifi_netif_sta = esp_netif_create_default_wifi_sta();
}

static esp_err_t wifi_start_ap(bool include_sta)
{
	wifi_init_common();
	wifi_ensure_netifs(true, include_sta);

	wifi_config_t ap_config = {0};
	strncpy((char *)ap_config.ap.ssid, AP_SSID, sizeof(ap_config.ap.ssid));
	ap_config.ap.ssid_len = (uint8_t)strlen(AP_SSID);
	ap_config.ap.channel = 1;
	ap_config.ap.max_connection = 2;
	ap_config.ap.authmode = (strlen(AP_PASS) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
	strncpy((char *)ap_config.ap.password, AP_PASS, sizeof(ap_config.ap.password));

	(void)esp_wifi_stop();
	ESP_ERROR_CHECK(esp_wifi_set_mode(include_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_LOGI(TAG, "SoftAP started: SSID=%s", AP_SSID);
	return ESP_OK;
}

static esp_err_t wifi_start_sta_with_creds(const char *ssid, const char *pass)
{
	if (!ssid || strlen(ssid) == 0)
		return ESP_ERR_INVALID_ARG;
	wifi_init_common();
	wifi_ensure_netifs(false, true);

	ESP_LOGI(TAG, "WiFi STA connect attempt: ssid='%s'", ssid);
	log_wifi_password("WiFi STA connect attempt", pass ? pass : "");

	wifi_config_t sta_config = {0};
	strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
	strncpy((char *)sta_config.sta.password, pass ? pass : "", sizeof(sta_config.sta.password));
	// Allow connecting to both OPEN and secured networks; if password is wrong, disconnect reason
	// will show it and provisioning can be re-run.
	sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
	sta_config.sta.pmf_cfg = (wifi_pmf_config_t){.capable = true, .required = false};

	(void)esp_wifi_stop();
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_ERROR_CHECK(esp_wifi_connect());
	ESP_LOGI(TAG, "WiFi STA connecting: SSID=%s", ssid);
	return ESP_OK;
}

static bool url_decode_inplace(char *s)
{
	if (!s)
		return false;
	char *src = s;
	char *dst = s;
	while (*src)
	{
		if (*src == '+')
		{
			*dst++ = ' ';
			src++;
			continue;
		}
		if (*src == '%' && src[1] && src[2])
		{
			char hex[3] = {src[1], src[2], 0};
			char *end = NULL;
			long v = strtol(hex, &end, 16);
			if (end != hex + 2)
				return false;
			*dst++ = (char)v;
			src += 3;
			continue;
		}
		*dst++ = *src++;
	}
	*dst = '\0';
	return true;
}

static bool form_get_value(char *body, const char *key, char *out, size_t out_len)
{
	if (!body || !key || !out || out_len == 0)
		return false;
	out[0] = '\0';

	const size_t key_len = strlen(key);
	char *p = body;
	while (p && *p)
	{
		char *amp = strchr(p, '&');
		if (amp)
			*amp = '\0';
		char *eq = strchr(p, '=');
		if (eq)
		{
			*eq = '\0';
			const char *k = p;
			char *v = eq + 1;
			if (strlen(k) == key_len && memcmp(k, key, key_len) == 0)
			{
				strncpy(out, v, out_len - 1);
				out[out_len - 1] = '\0';
				url_decode_inplace(out);
				*eq = '=';
				if (amp)
					*amp = '&';
				return true;
			}
			*eq = '=';
		}
		if (!amp)
			break;
		*amp = '&';
		p = amp + 1;
	}
	return false;
}

static esp_err_t provision_index_handler(httpd_req_t *req)
{
	static const char html[] =
		"<!doctype html><html><head><meta charset='utf-8'/>"
		"<meta name='viewport' content='width=device-width,initial-scale=1'/>"
		"<title>ESP32 Wi-Fi setup</title>"
		"<style>body{font-family:sans-serif;max-width:720px;margin:24px auto;padding:0 12px}"
		"button,input,select{font-size:16px;padding:10px} .row{margin:12px 0}"
		"small{color:#555} pre{background:#f3f3f3;padding:12px;overflow:auto}</style>"
		"</head><body>"
		"<h2>Wi-Fi setup</h2>"
		"<div class='row'><button onclick='scan()'>Scan networks</button> "
		"<small id='status'></small></div>"
		"<div class='row'><label>SSID<br/><select id='ssid'></select></label></div>"
		"<div class='row'><label>Password<br/><input id='pass' type='password' "
		"placeholder='(empty for open network)'/></label></div>"
		"<div class='row'><button onclick='save()'>Save and reboot</button> "
		"<button onclick='forget()'>Forget saved</button></div>"
		"<pre id='log'></pre>"
		"<script>"
		"async function scan(){"
		"document.getElementById('status').textContent='scanning...';"
		"const r=await fetch('/api/scan'); const j=await r.json();"
		"const s=document.getElementById('ssid'); s.innerHTML='';"
		"j.aps.forEach(ap=>{const o=document.createElement('option');"
		"o.value=ap.ssid; o.textContent=`${ap.ssid} (RSSI ${ap.rssi})`; s.appendChild(o);});"
		"document.getElementById('status').textContent=`found ${j.aps.length}`;"
		"document.getElementById('log').textContent=JSON.stringify(j,null,2);"
		"}"
		"async function save(){"
		"const ssid=document.getElementById('ssid').value;"
		"const pass=document.getElementById('pass').value;"
		"const body=new URLSearchParams({ssid,pass}).toString();"
		"const r=await fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
		"document.getElementById('log').textContent=await r.text();"
		"}"
		"async function forget(){"
		"const r=await fetch('/api/forget',{method:'POST'});"
		"document.getElementById('log').textContent=await r.text();"
		"}"
		"scan();"
		"</script></body></html>";

	httpd_resp_set_type(req, "text/html");
	return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t provision_scan_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "Provisioning /api/scan");
	wifi_scan_config_t scan_cfg = {
		.ssid = 0,
		.bssid = 0,
		.channel = 0,
		.show_hidden = false,
		.scan_type = WIFI_SCAN_TYPE_ACTIVE,
	};

	esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
	if (err != ESP_OK)
	{
		httpd_resp_set_status(req, "500");
		return httpd_resp_send(req, "scan_start_failed", HTTPD_RESP_USE_STRLEN);
	}

	uint16_t ap_count = 0;
	ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
	if (ap_count > 20)
		ap_count = 20;

	wifi_ap_record_t *aps = NULL;
	if (ap_count > 0)
	{
		aps = (wifi_ap_record_t *)calloc(ap_count, sizeof(wifi_ap_record_t));
		if (!aps)
		{
			httpd_resp_set_status(req, "500");
			return httpd_resp_send(req, "no_mem", HTTPD_RESP_USE_STRLEN);
		}
		ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, aps));
	}

	char *json = (char *)malloc(2048);
	if (!json)
	{
		free(aps);
		httpd_resp_set_status(req, "500");
		return httpd_resp_send(req, "no_mem", HTTPD_RESP_USE_STRLEN);
	}
	size_t off = 0;
	off += snprintf(json + off, 2048 - off, "{\"aps\":[");
	for (uint16_t i = 0; i < ap_count; i++)
	{
		const char *comma = (i == 0) ? "" : ",";
		off += snprintf(json + off, 2048 - off, "%s{\"ssid\":\"%s\",\"rssi\":%d}", comma,
						(const char *)aps[i].ssid, aps[i].rssi);
		if (off >= 2040)
			break;
	}
	off += snprintf(json + off, 2048 - off, "]}");

	free(aps);
	httpd_resp_set_type(req, "application/json");
	esp_err_t resp_err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
	free(json);
	return resp_err;
}

static esp_err_t provision_save_handler(httpd_req_t *req)
{
	const int total_len = req->content_len;
	ESP_LOGI(TAG, "Provisioning /api/save (content_len=%d)", total_len);
	if (total_len <= 0 || total_len > 1024)
	{
		httpd_resp_set_status(req, "400");
		return httpd_resp_send(req, "bad_request", HTTPD_RESP_USE_STRLEN);
	}

	char *body = (char *)calloc(1, total_len + 1);
	if (!body)
	{
		httpd_resp_set_status(req, "500");
		return httpd_resp_send(req, "no_mem", HTTPD_RESP_USE_STRLEN);
	}

	int cur_len = 0;
	while (cur_len < total_len)
	{
		int r = httpd_req_recv(req, body + cur_len, total_len - cur_len);
		if (r <= 0)
		{
			free(body);
			httpd_resp_set_status(req, "500");
			return httpd_resp_send(req, "recv_failed", HTTPD_RESP_USE_STRLEN);
		}
		cur_len += r;
	}

	char ssid[33] = {0};
	char pass[65] = {0};
	(void)form_get_value(body, "ssid", ssid, sizeof(ssid));
	(void)form_get_value(body, "pass", pass, sizeof(pass));
	free(body);

	if (strlen(ssid) == 0)
	{
		httpd_resp_set_status(req, "400");
		return httpd_resp_send(req, "ssid_required", HTTPD_RESP_USE_STRLEN);
	}

	ESP_LOGI(TAG, "Provisioning save request: ssid='%s', pass_len=%u", ssid, (unsigned)strlen(pass));
	log_wifi_password("Provisioning save request", pass);
	esp_err_t err = nvs_save_wifi_creds(ssid, pass);
	if (err != ESP_OK)
	{
		httpd_resp_set_status(req, "500");
		return httpd_resp_send(req, "save_failed", HTTPD_RESP_USE_STRLEN);
	}

	httpd_resp_set_type(req, "text/plain");
	httpd_resp_send(req, "saved. rebooting...", HTTPD_RESP_USE_STRLEN);
	schedule_restart_ms(800);
	return ESP_OK;
}

static esp_err_t provision_forget_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "Provisioning forget request");
	nvs_clear_wifi_creds();
	httpd_resp_set_type(req, "text/plain");
	httpd_resp_send(req, "cleared. rebooting...", HTTPD_RESP_USE_STRLEN);
	schedule_restart_ms(800);
	return ESP_OK;
}

static esp_err_t start_provision_server(void)
{
	if (provisionServer)
		return ESP_OK;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.server_port = 80;
	config.ctrl_port = 32769;
	config.max_uri_handlers = 16;

	ESP_LOGI(TAG, "Starting provision HTTP server on port %u", (unsigned)config.server_port);
	esp_err_t err = httpd_start(&provisionServer, &config);
	if (err != ESP_OK)
		return err;

	mdns_advertise_provision_http();

	httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = provision_index_handler};
	httpd_uri_t scan_uri = {.uri = "/api/scan", .method = HTTP_GET, .handler = provision_scan_handler};
	httpd_uri_t save_uri = {.uri = "/api/save", .method = HTTP_POST, .handler = provision_save_handler};
	httpd_uri_t forget_uri = {.uri = "/api/forget", .method = HTTP_POST, .handler = provision_forget_handler};

	ESP_ERROR_CHECK(httpd_register_uri_handler(provisionServer, &index_uri));
	ESP_ERROR_CHECK(httpd_register_uri_handler(provisionServer, &scan_uri));
	ESP_ERROR_CHECK(httpd_register_uri_handler(provisionServer, &save_uri));
	ESP_ERROR_CHECK(httpd_register_uri_handler(provisionServer, &forget_uri));

	// Common captive-portal probes (serve the same setup page).
	httpd_uri_t captive_204 = {.uri = "/generate_204", .method = HTTP_GET, .handler = provision_index_handler};
	httpd_uri_t captive_gen = {.uri = "/gen_204", .method = HTTP_GET, .handler = provision_index_handler};
	httpd_uri_t captive_hotspot = {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = provision_index_handler};
	httpd_uri_t captive_success = {.uri = "/library/test/success.html", .method = HTTP_GET, .handler = provision_index_handler};
	httpd_uri_t captive_fwlink = {.uri = "/fwlink", .method = HTTP_GET, .handler = provision_index_handler};

	(void)httpd_register_uri_handler(provisionServer, &captive_204);
	(void)httpd_register_uri_handler(provisionServer, &captive_gen);
	(void)httpd_register_uri_handler(provisionServer, &captive_hotspot);
	(void)httpd_register_uri_handler(provisionServer, &captive_success);
	(void)httpd_register_uri_handler(provisionServer, &captive_fwlink);

	return ESP_OK;
}

static esp_err_t ws_root_handler(httpd_req_t *req)
{
	if (req->method == HTTP_GET)
	{
		ESP_LOGI(TAG, "WS handshake done (fd=%d)", httpd_req_to_sockfd(req));
		return ESP_OK;
	}

	httpd_ws_frame_t frame = {0};
	esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
	if (err != ESP_OK)
	{
		ESP_LOGW(TAG, "WS recv header failed (fd=%d): %s", httpd_req_to_sockfd(req),
				 esp_err_to_name(err));
		httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
		return err;
	}

	if (frame.len == 0)
		return ESP_OK;

	uint8_t *payload = (uint8_t *)malloc(frame.len);
	if (!payload)
		return ESP_ERR_NO_MEM;
	frame.payload = payload;
	err = httpd_ws_recv_frame(req, &frame, frame.len);
	if (err != ESP_OK)
	{
		ESP_LOGW(TAG, "WS recv payload failed (fd=%d): %s", httpd_req_to_sockfd(req),
				 esp_err_to_name(err));
		free(payload);
		httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
		return err;
	}

	if (frame.type == HTTPD_WS_TYPE_BINARY && frame.len == RC_CONTROL_LEN)
	{
		if (memcmp(controlState, payload, RC_CONTROL_LEN) != 0)
		{
			memcpy(controlState, payload, RC_CONTROL_LEN);
			ESP_LOGI(TAG, "[RC State] UP:%u DOWN:%u LEFT:%u RIGHT:%u STOP:%u STEER:%u", controlState[0],
					 controlState[1], controlState[2], controlState[3], controlState[4], controlState[5]);
		}
	}
	else if (frame.type == HTTPD_WS_TYPE_TEXT)
	{
		ESP_LOGI(TAG, "[WS Text] %.*s", (int)frame.len, (const char *)payload);
	}

	free(payload);

	return ESP_OK;
}

static esp_err_t start_http_ws_server(void)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.server_port = RC_WS_PORT;
	config.ctrl_port = RC_WS_PORT + 1;

	ESP_LOGI(TAG, "Starting HTTPD/WS on port %u", (unsigned)config.server_port);
	esp_err_t err = httpd_start(&httpServer, &config);
	if (err != ESP_OK)
		return err;

	httpd_uri_t ws_uri = {
		.uri = "/",
		.method = HTTP_GET,
		.handler = ws_root_handler,
		.user_ctx = NULL,
		.is_websocket = true,
	};
	ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &ws_uri));
	mdns_advertise_rc_ws();
	return ESP_OK;
}

static void ws_broadcast_binary_sync(httpd_handle_t server, const uint8_t *data, size_t len)
{
	if (!server || !data || len == 0)
		return;

	int client_fds[8];
	size_t client_fd_count = sizeof(client_fds) / sizeof(client_fds[0]);
	if (httpd_get_client_list(server, &client_fd_count, client_fds) != ESP_OK)
		return;

	httpd_ws_frame_t frame = {
		.final = true,
		.fragmented = false,
		.type = HTTPD_WS_TYPE_BINARY,
		.payload = (uint8_t *)data,
		.len = len,
	};

	for (size_t i = 0; i < client_fd_count; i++)
	{
		const int fd = client_fds[i];
		if (httpd_ws_get_fd_info(server, fd) != HTTPD_WS_CLIENT_WEBSOCKET)
			continue;
		(void)httpd_ws_send_data(server, fd, &frame);
	}
}

static void ws_broadcast_raw_sync(httpd_handle_t server, uint8_t raw_format, uint16_t width, uint16_t height,
								  const uint8_t *payload, size_t payload_len)
{
	if (!server || !payload || payload_len == 0 || width == 0 || height == 0)
		return;

	uint8_t header[14] = {0};
	// magic "RAWH" + version(1) + format(1) + w(u16 LE) + h(u16 LE) + len(u32 LE)
	header[0] = 'R';
	header[1] = 'A';
	header[2] = 'W';
	header[3] = 'H';
	header[4] = 1; // version
	header[5] = raw_format; // 0 = RGB565, 1 = GRAY8
	header[6] = (uint8_t)(width & 0xFF);
	header[7] = (uint8_t)((width >> 8) & 0xFF);
	header[8] = (uint8_t)(height & 0xFF);
	header[9] = (uint8_t)((height >> 8) & 0xFF);
	header[10] = (uint8_t)(payload_len & 0xFF);
	header[11] = (uint8_t)((payload_len >> 8) & 0xFF);
	header[12] = (uint8_t)((payload_len >> 16) & 0xFF);
	header[13] = (uint8_t)((payload_len >> 24) & 0xFF);

	ws_broadcast_binary_sync(server, header, sizeof(header));
	ws_broadcast_binary_sync(server, payload, payload_len);
}

static void ws_broadcast_raw_rgb565_from_fb(httpd_handle_t server, const camera_fb_t *fb)
{
	if (!server || !fb || !fb->buf || fb->len == 0)
		return;
	if (fb->format != PIXFORMAT_RGB565)
		return;

	const size_t bytes_per_pixel = 2;
	const size_t row_bytes = fb->width * bytes_per_pixel;
	const uint16_t effective_height =
		(uint16_t)((row_bytes > 0) ? (fb->len / row_bytes) : (size_t)fb->height);
	const size_t effective_len = row_bytes * (size_t)effective_height;
	if (effective_height == 0 || effective_len == 0)
		return;

	ws_broadcast_raw_sync(server, 0, (uint16_t)fb->width, effective_height, fb->buf, effective_len);
}

static void ws_broadcast_raw_gray8_from_fb(httpd_handle_t server, const camera_fb_t *fb)
{
	if (!server || !fb || !fb->buf || fb->len == 0)
		return;
	if (fb->format != PIXFORMAT_RGB565)
		return;

	const size_t bytes_per_pixel = 2;
	const size_t row_bytes = fb->width * bytes_per_pixel;
	const uint16_t effective_height =
		(uint16_t)((row_bytes > 0) ? (fb->len / row_bytes) : (size_t)fb->height);
	const size_t pixel_count = (size_t)fb->width * (size_t)effective_height;
	const size_t effective_len = row_bytes * (size_t)effective_height;
	if (effective_height == 0 || effective_len == 0 || pixel_count == 0)
		return;

	uint8_t *gray = (uint8_t *)malloc(pixel_count);
	if (!gray)
		return;

	// RGB565 is typically MSB-first in ESP32 camera buffers.
	for (size_t i = 0, p = 0; i + 1 < effective_len && p < pixel_count; i += 2, p++)
	{
		const uint16_t pix = ((uint16_t)fb->buf[i] << 8) | (uint16_t)fb->buf[i + 1];
		const uint8_t r5 = (uint8_t)((pix >> 11) & 0x1F);
		const uint8_t g6 = (uint8_t)((pix >> 5) & 0x3F);
		const uint8_t b5 = (uint8_t)(pix & 0x1F);
		const uint8_t r8 = (uint8_t)((r5 << 3) | (r5 >> 2));
		const uint8_t g8 = (uint8_t)((g6 << 2) | (g6 >> 4));
		const uint8_t b8 = (uint8_t)((b5 << 3) | (b5 >> 2));
		const uint16_t y = (uint16_t)r8 * 77 + (uint16_t)g8 * 150 + (uint16_t)b8 * 29;
		gray[p] = (uint8_t)(y >> 8);
	}

	ws_broadcast_raw_sync(server, 1, (uint16_t)fb->width, effective_height, gray, pixel_count);
	free(gray);
}

static bool ws_has_clients(httpd_handle_t server)
{
	if (!server)
		return false;

	int client_fds[8];
	size_t client_fd_count = sizeof(client_fds) / sizeof(client_fds[0]);
	if (httpd_get_client_list(server, &client_fd_count, client_fds) != ESP_OK)
		return false;

	for (size_t i = 0; i < client_fd_count; i++)
	{
		const int fd = client_fds[i];
		if (httpd_ws_get_fd_info(server, fd) == HTTPD_WS_CLIENT_WEBSOCKET)
			return true;
	}

	return false;
}

static void camera_stream_task(void *arg)
{
	(void)arg;

	int retries_left = CAM_INIT_MAX_RETRIES;
	while (retries_left != 0)
	{
		const esp_err_t cam_err = init_camera();
		if (cam_err == ESP_OK)
		{
			camera_ok = true;
			ESP_LOGI(TAG, "Camera ready");
			break;
		}

		camera_ok = false;
		ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(cam_err));

		if (retries_left > 0)
			retries_left--;
		if (retries_left == 0)
			break;

		vTaskDelay(pdMS_TO_TICKS(CAM_INIT_RETRY_DELAY_MS));
	}

	if (!camera_ok)
	{
		ESP_LOGW(TAG, "Camera task stopped (camera not available)");
		vTaskDelete(NULL);
		return;
	}

	while (true)
	{
		const httpd_handle_t server = httpServer;
		if (server && ws_has_clients(server))
		{
			camera_fb_t *fb = esp_camera_fb_get();
			if (fb)
			{
#if (CAM_STREAM_MODE == CAM_STREAM_MODE_RGB565_RAW)
				ws_broadcast_raw_rgb565_from_fb(server, fb);
#elif (CAM_STREAM_MODE == CAM_STREAM_MODE_GRAY8)
				ws_broadcast_raw_gray8_from_fb(server, fb);
#else
				if (fb->format == PIXFORMAT_JPEG)
				{
					ws_broadcast_binary_sync(server, fb->buf, fb->len);
				}
				else
				{
					const size_t bytes_per_pixel = (fb->format == PIXFORMAT_RGB565) ? 2 : 0;
					const size_t row_bytes = fb->width * bytes_per_pixel;
					const size_t effective_height =
						(row_bytes > 0) ? (fb->len / row_bytes) : (size_t)fb->height;
					const size_t effective_len = row_bytes * effective_height;

					uint8_t *jpg_buf = NULL;
					size_t jpg_len = 0;
					const bool ok = (bytes_per_pixel > 0) &&
									fmt2jpg(fb->buf, effective_len, fb->width, effective_height, fb->format,
											CAM_SW_JPEG_QUALITY, &jpg_buf, &jpg_len);
					if (ok && jpg_buf && jpg_len > 0)
					{
						ws_broadcast_binary_sync(server, jpg_buf, jpg_len);
						free(jpg_buf);
					}
					else
					{
						if (jpg_buf)
							free(jpg_buf);
						ws_broadcast_raw_rgb565_from_fb(server, fb);
					}
				}
#endif
				esp_camera_fb_return(fb);
			}
			vTaskDelay(pdMS_TO_TICKS(frame_interval_ms()));
			continue;
		}

		vTaskDelay(pdMS_TO_TICKS(CAM_STREAM_IDLE_DELAY_MS));
	}
}

void app_main(void)
{
	ESP_ERROR_CHECK(nvs_flash_init());

	wifi_event_group = xEventGroupCreate();

	char ssid[33] = {0};
	char pass[65] = {0};
	bool have_saved = (nvs_load_wifi_creds(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK) &&
					  (strlen(ssid) > 0);

	const char *ssid_use = NULL;
	const char *pass_use = NULL;
	if (have_saved)
	{
		ssid_use = ssid;
		pass_use = pass;
		ESP_LOGI(TAG, "Using Wi-Fi credentials from NVS (SSID=%s)", ssid_use);
	}
	else if (strlen(WIFI_SSID) > 0)
	{
		ssid_use = WIFI_SSID;
		pass_use = WIFI_PASS;
		ESP_LOGI(TAG, "Using Wi-Fi credentials from rc_config.h (SSID=%s)", ssid_use);
	}

	if (ssid_use)
	{
		sta_retry_count = 0;
		if (wifi_event_group)
			xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

		ESP_ERROR_CHECK(wifi_start_sta_with_creds(ssid_use, pass_use));
		EventBits_t bits =
			xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE,
								pdMS_TO_TICKS(WIFI_STA_CONNECT_TIMEOUT_MS));
		if (bits & WIFI_CONNECTED_BIT)
		{
			ESP_LOGI(TAG, "WiFi connected (STA)");
		}
		else
		{
			ESP_LOGW(TAG, "WiFi connect failed, starting provisioning AP...");
			ESP_ERROR_CHECK(wifi_start_ap(true));
			ESP_ERROR_CHECK(start_provision_server());
			ESP_LOGI(TAG, "Open http://192.168.4.1/ to configure Wi-Fi");
		}
	}
	else
	{
		ESP_LOGI(TAG, "No saved Wi-Fi credentials, starting provisioning AP...");
		ESP_ERROR_CHECK(wifi_start_ap(true));
		ESP_ERROR_CHECK(start_provision_server());
		ESP_LOGI(TAG, "Open http://192.168.4.1/ to configure Wi-Fi");
	}

	ESP_ERROR_CHECK(start_http_ws_server());
	xTaskCreate(camera_stream_task, "camera_task", 6144, NULL, 5, NULL);
}
