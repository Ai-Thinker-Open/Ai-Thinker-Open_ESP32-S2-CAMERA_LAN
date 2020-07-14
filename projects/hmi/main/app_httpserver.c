/* ESPRESSIF MIT License
 * 
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 * 
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "app_httpserver.h"
#include "esp_log.h"

#include "cam.h"



static const char *TAG = "app_httpserver";

#define FACE_COLOR_WHITE  0x00FFFFFF
#define FACE_COLOR_BLACK  0x00000000
#define FACE_COLOR_RED    0x000000FF
#define FACE_COLOR_GREEN  0x0000FF00
#define FACE_COLOR_BLUE   0x00FF0000
#define FACE_COLOR_YELLOW (FACE_COLOR_RED | FACE_COLOR_GREEN)
#define FACE_COLOR_CYAN   (FACE_COLOR_BLUE | FACE_COLOR_GREEN)
#define FACE_COLOR_PURPLE (FACE_COLOR_BLUE | FACE_COLOR_RED)

#define ENROLL_CONFIRM_TIMES    1
#define FACE_ID_SAVE_NUMBER     10

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

extern char ip_addr_buf[20];


httpd_handle_t stream_httpd = NULL;
//httpd_handle_t camera_httpd = NULL;

esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = ESP_OK;
	int again_s=0;

	char * part_buf[128];
	int64_t fr_end = 0;
	static int64_t last_frame = 0;
	if(!last_frame)
	{
		last_frame = esp_timer_get_time();
	}
    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }
    //httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    //httpd_resp_set_hdr(req, "X-Framerate", "60");
	ESP_LOGI(TAG, "start stream---->");

	uint8_t *cam_buf = NULL;

	cam_start();
    while(true)
    {
		size_t recv_len = cam_take(&cam_buf);
		if(res == ESP_OK){
		res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
		}
        if(res == ESP_OK){
        size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, recv_len);
        res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
       	}
		if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)cam_buf, recv_len);
        }
		if(res != ESP_OK)
		{
			ESP_LOGE(TAG, "send err: %d\n", res);
			if(again_s++>20)
				break;
			cam_give(cam_buf);
			continue;
		}
		fr_end = esp_timer_get_time();
		int64_t frame_time = fr_end-last_frame;
		last_frame = fr_end;
		frame_time /= 1000;
		ESP_LOGI(TAG, "MJPG: %uKB %ums (%.1ffps)",
                (uint32_t)(recv_len/1024),
                (uint32_t)frame_time, 
                1000.0 / (uint32_t)frame_time);
		ESP_LOGI(TAG, "Free heap: %u", xPortGetFreeHeapSize());//Memory monitoring
		cam_give(cam_buf);
	}
	cam_give(cam_buf);
	cam_stop();
    return ESP_OK;
}

void app_httpserver_init ()
{

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.stack_size = 4096 * 2;

	httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
    	.user_ctx = NULL
	};
	ESP_LOGI(TAG, "Open http://%s/stream picture size is %dx%d", ip_addr_buf,GUI_CAM_WIDTH,GUI_CAM_HIGH);
	ESP_LOGI(TAG, "Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
	else	
		ESP_LOGE(TAG, "stream_httpd fail");
}

