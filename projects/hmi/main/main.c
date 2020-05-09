#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lcd.h"
#include "cam.h"
#include "ov2640.h"
#include "sccb.h"
#include "jpeg.h"
#include "lvgl.h"
#include "gui.h"
#include "esp_lua.h"

static const char *TAG = "main";

#define JPEG_MODE 0

#define CAM_WIDTH   (GUI_CAM_WIDTH)
#define CAM_HIGH    (GUI_CAM_HIGH)

#define LCD_CLK   GPIO_NUM_15
#define LCD_MOSI  GPIO_NUM_9
#define LCD_DC    GPIO_NUM_13
#define LCD_RST   GPIO_NUM_16
#define LCD_CS    GPIO_NUM_11
#define LCD_BK    GPIO_NUM_6

#define CAM_XCLK  GPIO_NUM_1
#define CAM_PCLK  GPIO_NUM_0
#define CAM_VSYNC GPIO_NUM_2
#define CAM_HSYNC GPIO_NUM_3

#define CAM_D0    GPIO_NUM_46
#define CAM_D1    GPIO_NUM_45
#define CAM_D2    GPIO_NUM_41
#define CAM_D3    GPIO_NUM_42
#define CAM_D4    GPIO_NUM_39
#define CAM_D5    GPIO_NUM_40
#define CAM_D6    GPIO_NUM_21
#define CAM_D7    GPIO_NUM_38

#define CAM_SCL   GPIO_NUM_7
#define CAM_SDA   GPIO_NUM_8

static lv_disp_t *disp[1];
// static lv_indev_t *indev[1];

static void IRAM_ATTR lv_disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    uint32_t len = (sizeof(uint16_t) * ((area->y2 - area->y1 + 1)*(area->x2 - area->x1 + 1)));

    lcd_set_index(area->x1, area->y1, area->x2, area->y2);
    lcd_write_data((uint8_t *)color_p, len);

    lv_disp_flush_ready(disp_drv);
}

static void lv_memory_monitor(lv_task_t * param)
{
    (void) param; /*Unused*/

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "used: %6d (%3d %%), frag: %3d %%, biggest free: %6d, system free: %d/%d\n", (int)mon.total_size - mon.free_size,
           mon.used_pct,
           mon.frag_pct,
           (int)mon.free_biggest_size,
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL), esp_get_free_heap_size());
}

static void lv_tick_task(void * arg)
{
    while(1) {
        lv_tick_inc(10);
        vTaskDelay(10 / portTICK_RATE_MS);
    }
}

static void gui_task(void *arg)
{
    lcd_config_t lcd_config = {
        .clk_fre = 80 * 1000 * 1000,
        .pin_clk = LCD_CLK,
        .pin_mosi = LCD_MOSI,
        .pin_dc = LCD_DC,
        .pin_cs = LCD_CS,
        .pin_rst = LCD_RST,
        .pin_bk = LCD_BK,
        .max_buffer_size = 2 * 1024,
        .horizontal = 2 // 2: UP, 3： DOWN
    };

    lcd_init(&lcd_config);
    
    xTaskCreate(lv_tick_task, "lv_tick_task", 1024, NULL, 5, NULL);

    lv_init();

    /*Create an other buffer for double buffering*/
    static lv_disp_buf_t disp_buf;
    static lv_color_t *buf = NULL;
    buf = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * (320 * 240), MALLOC_CAP_SPIRAM);
    lv_disp_buf_init(&disp_buf, buf, NULL, (320 * 240));

    /*Create a display*/
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);            /*Basic initialization*/
    disp_drv.buffer = &disp_buf;
    disp_drv.flush_cb = lv_disp_flush;    /*Used when `LV_VDB_SIZE != 0` in lv_conf.h (buffered drawing)*/
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp[0] = lv_disp_drv_register(&disp_drv);

    lv_disp_set_default(disp[0]);

    lv_task_create(lv_memory_monitor, 3000, LV_TASK_PRIO_MID, NULL);

    gui_init(disp, NULL, lv_theme_material_init(0, NULL));

    while(1) {
        lv_task_handler();
        vTaskDelay(10 / portTICK_RATE_MS);
    }
}

#include "esp_lua_lib.h"

static int lcd_write(lua_State *L) 
{
    int ret = -1;
    char *io = luaL_checklstring(L, 1, NULL);

    ret = gui_write(io, luaL_checklstring(L, 2, NULL), 100 / portTICK_RATE_MS);

    if (ret == 0) {
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);
    }
    return 1;
}

static const luaL_Reg lcd_lib[] = {
    {"write", lcd_write},
    {NULL, NULL}
};

LUAMOD_API int esp_lib_lcd(lua_State *L) 
{
    xTaskCreate(gui_task, "gui_task", 4096, NULL, 5, NULL);

    luaL_newlib(L, lcd_lib);
    lua_pushstring(L, "0.1.0");
    lua_setfield(L, -2, "_version");
    return 1;
}

static const luaL_Reg mylibs[] = {
    {"sys", esp_lib_sys},
    {"net", esp_lib_net},
    {"web", esp_lib_web},
    {"httpd", esp_lib_httpd},
    {"ramf", esp_lib_ramf},
    {"lcd", esp_lib_lcd},
    {NULL, NULL}
};

const char LUA_SCRIPT_INIT[] = " \
assert(sys.init()) \
dofile(\'/lua/init.lua\') \
";

void lua_task(void *arg)
{
    const char *ESP_LUA_ARGV[5] = {"./lua", "-i", "-e", LUA_SCRIPT_INIT, NULL}; // enter interactive mode after executing 'script'

    esp_lua_init(NULL, NULL, mylibs);

    while (1) {
        esp_lua_main(4, ESP_LUA_ARGV);
        printf("lua exit\n");
        vTaskDelay(2000 / portTICK_RATE_MS);
    }

    vTaskDelete(NULL);
}

static void cam_task(void *arg)
{
    cam_config_t cam_config = {
        .bit_width = 8,
        .mode.jpeg = JPEG_MODE,
        .xclk_fre = 8 * 1000 * 1000,
        .pin = {
            .xclk  = CAM_XCLK,
            .pclk  = CAM_PCLK,
            .vsync = CAM_VSYNC,
            .hsync = CAM_HSYNC,
        },
        .pin_data = {CAM_D0, CAM_D1, CAM_D2, CAM_D3, CAM_D4, CAM_D5, CAM_D6, CAM_D7},
        .size = {
            .width = CAM_WIDTH,
            .high  = CAM_HIGH,
        },
        .max_buffer_size = 4 * 1024,
        .task_stack = 1024,
        .task_pri = configMAX_PRIORITIES
    };

    // 使用PingPang buffer，帧率更高， 也可以单独使用一个buffer节省内存
    cam_config.frame1_buffer = (uint8_t *)heap_caps_malloc(CAM_WIDTH * CAM_HIGH * 2 * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    cam_config.frame2_buffer = (uint8_t *)heap_caps_malloc(CAM_WIDTH * CAM_HIGH * 2 * sizeof(uint8_t), MALLOC_CAP_SPIRAM);

    cam_init(&cam_config);
    SCCB_Init(CAM_SDA, CAM_SCL);
    uint8_t id = SCCB_Probe();
    ESP_LOGI(TAG, "sensor_id: 0x%x\n", id);
    if (OV2640_Init(0, 0) == 1) {
        vTaskDelete(NULL);
        return;
    }
    if (cam_config.mode.jpeg) {
        OV2640_JPEG_Mode();
    } else {
        OV2640_RGB565_Mode(false);	//RGB565模式
    }
    
    OV2640_ImageSize_Set(800, 600);
    OV2640_ImageWin_Set(0, 0, 800, 600);
  	OV2640_OutSize_Set(CAM_WIDTH, CAM_HIGH); 
    ESP_LOGI(TAG, "camera init done\n");
    cam_start();
    while (1) {
        uint8_t *cam_buf = NULL;
        size_t recv_len = cam_take(&cam_buf);
#if JPEG_MODE
#if DEBUG
        printf("total_len: %d\n", recv_len);
        for (int x = 0; x < 10; x++) {
            ets_printf("%d ", cam_buf[x]);
        }
        ets_printf("\n");
#endif

        int w, h;
        uint8_t *img = jpeg_decode(cam_buf, &w, &h);
        if (img) {
            ESP_LOGI(TAG, "jpeg: w: %d, h: %d\n", w, h);
            gui_set_camera(img, w * h * sizeof(uint16_t), 100 / portTICK_RATE_MS);
            free(img);
        }
#else
        if (recv_len == CAM_WIDTH * CAM_HIGH * 2) {
            gui_set_camera(cam_buf, recv_len, 100 / portTICK_RATE_MS);
        } else {
            printf("len: %d\n", recv_len);
        }
#endif
        cam_give(cam_buf);   
        // 使用逻辑分析仪观察帧率
        gpio_set_level(LCD_BK, 1);
        gpio_set_level(LCD_BK, 0);  
    }
    vTaskDelete(NULL);
}

void app_main()
{
    xTaskCreate(cam_task, "cam_task", 2048, NULL, configMAX_PRIORITIES, NULL);
    esp_log_level_set("*", ESP_LOG_ERROR);
    xTaskCreate(lua_task, "lua_task", 10240, NULL, 5, NULL);
}