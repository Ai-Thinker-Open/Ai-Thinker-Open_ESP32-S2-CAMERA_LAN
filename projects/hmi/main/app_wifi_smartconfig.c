/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "esp_smartconfig.h"
#include "sdkconfig.h"
//#include "soc/gpio_sig_map.h"
//#include "soc/io_mux_reg.h"
#include "driver/gpio.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs.h"


static const char *TAG = "wifi_config";

#define ESP_MAXIMUM_RETRY  10
#define WIFI_SSID_MAX  32
#define WIFI_PASS_MAX  64
#define M_WIFI_CONFIG_L  "m_wifi_config_l"
#define W_CONFIG_FLAG "w_config_flag"
#define C_WIFI_SSID "c_wifi_ssid"
#define C_WIFI_PASS "c_wifi_pass"

typedef struct {
	int8_t w_config_flag;
	char c_wifi_ssid[WIFI_SSID_MAX];
	char c_wifi_pass[WIFI_PASS_MAX];
}wifi_conf_info;


static wifi_conf_info m_wifi_conf = {0};

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t	s_wifi_event_group;
SemaphoreHandle_t 			smart_w;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;

static int s_retry_num = 0;
//static int8_t wifi_config = 0;
//static char   app_wifi_ssid[WIFI_SSID_MAX] = {0};
//static char   app_wifi_pass[WIFI_PASS_MAX] = {0};
static esp_event_handler_instance_t any_id;
static esp_event_handler_instance_t got_ip;
static nvs_handle_t m_nvs_handle;

char ip_addr_buf[32];

static void smartconfig_example_task(void * parm);
static void initialise_wifi(void);
static esp_err_t read_wifi_config(void);
static esp_err_t write_wifi_config(int8_t flag);

static void event_handler(void* arg, esp_event_base_t event_base, 
                                int event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if(m_wifi_conf.w_config_flag == 0) 
			xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
		else
			esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if(m_wifi_conf.w_config_flag == 0)
			xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
		else
			{
            if (s_retry_num < ESP_MAXIMUM_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG,"retry to connect to the AP");
            }
			else
            {
            	ESP_LOGI(TAG,"connect to the AP fail");
				write_wifi_config(0);
				esp_restart();
			}
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ESP_LOGD(TAG, " IP_EVENT_STA_GOT_IP");
        s_retry_num = 0;
        if(m_wifi_conf.w_config_flag == 0) 
		{
			xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
        }
		else
		{
			const ip_event_got_ip_t *event = (const ip_event_got_ip_t *) event_data;
			sprintf(ip_addr_buf,IPSTR,IP2STR(&event->ip_info.ip));
			ESP_LOGD(TAG, "IP sta:%s",ip_addr_buf);
		}
		xSemaphoreGive(smart_w);
    } 
	else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        //ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }
		ESP_LOGI(TAG, "Got SSID and password");
        ESP_LOGI(TAG, "SSID:%s",wifi_config.sta.ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", wifi_config.sta.password);
		sprintf(ip_addr_buf,IPSTR,evt->cellphone_ip[0],
									evt->cellphone_ip[1],
									evt->cellphone_ip[2],
									evt->cellphone_ip[3]);
		ESP_LOGD(TAG, "IP smart:%s",ip_addr_buf);
		m_wifi_conf.w_config_flag = 1;
		memcpy(m_wifi_conf.c_wifi_ssid, wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid));
		memcpy(m_wifi_conf.c_wifi_pass, wifi_config.sta.password, sizeof(wifi_config.sta.password));
        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
        ESP_ERROR_CHECK( esp_wifi_connect() );
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

static void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY); 
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
			write_wifi_config(1);
			vTaskDelay(3*1000/portTICK_PERIOD_MS);
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}
#if 1
static esp_err_t read_wifi_config(void)
{
	ESP_LOGI(TAG, "------------ read_wifi_config ------------");
	esp_err_t err = ESP_FAIL;
	size_t length;
    err = nvs_open(M_WIFI_CONFIG_L, NVS_READWRITE, &m_nvs_handle);
    if (err == ESP_OK) {
		err = nvs_get_i8(m_nvs_handle, W_CONFIG_FLAG, &m_wifi_conf.w_config_flag);
        if(err==ESP_OK){
            //ESP_LOGI(TAG, "flag = %d", m_wifi_conf.w_config_flag);
			length = WIFI_SSID_MAX;
            err = nvs_get_str (m_nvs_handle, C_WIFI_SSID, m_wifi_conf.c_wifi_ssid, &length);
            if(err==ESP_OK) ESP_LOGI(TAG, "ssid = %s", m_wifi_conf.c_wifi_ssid);
			length = WIFI_PASS_MAX;
            err = nvs_get_str (m_nvs_handle, C_WIFI_PASS, m_wifi_conf.c_wifi_pass, &length);
            if(err==ESP_OK) ESP_LOGI(TAG, "pass = %s", m_wifi_conf.c_wifi_pass);
        }

	}
    nvs_close(m_nvs_handle);
	ESP_LOGI(TAG, "------------ read_wifi_config ------------");
	return err;
}
#endif
static esp_err_t write_wifi_config(int8_t flag)
{
	ESP_LOGI(TAG, "------------ write_wifi_config ------------");
	esp_err_t err = ESP_FAIL;
    err = nvs_open(M_WIFI_CONFIG_L, NVS_READWRITE, &m_nvs_handle);
    if (err == ESP_OK && flag == 1) {
		err = nvs_set_i8(m_nvs_handle, W_CONFIG_FLAG, m_wifi_conf.w_config_flag);
        if(err==ESP_OK){
            //ESP_LOGI(TAG, "flag = %d", m_wifi_conf.w_config_flag);
            err = nvs_set_str (m_nvs_handle, C_WIFI_SSID, m_wifi_conf.c_wifi_ssid);
            if(err==ESP_OK) ESP_LOGI(TAG, "ssid = %s", m_wifi_conf.c_wifi_ssid);
            err = nvs_set_str (m_nvs_handle, C_WIFI_PASS, m_wifi_conf.c_wifi_pass);
            if(err==ESP_OK) ESP_LOGI(TAG, "pass = %s", m_wifi_conf.c_wifi_pass);
        }
	}
	else
	{
		ESP_LOGI(TAG, "Remove the configuration");
		err = nvs_set_i8(m_nvs_handle, W_CONFIG_FLAG,flag);
	}
	err = nvs_commit(m_nvs_handle);
	vTaskDelay(1000/portTICK_PERIOD_MS);
    nvs_close(m_nvs_handle);
	ESP_LOGI(TAG, "------------ write_wifi_config ------------");
	return err;
}


static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

	esp_event_handler_instance_t sc_event;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(SC_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        &sc_event,
                                                        NULL));

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );

}

static int wifi_init_sta(void)
{
	if(0 == strlen(m_wifi_conf.c_wifi_ssid))
	{
		ESP_LOGE(TAG, "SSID is NULL");
		m_wifi_conf.w_config_flag=0;
		return -1;
	}
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t any_id;
    esp_event_handler_instance_t got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
	memcpy(wifi_config.sta.ssid, m_wifi_conf.c_wifi_ssid,sizeof(m_wifi_conf.c_wifi_ssid));
	memcpy(wifi_config.sta.password, m_wifi_conf.c_wifi_pass,sizeof(m_wifi_conf.c_wifi_pass));

	ESP_LOGI(TAG, "SSID = %s", wifi_config.sta.ssid);
	ESP_LOGI(TAG, "PASS = %s", wifi_config.sta.password);
	
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_start() );
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
	return ESP_OK;
}

void _app_wifi_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
		ESP_ERROR_CHECK(nvs_flash_init());
    }

	if(ESP_OK != read_wifi_config())
		m_wifi_conf.w_config_flag = 0;
    gpio_config_t conf = { 0 };
    conf.pin_bit_mask = 1LL << GPIO_NUM_18;
    conf.mode = GPIO_MODE_INPUT;
	conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&conf);
	vTaskDelay(100 / portTICK_PERIOD_MS);
	if(gpio_get_level(GPIO_NUM_18)==0)
	{	
		m_wifi_conf.w_config_flag = 0;
		ESP_LOGE(TAG, "smartconfig start");
	}
	smart_w = xSemaphoreCreateBinary();
	if(m_wifi_conf.w_config_flag == 0 || ESP_OK != wifi_init_sta())
	{
		initialise_wifi();
	}

}

