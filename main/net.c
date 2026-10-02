// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * net.c
 *
 * Network bring-up: Wi-Fi station (dev board) or open_eth (QEMU), then SNTP. See net.h.
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_eth.h"
#include "esp_eth_mac_openeth.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "net.h"

volatile bool net_radio_started;
volatile bool net_entropy_stubbed;
static SemaphoreHandle_t got_ip;

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
    printf("NET got ip " IPSTR "\n", IP2STR(&e->ip_info.ip));
    xSemaphoreGive(got_ip);
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    }
}

static bool start_wifi(const char *ssid, const char *pass)
{
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL);
    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    if (esp_wifi_start() != ESP_OK) {
        return false;
    }
    net_radio_started = true; // as wifi_modem.c: entropy is RADIO once esp_wifi_start() succeeded
    esp_wifi_connect();
    return true;
}

static bool start_openeth(void)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&cfg);
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.autonego_timeout_ms = 100;
    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_config);
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth = NULL;
    if (esp_eth_driver_install(&config, &eth) != ESP_OK) {
        return false;
    }
    esp_netif_attach(netif, esp_eth_new_netif_glue(eth));
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip, NULL);
    net_entropy_stubbed = true;
    return esp_eth_start(eth) == ESP_OK;
}

const char *net_start(void)
{
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    got_ip = xSemaphoreCreateBinary();
    esp_netif_init();
    esp_event_loop_create_default();

    char ssid[33] = "", pass[65] = "";
    nvs_handle_t h;
    if (nvs_open("standalone", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(ssid);
        nvs_get_str(h, "ssid", ssid, &n);
        n = sizeof(pass);
        nvs_get_str(h, "pass", pass, &n);
        nvs_close(h);
    }
    const char *path;
    bool ok;
    if (ssid[0]) {
        path = "wifi";
        printf("NET Wi-Fi station, SSID from NVS\n");
        ok = start_wifi(ssid, pass);
    } else {
        path = "qemu-openeth";
        printf("NET no Wi-Fi credentials in NVS: QEMU open_eth (entropy gate stubbed: no radio)\n");
        ok = start_openeth();
    }
    if (!ok || (xSemaphoreTake(got_ip, pdMS_TO_TICKS(60000)) != pdTRUE)) {
        printf("NET failed\n");
        return NULL;
    }
    return path;
}

bool net_sntp(const char *server, int timeout_ms)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
    esp_netif_sntp_init(&config);
    return esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
}
