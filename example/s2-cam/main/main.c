#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "cam.h"
#include "ov2640.h"
#include "ov3660.h"
#include "sensor.h"
#include "sccb.h"
#include "jpeg.h"
#include "lvgl.h"
#include "gui.h"


#include "app_httpserver.h"
#include "app_wifi_smartconfig.h"


static const char *TAG = "main";

static int camera_init(cam_config_t *config_s)
{
	if(NULL == config_s)
	{
		ESP_LOGE(TAG, "No valid parameters");
		return -1;
	}
	
	sensor_t sensor;
    // 使用PingPang buffer，帧率更高， 也可以单独使用一个buffer节省内存
    config_s->frame1_buffer = (uint8_t *)heap_caps_malloc(config_s->size.width * config_s->size.high * 2 * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    config_s->frame2_buffer = (uint8_t *)heap_caps_malloc(config_s->size.width * config_s->size.high * 2 * sizeof(uint8_t), MALLOC_CAP_SPIRAM);

	if(ESP_OK != cam_init(config_s))
		goto fail;
    SCCB_Init(config_s->I2C.sscb_sda, config_s->I2C.sscb_scl);
	
    sensor.slv_addr = SCCB_Probe();
    ESP_LOGI(TAG, "sensor_id: 0x%x\n", sensor.slv_addr);
	
    if (sensor.slv_addr == 0x30) { // OV2640
        if (OV2640_Init(0, 1) != 0) {
            goto fail;
    	}
		OV2640_JPEG_Mode();
//        OV2640_RGB565_Mode(false);	//RGB565模式
        OV2640_ImageSize_Set(GUI_CAM_WIDTH, GUI_CAM_HIGH);
        OV2640_ImageWin_Set(0, 0, GUI_CAM_WIDTH, GUI_CAM_HIGH);
        OV2640_OutSize_Set(config_s->size.width, config_s->size.high); 
    } else if (sensor.slv_addr == 0x3C) { // OV3660
        ov3660_init(&sensor);
        sensor.init_status(&sensor);
        if (sensor.reset(&sensor) != 0) {
            goto fail;
        }
        sensor.set_pixformat(&sensor, PIXFORMAT_RGB565);
        // totalX 变小，帧率提高
        // totalY 变小，帧率提高vsync 变短
        sensor.set_res_raw(&sensor, 0, 0, 2079, 1547, 8, 2, 1920, 800, config_s->size.width, config_s->size.high, true, true);
        sensor.set_vflip(&sensor, 1);
        sensor.set_hmirror(&sensor, 1);
        sensor.set_pll(&sensor, false, 10, 1, 0, false, 0, true, 5); // 13 fps
    } else {
        ESP_LOGE(TAG, "sensor is temporarily not supported\n");
        goto fail;
    }

    ESP_LOGI(TAG, "camera init done\n");
	return 0;
fail:
    free(config_s->frame1_buffer);
    free(config_s->frame2_buffer);
    cam_deinit();
	ESP_LOGE(TAG, "camera init fail\n");
	return -1;
}
static void app_camera_init(void)
{
    cam_config_t config = {
        .bit_width = 8,
        .mode.jpeg = true,
        .xclk_fre = 10 * 1000 * 1000,
        .pin = {
            .xclk  = CAM_XCLK,
            .pclk  = CAM_PCLK,
            .vsync = CAM_VSYNC,
            .hsync = CAM_HSYNC,
        },
        .pin_data = {CAM_D0, 
        			 CAM_D1,
        			 CAM_D2,
        			 CAM_D3,
        			 CAM_D4,
        			 CAM_D5,
        			 CAM_D6,
        			 CAM_D7},
        .vsync_invert = true,
        .hsync_invert = false,
        .size = {
            .width = CAM_WIDTH,
            .high  = CAM_HIGH,
        },
        .I2C = {
			.sscb_sda = CAM_SDA,
			.sscb_scl = CAM_SCL,
		},
        .max_buffer_size = 4 * 1024,
        .task_stack = 1024,
        .task_pri = configMAX_PRIORITIES
    };
   while(ESP_OK != camera_init(&config))
    {
    	vTaskDelay(2000 / portTICK_RATE_MS);
    }
}

extern SemaphoreHandle_t smart_w;
void app_main()
{
	_app_wifi_init();
	ESP_LOGI(TAG, "wifi init");
    if (xSemaphoreTake(smart_w, portMAX_DELAY) == pdTRUE){
		vSemaphoreDelete(smart_w);
	}
	app_camera_init();
	app_httpserver_init();
}
