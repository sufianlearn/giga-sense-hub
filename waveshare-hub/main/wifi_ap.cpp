/**
 * @file wifi_ap.cpp
 * @brief WiFi SoftAP mode — camera nodes connect to us.
 */
#include "wifi_ap.h"
#include "definitions.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include <string.h>

static bool s_ap_started = false;
static int  s_sta_count  = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station connected: " MACSTR, MAC2STR(e->mac));
        s_sta_count++;
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station disconnected: " MACSTR, MAC2STR(e->mac));
        if (s_sta_count > 0) s_sta_count--;
    }
}

void wifi_ap_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_ap();

    /* Set static IP for AP */
    esp_netif_dhcps_stop(netif);
    esp_netif_ip_info_t ip_info = {};
    esp_netif_str_to_ip4(AP_IP, &ip_info.ip);
    esp_netif_str_to_ip4("255.255.255.0", &ip_info.netmask);
    esp_netif_str_to_ip4(AP_IP, &ip_info.gw);
    esp_netif_set_ip_info(netif, &ip_info);
    esp_netif_dhcps_start(netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {};
    strncpy((char *)wifi_cfg.ap.ssid, AP_SSID_WS, sizeof(wifi_cfg.ap.ssid));
    strncpy((char *)wifi_cfg.ap.password, AP_PASS_WS, sizeof(wifi_cfg.ap.password));
    wifi_cfg.ap.ssid_len       = strlen(AP_SSID_WS);
    wifi_cfg.ap.channel        = AP_CHANNEL_WS;
    wifi_cfg.ap.max_connection = AP_MAX_CONN;
    wifi_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_started = true;
    ESP_LOGI(TAG, "WiFi AP started — SSID: %s IP: %s", AP_SSID_WS, AP_IP);
}

bool wifi_ap_is_started(void) { return s_ap_started; }
int  wifi_ap_get_sta_count(void) { return s_sta_count; }
