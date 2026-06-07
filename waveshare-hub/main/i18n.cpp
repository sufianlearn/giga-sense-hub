/**
 * @file i18n.cpp
 * @brief English + German string tables for UI localization.
 */
#include "i18n.h"

static lang_t s_lang = LANG_EN;

static const char *s_en[] = {
    [S_SETTINGS]        = "Settings",
    [S_NETWORK]         = "Network",
    [S_CAMERAS]         = "Cameras",
    [S_DISPLAY]         = "Display",
    [S_SECURITY]        = "Security",
    [S_SYSTEM_INFO]     = "System Info",
    [S_ABOUT]           = "About",
    [S_BACK]            = "Back",
    [S_BRIGHTNESS]      = "Brightness",
    [S_DIM_TIMEOUT]     = "Dim Timeout",
    [S_OFF_TIMEOUT]     = "Off Timeout",
    [S_LANGUAGE]        = "Language",
    [S_REBOOT]          = "Reboot",
    [S_OTA_UPDATE]      = "OTA Update",
    [S_FREE_HEAP]       = "Free Heap",
    [S_UPTIME]          = "Uptime",
    [S_WIFI_RSSI]       = "WiFi RSSI",
    [S_FPS]             = "FPS",
    [S_NODES_CONNECTED] = "Nodes Connected",
    [S_AUTHENTICATION]  = "Authentication",
    [S_PIN_LOCK]        = "PIN Lock",
    [S_WEATHER]         = "Weather",
    [S_WIND]            = "Wind",
    [S_LOADING]         = "Loading...",
    [S_ONLINE]          = "Online",
    [S_AP_ONLY]         = "AP Only",
    [S_ACTIVE]          = "Active",
    [S_OFFLINE]         = "Offline",
    [S_CONNECTED]       = "Connected",
    [S_DISCONNECTED]    = "Disconnected",
    [S_NODE]            = "Node",
    [S_MODE_AP]         = "Mode: Access Point",
    [S_CHANNEL]         = "Channel",
    [S_FIRMWARE]        = "Firmware",
    [S_BUILT_WITH]      = "Built with",
    [S_SELECT_NODE]     = "Select Node",
    [S_UPDATE_FW]       = "Update Firmware",
    [S_SECONDS]         = "seconds",
};

static const char *s_de[] = {
    [S_SETTINGS]        = "Einstellungen",
    [S_NETWORK]         = "Netzwerk",
    [S_CAMERAS]         = "Kameras",
    [S_DISPLAY]         = "Anzeige",
    [S_SECURITY]        = "Sicherheit",
    [S_SYSTEM_INFO]     = "Systeminfo",
    [S_ABOUT]           = "\xC3\x9C" "ber",    /* Über */
    [S_BACK]            = "Zur\xC3\xBC" "ck", /* Zurück */
    [S_BRIGHTNESS]      = "Helligkeit",
    [S_DIM_TIMEOUT]     = "Abdunkel-Timer",
    [S_OFF_TIMEOUT]     = "Aus-Timer",
    [S_LANGUAGE]        = "Sprache",
    [S_REBOOT]          = "Neustart",
    [S_OTA_UPDATE]      = "OTA-Update",
    [S_FREE_HEAP]       = "Freier Heap",
    [S_UPTIME]          = "Laufzeit",
    [S_WIFI_RSSI]       = "WLAN-RSSI",
    [S_FPS]             = "FPS",
    [S_NODES_CONNECTED] = "Verbundene Knoten",
    [S_AUTHENTICATION]  = "Authentifizierung",
    [S_PIN_LOCK]        = "PIN-Sperre",
    [S_WEATHER]         = "Wetter",
    [S_WIND]            = "Wind",
    [S_LOADING]         = "Laden...",
    [S_ONLINE]          = "Online",
    [S_AP_ONLY]         = "Nur AP",
    [S_ACTIVE]          = "Aktiv",
    [S_OFFLINE]         = "Offline",
    [S_CONNECTED]       = "Verbunden",
    [S_DISCONNECTED]    = "Getrennt",
    [S_NODE]            = "Knoten",
    [S_MODE_AP]         = "Modus: Access Point",
    [S_CHANNEL]         = "Kanal",
    [S_FIRMWARE]        = "Firmware",
    [S_BUILT_WITH]      = "Erstellt mit",
    [S_SELECT_NODE]     = "Knoten w\xC3\xA4" "hlen", /* wählen */
    [S_UPDATE_FW]       = "Firmware aktualisieren",
    [S_SECONDS]         = "Sekunden",
};

void i18n_set_language(lang_t lang)
{
    s_lang = lang;
}

lang_t i18n_get_language(void)
{
    return s_lang;
}

const char *i18n(int str_id)
{
    if (str_id < 0 || str_id >= S__COUNT) return "???";
    return (s_lang == LANG_DE) ? s_de[str_id] : s_en[str_id];
}
