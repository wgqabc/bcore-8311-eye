#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"

#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "esp32_camera.h"
#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include "iot/thing_manager.h"
#include "i2c_device.h"
#include "driver/i2c.h"
#include "display/eye_display.h"
#include "esp_lcd_gc9a01.h"
#define TAG "XiaoZhiEyeBoard"

LV_FONT_DECLARE(font_puhui_20_4);   //表示名为 puhui 的字体,是一个常规字体，用于显示文本内容，大小为 14
LV_FONT_DECLARE(font_awesome_20_4); //表示名为 font_awesome 的字体,是一个图标字体库，用于显示各种图标，大小为 14


class XiaoZhiEyeBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    i2c_master_bus_handle_t camera_i2c_bus_; // 
    Button boot_button_;
    LcdDisplay* display_;
     /* LCD IO and panel */
    esp_lcd_panel_io_handle_t lcd_io1 = NULL;
    esp_lcd_panel_handle_t lcd_panel1 = NULL;
    esp_lcd_panel_io_handle_t lcd_io2 = NULL;
    esp_lcd_panel_handle_t lcd_panel2 = NULL;
    Esp32Camera* camera_;
// /* =================================*/
  
    // 魔眼的初始化互斥锁
    void lcd_mutex_init(void) {
        if (lcd_mutex == NULL) {
            lcd_mutex = xSemaphoreCreateMutex();
            if (lcd_mutex == NULL) {
                ESP_LOGE("LCD", "Failed to create LCD mutex");
                abort();    //abort()函数用于在程序遇到严重错误时终止程序的执行。
            }
        }
    }

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }
// 修改InitializeI2c2()
void InitializeI2c2() {
    i2c_master_bus_config_t i2c_bus_cfg2 = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = CAMERA_PIN_SIOD,
        .scl_io_num = CAMERA_PIN_SIOC,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
                .enable_internal_pullup = 1,
            },
    };
    
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg2, &bus_handle));
    
    // 增加超时时间并降低I2C速率
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x30, // 确认这是正确的摄像头地址
        .scl_speed_hz = 400000, // 尝试降低到50000
    };
    
    i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));
}
     //物联网初始化，添加对AI可见设备
     void initializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        //thing_manager.AddThing(iot::CreateThing("Screen"));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    // GC9A01-SPI2初始化-用于显示小智
    void InitializeSpiEye1() {
        const spi_bus_config_t buscfg = {       
            .mosi_io_num = GC9A01_SPI1_LCD_GPIO_MOSI,
            .miso_io_num = GPIO_NUM_NC,
            .sclk_io_num = GC9A01_SPI1_LCD_GPIO_SCLK,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .max_transfer_sz = GC9A01_LCD_H_RES * GC9A01_LCD_V_RES * sizeof(uint16_t), // 增大传输大小,
        };
        ESP_ERROR_CHECK(spi_bus_initialize(GC9A01_LCD_SPI1_NUM, &buscfg, SPI_DMA_CH_AUTO));
    }

    // GC9A01-SPI2初始化-用于显示魔眼
    void InitializeGc9a01DisplayEye1() {
        ESP_LOGI(TAG, "Init GC9A01 display1");

        ESP_LOGI(TAG, "Install panel IO1");
        ESP_LOGD(TAG, "Install panel IO1");
        const esp_lcd_panel_io_spi_config_t io_config = {
            .cs_gpio_num = GC9A01_SPI1_LCD_GPIO_CS,
            .dc_gpio_num = GC9A01_SPI1_LCD_GPIO_DC,
            .spi_mode = 0,
            .pclk_hz = GC9A01_LCD_PIXEL_CLK_HZ,
            .trans_queue_depth = 10,
            .lcd_cmd_bits = GC9A01_LCD_CMD_BITS,
            .lcd_param_bits = GC9A01_LCD_PARAM_BITS,
            

        };
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)GC9A01_LCD_SPI1_NUM, &io_config, &lcd_io1);
        lcd_io_eye = lcd_io1;

        ESP_LOGD(TAG, "Install LCD1 driver");
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = GC9A01_SPI1_LCD_GPIO_RST,
            .color_space = GC9A01_LCD_COLOR_SPACE,
            .bits_per_pixel = GC9A01_LCD_BITS_PER_PIXEL,
            
        };
        panel_config.rgb_endian = DISPLAY_RGB_ORDER;
        esp_lcd_new_panel_gc9a01(lcd_io1, &panel_config, &lcd_panel1);
        lcd_panel_eye = lcd_panel1;
        
        esp_lcd_panel_reset(lcd_panel1);
        esp_lcd_panel_init(lcd_panel1);
        esp_lcd_panel_invert_color(lcd_panel1, true);
        esp_lcd_panel_disp_on_off(lcd_panel1, true);
    }

    // GC9A01-SPI2初始化-用于显示魔眼
    void InitializeSpiEye2() {
        const spi_bus_config_t buscfg = {       
            .mosi_io_num = GC9A01_SPI2_LCD_GPIO_MOSI,
            .miso_io_num = GPIO_NUM_NC,
            .sclk_io_num = GC9A01_SPI2_LCD_GPIO_SCLK,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .max_transfer_sz = GC9A01_LCD_H_RES * GC9A01_LCD_V_RES * sizeof(uint16_t),
        };
        ESP_ERROR_CHECK(spi_bus_initialize(GC9A01_LCD_SPI2_NUM, &buscfg, SPI_DMA_CH_AUTO));
    }

    // GC9A01-SPI2初始化-用于显示魔眼
    void InitializeGc9a01DisplayEye2() {
        ESP_LOGI(TAG, "Init GC9A01 display2");

        ESP_LOGI(TAG, "Install panel IO2");
        ESP_LOGD(TAG, "Install panel IO2");
        const esp_lcd_panel_io_spi_config_t io_config = {
            .cs_gpio_num = GC9A01_SPI2_LCD_GPIO_CS,
            .dc_gpio_num = GC9A01_SPI2_LCD_GPIO_DC,
            .spi_mode = 0,
            .pclk_hz = GC9A01_LCD_PIXEL_CLK_HZ,
            .trans_queue_depth = 10,
            .lcd_cmd_bits = GC9A01_LCD_CMD_BITS,
            .lcd_param_bits = GC9A01_LCD_PARAM_BITS,


        };
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)GC9A01_LCD_SPI2_NUM, &io_config, &lcd_io2);
        lcd_io_eye2 = lcd_io2;

        ESP_LOGD(TAG, "Install LCD2 driver");
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = GC9A01_SPI2_LCD_GPIO_RST,
            .color_space = GC9A01_LCD_COLOR_SPACE,
            .bits_per_pixel = GC9A01_LCD_BITS_PER_PIXEL
        };
        panel_config.rgb_endian = DISPLAY_RGB_ORDER;
        esp_lcd_new_panel_gc9a01(lcd_io2, &panel_config, &lcd_panel2);
        lcd_panel_eye2 = lcd_panel2;
        esp_lcd_panel_reset(lcd_panel2);
        esp_lcd_panel_init(lcd_panel2);
        esp_lcd_panel_invert_color(lcd_panel2, true);
        esp_lcd_panel_disp_on_off(lcd_panel2, true);
        
    }

    //初始化双屏
    void InitializeDualScreenEye(){
        // 初始化第一块屏幕
        InitializeSpiEye1();
        InitializeSpiEye2();
        InitializeGc9a01DisplayEye1();
        InitializeGc9a01DisplayEye2();

        // 创建双屏显示对象
        display_ = new DualScreenDisplay(
            lcd_io1, lcd_panel1,
            lcd_io2, lcd_panel2,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
            {
                .text_font = &font_puhui_20_4,
                .icon_font = &font_awesome_20_4,
                .emoji_font = font_emoji_64_init(),
            }
        );
    }
    void InitializeCamera() {
        // Open camera power

     camera_config_t config = {};
        config.ledc_channel = LEDC_CHANNEL_0;  // LEDC通道选择  用于生成XCLK时钟 但是S3不用
        config.ledc_timer = LEDC_TIMER_0; // LEDC timer选择  用于生成XCLK时钟 但是S3不用
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
//        config.pin_sccb_sda = CAMERA_PIN_SIOD;   // 这里写-1 表示使用已经初始化的I2C接口
//        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.pin_sccb_sda = -1;   // 这里写-1 表示使用已经初始化的I2C接口
        config.pin_sccb_scl = -1;
        config.sccb_i2c_port = I2C_NUM_1;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = 20000000;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
//        config.fb_location = CAMERA_FB_IN_DRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

        camera_ = new Esp32Camera(config);
  
    }

public:
  
  //没有按键
  XiaoZhiEyeBoard() : boot_button_(BOOT_BUTTON_GPIO) {

 
    gpio_set_direction(AUDIO_CODEC_PA_PIN, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(AUDIO_CODEC_PA_PIN, GPIO_PULLDOWN_ONLY);
    gpio_set_level(AUDIO_CODEC_PA_PIN, 0); //初始化
    InitializeI2c();
     InitializeI2c2();     
ESP_LOGI(TAG, "Init dual eye");  
ESP_LOGD(TAG, "Init dual eye"); 
    lcd_mutex_init();
    InitializeDualScreenEye();
ESP_LOGI(TAG, "Init dual ok");
ESP_LOGD(TAG, "Init dual ok");
    InitializeButtons();
    initializeIot();
      InitializeCamera();  

}


  virtual AudioCodec* GetAudioCodec() override {
    static Es8311AudioCodec audio_codec(
        codec_i2c_bus_, 
        I2C_NUM_0, 
        AUDIO_INPUT_SAMPLE_RATE, 
        AUDIO_OUTPUT_SAMPLE_RATE,
        AUDIO_I2S_GPIO_MCLK, 
        AUDIO_I2S_GPIO_BCLK, 
        AUDIO_I2S_GPIO_WS, 
        AUDIO_I2S_GPIO_DOUT, 
        AUDIO_I2S_GPIO_DIN,
        AUDIO_CODEC_PA_PIN, 
        AUDIO_CODEC_ES8311_ADDR,
        1,
        0);
    return &audio_codec;
}



    //virtual Backlight* GetBacklight() override {
    //    static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    //    return &backlight;
    //}
    virtual Camera* GetCamera() override {
        return camera_;
    }

};

//DECLARE_BOARD(bcoreeyecam);
DECLARE_BOARD(XiaoZhiEyeBoard);
