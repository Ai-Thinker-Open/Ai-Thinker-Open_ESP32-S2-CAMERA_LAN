#ifndef _CAM_H_
#define _CAM_H_

#ifdef __cplusplus
extern "C" {
#endif
//图像大小设置，必须为4的整数倍且小于开窗大小
//开窗大小的设置，必须为4的整数倍
#define CAM_WIDTH   (800) 
#define CAM_HIGH    (600)
#define GUI_CAM_WIDTH   (800)//(320)//(248)	
#define GUI_CAM_HIGH    (600)//(240)//(248)

#define CAM_BOARD "ESP_CAM"
#define CAM_XCLK  1
#define CAM_PCLK  33
#define CAM_VSYNC 2
#define CAM_HSYNC 3

#define CAM_D0    46
#define CAM_D1    45
#define CAM_D2    41
#define CAM_D3    42
#define CAM_D4    39
#define CAM_D5    40
#define CAM_D6    21
#define CAM_D7    38

#define CAM_SCL   7
#define CAM_SDA   8

typedef struct {
    uint8_t pin_data[16];
    uint8_t vsync_invert;
    uint8_t hsync_invert;
	uint8_t task_pri;
    uint8_t bit_width;
    uint32_t xclk_fre;
    uint32_t max_buffer_size; // DMA used
    uint32_t task_stack;
    union {
        struct {
            uint32_t xclk:   8;
            uint32_t pclk:   8;
            uint32_t vsync:  8;
            uint32_t hsync:  8;
        };
        uint32_t val;
    } pin;
    union {
        struct {
            uint32_t width:   16;
            uint32_t high:    16;
        };
        uint32_t val;
    } size;
    union {
        struct {
            uint32_t jpeg:   1; 
        };
        uint32_t val;
    } mode;
    union {
	    struct {
	        uint32_t sscb_sda:   16;
            uint32_t sscb_scl:   16;
	    };
	    uint32_t val;
    } I2C;
    uint8_t *frame1_buffer;
    uint8_t *frame2_buffer;
} cam_config_t;

void cam_start(void);
void cam_stop(void);
size_t cam_take(uint8_t **buffer_p);
void cam_give(uint8_t *buffer);
void cam_deinit();
int cam_init(const cam_config_t *config);

#ifdef __cplusplus
}
#endif
#endif // cam_h
