/**
 * @file wifi_ap.cpp
 * @brief WiFi AP+STA mode — camera nodes connect to our AP,
 *        and we connect to the home router for internet (weather).
 */
#include "wifi_ap.h"
#include "definitions.h"
#include "wifi_secrets.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include <string.h>

static bool s_ap_started    = false;
static bool s_sta_connected = false;
static int  s_sta_count     = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "Station connected: " MACSTR, MAC2STR(e->mac));
            s_sta_count++;
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "Station disconnected: " MACSTR, MAC2STR(e->mac));
            if (s_sta_count > 0) s_sta_count--;
            break;
        }
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting to %s...", STA_SSID);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "STA disconnected from %s, reconnecting...", STA_SSID);
            s_sta_connected = false;
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_wifi_connect();
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&e->ip_info.ip));
        s_sta_connected = true;
    }
}

void wifi_ap_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create both AP and STA netifs */
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    /* Set static IP for AP */
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_ip_info_t ip_info = {};
    esp_netif_str_to_ip4(AP_IP, &ip_info.ip);
    esp_netif_str_to_ip4("255.255.255.0", &ip_info.netmask);
    esp_netif_str_to_ip4(AP_IP, &ip_info.gw);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    /* Configure AP */
    wifi_config_t ap_cfg = {};
    strncpy((char *)ap_cfg.ap.ssid, AP_SSID_WS, sizeof(ap_cfg.ap.ssid));
    strncpy((char *)ap_cfg.ap.password, AP_PASS_WS, sizeof(ap_cfg.ap.password));
    ap_cfg.ap.ssid_len       = strlen(AP_SSID_WS);
    ap_cfg.ap.channel        = AP_CHANNEL_WS;
    ap_cfg.ap.max_connection = AP_MAX_CONN;
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;

    /* Configure STA */
    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid, STA_SSID, sizeof(sta_cfg.sta.ssid));
    strncpy((char *)sta_cfg.sta.password, STA_PASSWORD, sizeof(sta_cfg.sta.password));
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    /* APSTA mode — serve camera nodes AND connect to router */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_started = true;
    ESP_LOGI(TAG, "WiFi APSTA started — AP SSID: %s IP: %s | STA: %s",
             AP_SSID_WS, AP_IP, STA_SSID);
}

bool wifi_ap_is_started(void)   { return s_ap_started; }
bool wifi_sta_is_connected(void) { return s_sta_connected; }
int  wifi_ap_get_sta_count(void) { return s_sta_count; }
