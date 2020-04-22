#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "esp_heap_caps.h"
#include "gui.h"
#include "esp_log.h"
#include "esp_lua.h"
#include "esp_lua_lib.h"
#include "lv_lodepng.h"

static const char *TAG = "gui";

typedef struct {
    int event;
    void *arg;
} gui_event_t;

#define GUI_WIFI_EVENT 0
#define GUI_BATTERY_EVENT 1
#define GUI_STATE_EVENT 2
#define GUI_MSG_EVENT 3
#define GUI_QR_EVENT 4
#define GUI_SHARES_EVENT 5
#define GUI_CAMERA_EVENT 6
#define GUI_MEM_EVENT 7
#define GUI_CPU_EVENT 8

#define MY_TEMP_SYMBOL "\xEF\x8B\x88"  // f2c8
#define MY_MOTION_SYMBOL "\xEF\x8B\x9B"  // f2db
#define MY_LED_SYMBOL "\xEF\x83\xAB"  // f0eb
#define MY_TOUCH_SYMBOL "\xEF\x82\xA6" // f0a6
#define MY_CAMERA_SYMBOL "\xEF\x85\xAD" // f16d
#define MY_ESPRESSIF_SYMBOL "\xEF\x8B\xA1" // f2e1

static lv_style_t style_my_symbol;

static lv_obj_t * state = NULL;
static lv_obj_t * wifi = NULL;
static lv_obj_t * battery = NULL;
static lv_obj_t * mem = NULL;
static lv_obj_t * mem_val = NULL;
static lv_obj_t * cpu = NULL;
static lv_obj_t * cpu_val = NULL;
static lv_obj_t * message = NULL;
static lv_obj_t * shares = NULL;

static QueueHandle_t gui_event_queue = NULL;

static lv_disp_t **disp;
static lv_indev_t **indev;

static void header_create(lv_obj_t * parent)
{
    lv_obj_t *body = lv_cont_create(parent, NULL);
    lv_obj_set_width(body, lv_disp_get_hor_res(disp[0]));
    lv_obj_set_height(body, 16);

    wifi = lv_label_create(body, NULL);
    lv_label_set_text(wifi, LV_SYMBOL_WIFI);
    lv_obj_align(wifi, NULL, LV_ALIGN_IN_LEFT_MID, 0, 0);

    state = lv_label_create(body, NULL);
    lv_label_set_align(state, LV_LABEL_ALIGN_CENTER);
    lv_label_set_text(state, "00:00:00");
    // lv_obj_set_style(state, &style_my_symbol);
    lv_obj_align(state, NULL, LV_ALIGN_CENTER, 0, 0);

    battery = lv_label_create(body, NULL);
    lv_label_set_text(battery, LV_SYMBOL_BATTERY_3);
    lv_obj_align(battery, NULL, LV_ALIGN_IN_RIGHT_MID, 0, 0);

    // lv_cont_set_fit2(body, false, true);   /*Let the height set automatically*/
    lv_obj_set_pos(body, 0, 0);
}

static void side_create(lv_obj_t * parent)
{
    lv_obj_t * h1 = lv_cont_create(parent, NULL);
    lv_obj_set_size(h1, 72, 240 - 72 - 16);

    static lv_style_t style_mem;
    lv_style_copy(&style_mem, &lv_style_pretty_color);
    style_mem.line.width = 2;
    style_mem.line.color = LV_COLOR_SILVER;
    style_mem.body.main_color = lv_color_hex(0xFF4040);         
    style_mem.body.grad_color = lv_color_hex(0xC0FF3E); 
    style_mem.body.padding.left = 12;    /*Line length*/

    mem = lv_lmeter_create(h1, NULL);
    lv_lmeter_set_range(mem, 0, 60);                   /*Set the range*/
    lv_lmeter_set_value(mem, 60);                       /*Set the current value*/
    lv_lmeter_set_scale(mem, 240, 20);                  /*Set the angle and number of lines*/
    lv_lmeter_set_style(mem, LV_LMETER_STYLE_MAIN, &style_mem);           /*Apply the new style*/
    lv_obj_set_size(mem, 60, 60);
    lv_obj_align(mem, NULL, LV_ALIGN_IN_TOP_MID, 0, 10);

    mem_val = lv_label_create(h1, NULL);
    lv_label_set_text(mem_val, "60K");
    lv_obj_align(mem_val, mem, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * mem_txt = lv_label_create(h1, NULL);
    lv_label_set_text(mem_txt, "mem");
    lv_obj_align(mem_txt, mem, LV_ALIGN_IN_BOTTOM_MID, 0, 0);

    static lv_style_t style_cpu;
    lv_style_copy(&style_cpu, &lv_style_pretty_color);
    style_cpu.line.width = 2;
    style_cpu.line.color = LV_COLOR_SILVER;
    style_cpu.body.main_color = lv_color_hex(0xC0FF3E); 
    style_cpu.body.grad_color = lv_color_hex(0xFF4040); 
    style_cpu.body.padding.left = 12;        /*Line length*/

    cpu = lv_lmeter_create(h1, NULL);
    lv_lmeter_set_range(cpu, 0, 100);                   /*Set the range*/
    lv_lmeter_set_value(cpu, 0);                       /*Set the current value*/
    lv_lmeter_set_scale(cpu, 240, 20);                  /*Set the angle and number of lines*/
    lv_lmeter_set_style(cpu, LV_LMETER_STYLE_MAIN, &style_cpu);           /*Apply the new style*/
    lv_obj_set_size(cpu, 60, 60);
    lv_obj_align(cpu, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -10);

    cpu_val = lv_label_create(h1, NULL);
    lv_label_set_text(cpu_val, "0%");
    lv_obj_align(cpu_val, cpu, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * cpu_txt = lv_label_create(h1, NULL);
    lv_label_set_text(cpu_txt, "cpu");
    lv_obj_align(cpu_txt, cpu, LV_ALIGN_IN_BOTTOM_MID, 0, 0);

    lv_obj_set_pos(h1, 248, 16);

    lv_obj_t * h2 = lv_cont_create(parent, NULL);
    lv_cont_set_layout(h2, LV_LAYOUT_CENTER);

    LV_IMG_DECLARE(logo);
    lv_obj_t * img = lv_img_create(h2, NULL);
    lv_img_set_src(img, &logo);

    lv_obj_t * txt = lv_label_create(h2, NULL);
    lv_label_set_text(txt, "ESP32-S2");

    lv_obj_set_size(h2, 72, 72);
    lv_obj_set_pos(h2, 248, 240 - 72);
}

static lv_obj_t * camera_canvas = NULL;
static lv_color_t *camera_canvas_buffer = NULL;
static int gui_camera_flag = 0;

static lv_img_dsc_t shares_img = {
  .header.always_zero = 0,
  .header.cf = LV_IMG_CF_RAW_ALPHA,
  .data_size = 0,
  .data = NULL,
};

static void shares_create(lv_obj_t * parent)
{
    static lv_style_t style_terminal_symbol;

    // lv_obj_t *body = lv_cont_create(parent, NULL);
    // lv_obj_set_width(body, 250);
    // lv_obj_set_height(body, 240);

    camera_canvas_buffer = (lv_color_t *)heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(GUI_CAM_WIDTH, GUI_CAM_HIGH), MALLOC_CAP_SPIRAM);
    
    static lv_style_t style;
    lv_style_copy(&style, &lv_style_plain);
    style.text.color = LV_COLOR_RED;
    camera_canvas = lv_canvas_create(parent, NULL);
    lv_obj_t * body = camera_canvas;
    lv_obj_set_width(body, lv_disp_get_hor_res(disp[0]));
    lv_obj_set_height(body, 248);

    lv_obj_set_drag_parent(camera_canvas, true);
    lv_canvas_set_buffer(camera_canvas, camera_canvas_buffer, GUI_CAM_WIDTH, GUI_CAM_HIGH, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(camera_canvas, LV_COLOR_WHITE);
    lv_obj_align(camera_canvas, NULL, LV_ALIGN_CENTER, 0, 0);

    message = lv_label_create(body, NULL);
    lv_style_copy(&style_terminal_symbol, lv_obj_get_style(message));
    style_terminal_symbol.text.font = &lv_font_roboto_28;
    lv_label_set_style(message, LV_LABEL_STYLE_MAIN, &style_terminal_symbol);
    lv_label_set_recolor(message, true);
    lv_label_set_long_mode(message, LV_LABEL_LONG_SROLL_CIRC);     /*Circular scroll*/
    // lv_label_set_anim_speed(message, 32);
    lv_label_set_align(message, LV_LABEL_ALIGN_CENTER);
    lv_label_set_text(message, "Welcome");
    lv_obj_set_size(message, 248, 99);
    // lv_obj_align(message, NULL, LV_ALIGN_IN_TOP_MID, 0, 0);
    lv_obj_set_pos(message, 0, 0);

    lv_lodepng_init();

    shares = lv_img_create(body, NULL);

    lv_img_set_src(shares, "");
    lv_obj_set_pos(shares, 0, 99);

    lv_obj_set_pos(body, 0, 16);
}

static void gui_task(lv_task_t * arg)
{
    int ret;
    gui_event_t e;
    while (1) {
        ret = xQueueReceive(gui_event_queue, &e, 0);
        if (ret == pdFALSE) {
            break;
        }
        char *text = (char *)e.arg;
        switch (e.event) {
            case GUI_WIFI_EVENT: {
                if (atoi(text) == 1) {
                    lv_label_set_text(wifi, LV_SYMBOL_WIFI);
                } else {
                    lv_label_set_text(wifi, "");
                } 
            }
            break;

            case GUI_STATE_EVENT: {
                lv_label_set_text(state, text);
            }
            break;

            case GUI_BATTERY_EVENT: {
                switch (atoi(text)) {
                    case BATTERY_FULL: {
                        lv_label_set_text(battery, LV_SYMBOL_BATTERY_FULL);
                    }
                    break;

                    case BATTERY_3: {
                        lv_label_set_text(battery, LV_SYMBOL_BATTERY_3);
                    }
                    break;

                    case BATTERY_2: {
                        lv_label_set_text(battery, LV_SYMBOL_BATTERY_2);
                    }
                    break;

                    case BATTERY_1: {
                        lv_label_set_text(battery, LV_SYMBOL_BATTERY_1);
                    }
                    break;

                    case BATTERY_EMPTY: {
                        lv_label_set_text(battery, LV_SYMBOL_BATTERY_EMPTY);
                    }
                    break;

                    case BATTERY_CHARGE: {
                        lv_label_set_text(battery, LV_SYMBOL_CHARGE);
                    }
                    break;
                }
            }
            break;

            case GUI_MSG_EVENT: {
                lv_label_set_text(message, text);
            }
            break;

            case GUI_CAMERA_EVENT: {
                    memcpy(camera_canvas_buffer, text, GUI_CAM_WIDTH*GUI_CAM_HIGH*2);
                    lv_obj_invalidate(camera_canvas);
                    gui_camera_flag = 0;
                    continue;
            }
            break;

            case GUI_SHARES_EVENT: {
                if (strncmp(text, ESP_LUA_RAM_FILE_PATH, strlen(ESP_LUA_RAM_FILE_PATH)) == 0) {
                    esp_lua_ramf_t *ramf = (esp_lua_ramf_t *)atoi(&text[strlen(ESP_LUA_RAM_FILE_PATH)]);
                    shares_img.data = ramf->data;
                    shares_img.data_size = ramf->size;
                    uint32_t size[2];
                    memcpy(size, shares_img.data + 16, 8);

                    /*The width and height are stored in Big endian format so convert them to little endian*/
                    shares_img.header.w = (lv_coord_t) ((size[0] & 0xff000000) >> 24) +  ((size[0] & 0x00ff0000) >> 8);
                    shares_img.header.h = (lv_coord_t) ((size[1] & 0xff000000) >> 24) +  ((size[1] & 0x00ff0000) >> 8);
                    lv_img_set_src(shares, &shares_img);
                } else {
                    lv_img_set_src(shares, text);
                }
            }
            break;

            case GUI_MEM_EVENT: {
                int mem_value = atoi(text);
                char mem_str[16] = {0};
                sprintf(mem_str, "%2dK", mem_value);
                lv_lmeter_set_value(mem, mem_value);
                lv_label_set_text(mem_val, mem_str);
            }
            break;

            case GUI_CPU_EVENT: {
                int cpu_value = atoi(text);
                char cpu_str[16] = {0};
                sprintf(cpu_str, "%2d%%", cpu_value);
                lv_lmeter_set_value(cpu, cpu_value);
                lv_label_set_text(cpu_val, cpu_str);
            }
            break;
        }
        free(text);
    }
}

static int gui_event_send(int event, void *arg, int ticks_wait)
{
    int ret;
    if (gui_event_queue == NULL) {
        return -1;
    }

    gui_event_t e;
    e.event = event;
    e.arg = arg;
    ret = xQueueSend(gui_event_queue, &e, ticks_wait);
    if (ret == pdFALSE) {
        return -1;
    }

    return 0;
}

int gui_set_camera(uint8_t* src, size_t len, int ticks_wait) 
{   
    gui_camera_flag = 1;
    if (gui_event_send(GUI_CAMERA_EVENT, src, ticks_wait) == -1) {
        return -1;
    }

    while (1) {
        if (gui_camera_flag == 0) {
            break;
        } else {
            vTaskDelay(10 / portTICK_RATE_MS);
        }
    }

    return 0;
}

int gui_write(char *io, char* str, int ticks_wait) 
{
    int event = 0;

    if (strcmp(io, "WIFI") == 0) {
        event = GUI_WIFI_EVENT;
    } else if (strcmp(io, "BATT") == 0) {
        event = GUI_BATTERY_EVENT;
    } else if (strcmp(io, "STATE") == 0) {
        event = GUI_STATE_EVENT;
    } else if (strcmp(io, "MSG") == 0) {
        event = GUI_MSG_EVENT;
    } else if (strcmp(io, "SHARES") == 0) {
        event = GUI_SHARES_EVENT;
    } else if (strcmp(io, "MEM") == 0) {
        event = GUI_MEM_EVENT;
    } else if (strcmp(io, "CPU") == 0) {
        event = GUI_CPU_EVENT;
    } else {
        return -1;
    }

    char *text = (char *)malloc(strlen(str) + 1);
    strcpy(text, str);
    if (gui_event_send(event, text, ticks_wait) == 0) {
        return 0;
    } else {
        free(text);
        return -1;
    }
}

void gui_init(lv_disp_t **disp_array, lv_indev_t **indev_array, lv_theme_t * th)
{
    disp = disp_array;
    indev = indev_array;
    gui_event_queue = xQueueCreate(10, sizeof(gui_event_t));
    lv_theme_set_current(th);
    th = lv_theme_get_current();    /*If `LV_THEME_LIVE_UPDATE  1` `th` is not used directly so get the real theme after set*/
    // th->style.bg->body.main_color = LV_COLOR_BLACK;
    // th->style.bg->body.grad_color = LV_COLOR_BLACK;
    lv_cont_set_style(lv_disp_get_scr_act(disp[0]), LV_CONT_STYLE_MAIN, th->style.bg);

    LV_FONT_DECLARE(my_symbol);
    lv_style_copy(&style_my_symbol, &lv_style_scr);
    style_my_symbol.text.font = &my_symbol;

    header_create(lv_disp_get_scr_act(disp[0]));
    side_create(lv_disp_get_scr_act(disp[0]));
    shares_create(lv_disp_get_scr_act(disp[0]));

    lv_task_create(gui_task, 10, LV_TASK_PRIO_MID, NULL);
}