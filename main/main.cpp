#include <stdio.h>
#include <unity.h>

#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"

#include "driver/i2c_master.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_random.h"

#include "esp_es8311_port.h"
#include "esp_sdcard_port.h"
#include "esp_wifi_port.h"
#include "esp_3inch5_lcd_port.h"

#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_interface.h"

#define EXAMPLE_PIN_I2C_SDA GPIO_NUM_16
#define EXAMPLE_PIN_I2C_SCL GPIO_NUM_15
#define I2C_PORT_NUM 0

#define EXAMPLE_PIN_BUTTON GPIO_NUM_0

#define EXAMPLE_DISPLAY_ROTATION_NONE  0
#define EXAMPLE_DISPLAY_ROTATION_90   90
#define EXAMPLE_DISPLAY_ROTATION_180 180
#define EXAMPLE_DISPLAY_ROTATION_270 270
#define EXAMPLE_DISPLAY_ROTATION EXAMPLE_DISPLAY_ROTATION_270

#if EXAMPLE_DISPLAY_ROTATION == EXAMPLE_DISPLAY_ROTATION_90 || EXAMPLE_DISPLAY_ROTATION == EXAMPLE_DISPLAY_ROTATION_270
#define EXAMPLE_LCD_H_RES 480
#define EXAMPLE_LCD_V_RES 320
#else
#define EXAMPLE_LCD_H_RES 320
#define EXAMPLE_LCD_V_RES 480
#endif

#define EXAMPLE_LCD_BIT_PER_PIXEL 16
#define LCD_BUFFER_SIZE EXAMPLE_LCD_H_RES*EXAMPLE_LCD_V_RES*2
//#define LCD_BUFFER_SIZE EXAMPLE_LCD_H_RES*EXAMPLE_LCD_V_RES*EXAMPLE_LCD_BIT_PER_PIXEL/8
//#define LCD_BUFFER_SIZE EXAMPLE_LCD_H_RES*EXAMPLE_LCD_V_RES / 8

static const char *TAG = "FNK0104S_Jet";

i2c_master_bus_handle_t i2c_bus_handle;

esp_lcd_panel_io_handle_t io_handle = NULL;
esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_touch_handle_t touch_handle = NULL;
esp_lcd_panel_t *panel;

//esp_lcd_touch_point_data_t touch_point_data[2];
uint16_t tp_x[2];
uint16_t tp_y[2];
uint16_t tp_strength[2];
uint8_t point_num = 0;
bool touch_pressed = false;
void touchpad_read();

// sdmmc_card_t *card = NULL;

void i2c_bus_init(void);
void lv_port_init(void);

uint16_t *fb[2];
uint16_t *depth = NULL;
int draw_idx = 0;

StaticSemaphore_t lcd_sem_buffer;
volatile SemaphoreHandle_t lcd_trans_done_sem = NULL;

void jet_render_task(void *pvParameters);
void JetExample(esp_lcd_panel_handle_t panel_handle, uint32_t width, uint32_t height);
void drawBitmap(esp_lcd_panel_handle_t panel_handle, uint32_t width, uint32_t height);

uint16_t swap_bytes_16(uint16_t val) {
    return ((val >> 8) | (val << 8)) ;
    //return ((val & 0x00FF) << 8) | ((val & 0xFF00) >> 8);
}

typedef struct {
    esp_lcd_panel_handle_t panel_handle;
    uint32_t width;
    uint32_t height;
} jet_task_args_t;

#include "Jet.hpp"
using namespace Renderer;

extern "C" void app_main(void)
{
    // 세마포어 생성 직전에 출력해 보기
    //printf("Free heap before semaphore creation: %ld bytes\n", esp_get_free_heap_size());
    //printf("Free internal heap: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // 정적 할당 방식으로 세마포어 생성 (힙 메모리를 쓰지 않음)
    lcd_trans_done_sem = xSemaphoreCreateBinaryStatic(&lcd_sem_buffer);
    // 동적 할당.
    //lcd_trans_done_sem = xSemaphoreCreateBinary();
    if (lcd_trans_done_sem == NULL) {
        ESP_LOGI(TAG, "lcd_trans_done_sem = xSemaphoreCreateBinary() failed.");
        return;
    } else {
        ESP_LOGI(TAG, "lcd_trans_done_sem = xSemaphoreCreateBinary() done.");
    }

    // app_main 또는 jet_render_task 진입 직후
    // 첫번째 루프의 xSemaphoreTake가 바로 통과되도록 Give 실행
    xSemaphoreGive(lcd_trans_done_sem);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ST7796 display init
    esp_3inch5_display_port_init(&io_handle, &panel_handle, 0); // 0: Maximum transfer size
    //esp_3inch5_display_port_init(&io_handle, &panel_handle, LCD_BUFFER_SIZE);

    // I2C init
    i2c_bus_init();

    // FT6336 touch init
    esp_3inch5_touch_port_init(&touch_handle, i2c_bus_handle, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, EXAMPLE_DISPLAY_ROTATION);
    vTaskDelay(pdMS_TO_TICKS(100));
    touch_pressed = false;

    // ES8311 I2S init
    esp_es8311_port_init(i2c_bus_handle);

    // SD Card init
    esp_sdcard_port_init();

    // WiFi init
    esp_wifi_port_init("s629", "darksky85");

    // ST7796 display beightness init
    esp_3inch5_brightness_port_init();
    esp_3inch5_brightness_port_set(100);

    panel = (esp_lcd_panel_t *)panel_handle;
// Rotation is controlled here via MADCTL(0x36) register of ST7796.
#if EXAMPLE_DISPLAY_ROTATION == EXAMPLE_DISPLAY_ROTATION_NONE
    panel->swap_xy(panel, false);
    panel->mirror(panel, true /*mirror_x*/, false /*mirror_y*/);
#elif EXAMPLE_DISPLAY_ROTATION == EXAMPLE_DISPLAY_ROTATION_90
    panel->swap_xy(panel, true);
    panel->mirror(panel, false /*mirror_x*/, false /*mirror_y*/);
#elif EXAMPLE_DISPLAY_ROTATION == EXAMPLE_DISPLAY_ROTATION_180
    panel->swap_xy(panel, false);
    panel->mirror(panel, false /*mirror_x*/, true /*mirror_y*/);
#elif EXAMPLE_DISPLAY_ROTATION == EXAMPLE_DISPLAY_ROTATION_270
    panel->swap_xy(panel, true);
    panel->mirror(panel, true /*mirror_x*/, true /*mirror_y*/);
#endif

    fb[0] = (uint16_t *)heap_caps_malloc(LCD_BUFFER_SIZE, MALLOC_CAP_DMA|MALLOC_CAP_SPIRAM );
    TEST_ASSERT_NOT_NULL(fb[0]);
    fb[1] = (uint16_t *)heap_caps_malloc(LCD_BUFFER_SIZE, MALLOC_CAP_DMA|MALLOC_CAP_SPIRAM );
    TEST_ASSERT_NOT_NULL(fb[1]);

    depth = (uint16_t *)heap_caps_malloc(ZBUFFER_STRIDE((int32_t)EXAMPLE_LCD_H_RES)*(int32_t)EXAMPLE_LCD_V_RES, MALLOC_CAP_DMA|MALLOC_CAP_SPIRAM );
    TEST_ASSERT_NOT_NULL(depth);

    JetExample(panel_handle, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
}

void i2c_bus_init(void)
{
    i2c_master_bus_config_t i2c_mst_config = {};
    i2c_mst_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_mst_config.i2c_port = (i2c_port_num_t)I2C_PORT_NUM;
    i2c_mst_config.scl_io_num = EXAMPLE_PIN_I2C_SCL;
    i2c_mst_config.sda_io_num = EXAMPLE_PIN_I2C_SDA;
    i2c_mst_config.glitch_ignore_cnt = 7;
    i2c_mst_config.flags.enable_internal_pullup = 1;

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &i2c_bus_handle));
}

void touchpad_read()
{
    touch_handle->read_data(touch_handle);
    touch_handle->get_xy(touch_handle, tp_x, tp_y, tp_strength, &point_num, 2);
    if (point_num > 0) {
        if (touch_pressed == false) {
            touch_pressed = true;
        }
        //printf("nTouch:%d, x1: %3d, y1: %3d, x2: %3d, y2:%3d\n", point_num,
            //tp_x[0], tp_y[0], tp_x[1], tp_y[1]);
            //touch_handle->data.coords[0].x, touch_handle->data.coords[0].y,
            //touch_handle->data.coords[1].x, touch_handle->data.coords[1].y);
    } else {
        if (touch_pressed == true) {
            touch_pressed = false;
        }
        return;
    }
}

void JetExample(esp_lcd_panel_handle_t panel_handle, uint32_t width, uint32_t height)
{
    // 태스크 인자 구조체 동적 할당 및 세팅
    jet_task_args_t *task_args = (jet_task_args_t *)malloc(sizeof(jet_task_args_t));
    if (task_args != NULL) {
        task_args->panel_handle = panel_handle;
        task_args->width = EXAMPLE_LCD_H_RES;
        task_args->height = EXAMPLE_LCD_V_RES;
    
        // Core 1에 높은 우선순위로 태스크 생성 후 구조체 포인터 전달
        xTaskCreatePinnedToCore(
            jet_render_task,        // 태스크 함수
            "jet_render_task",      // 태스크 이름      
            1024 * 8,               // 스택 크기
            (void *)task_args,      // 인자 구조체 포인터 주입!
            5,                      // 우선순위
            NULL,                   // 태스크 핸들
            1                       // Core 1 고정
        );
    } else {
        ESP_LOGE(TAG, "Task args allocation failed");
    }

    ESP_LOGI("JetExample", "JetExample done.");

    // JetExample 은 여기서 끝.  
    // 할당된 fb, depth, 태스크 등은 백그라운드에서 계속 유지됩니다.
    vTaskDelete(NULL);
}

void jet_render_task(void *pvParameters)
{
    // app_main 또는 jet_render_task 진입 직후
    // 첫번째 루프의 xSemaphoreTake가 바로 통과되도록 Give 실행
    xSemaphoreGive(lcd_trans_done_sem);

    // void* 포인터를 원래 구조체 포인터로 캐스팅하여 인자 추출
    jet_task_args_t *args = (jet_task_args_t *)pvParameters;

    //esp_lcd_panel_handle_t panel = args->panel_handle;
    uint32_t width = args->width;
    uint32_t height = args->height;

    // 동적 할당된 구조체 메모리는 값을 다 복사했으므로 바로 해제 (메모리 누수 방지)
    free(args);

    // 🚀 FPS 계산용 변수 선언
    uint32_t frame_count = 0;
    uint32_t last_time = int64_t(esp_timer_get_time() / 1000); // 현재 밀리초 구하기

    ESP_LOGI("JetTask", "Jet Render Task Started on Core %d (%dx%d)",
             xPortGetCoreID(), (int32_t)width, (int32_t)height);

    Scene  scene(fb[draw_idx], depth, (int32_t)width, (int32_t)height);
    //scene.setBackcolor(0x0000);          // RGB565 clear colour (black)
    scene.setBackcolor(0xffff);          // RGB565 clear colour (white)
    scene.setClearBuffer(true);

    Camera camera;
    camera.setPosition(0, 0, -500);      // world units
    camera.setFOV((int32_t)75, (int32_t)width);
    camera.nearPlane = 16;
    camera.farPlane  = 8192;
    scene.setCamera(&camera);

    DirectionalLight sun(Vector3{45, 35, 0}, Color{255, 245, 220}, 220);
    AmbientLight     amb(Color{40, 48, 64});
    scene.setDirectionalLight(&sun);
    scene.setAmbientLight(&amb);

    Material red(swap_bytes_16(0xF800));                // RGB565: bright red
    Material green(swap_bytes_16(0x07E0));                // RGB565: bright green
    Material blue(swap_bytes_16(0x001F));                // RGB565: bright blue
    red.shadingMode = ShadingMode::GOURAUD;

    //Object* cube = Primitives::createCube(200, 200, 200, &red);
    //cube->setPosition(0, 0, 100);
    //scene.addObject(cube);
    Object* cube = Primitives::createCube(100, 100, 100, &red);
    cube->setPosition(0, 0, 200);
    scene.addObject(cube);
    Object* plane = Primitives::createPlane(500, 500, &green);
    cube->setPosition(0, 10, 0);
    scene.addObject(plane);
    Object* sphere = Primitives::createSphere(50, 12/*16*/, &blue);
    cube->setPosition(200, 0, 0);
    scene.addObject(sphere);

    // ===================================================
    // 🎥 [1] 터치 및 카메라 제어용 상태 변수
    // ===================================================
    static int16_t prev_touch_x = 0;
    static int16_t prev_touch_y = 0;
    static bool was_touched = false;

    // 멀티터치(핀치 줌) 상태 변수
    static float prev_pinch_dist = 0.0f;
    static bool is_multi_touch = false;

    // 카메라 위치/거리 제어 변수
    float cam_distance = 500.0f; // 카메라와 원점 사이의 거리
    float cam_angle_x  = 0.0f;   // 좌우 회전 각도 (Yaw)
    float cam_angle_y  = 0.0f;   // 상하 회전 각도 (Pitch)

    // Per-frame: rotate, render, push the buffer to your display.
    for (;;) {

        // 1. 터치 패드 데이터 읽기
        touchpad_read();
        //if (touch_pressed == true) {
        //    for(int i = 0; i < touch_data.touch_num; i++) {
        //        printf("id:%02d x:%3d y:%3d ", i, touch_data.coords[i].x, touch_data.coords[i].y);
        //    }
        //    printf("\n");
        //}

        // 2. 터치 입력 분기 처리 (1개 손가락 vs 2개 이상 손가락)
        if (touch_pressed && point_num > 0) {

            // ----------------------------------------------------
            // ✌️  [A] 멀티터치 (2개 이상): 확대 / 축소 (Pinch-to-Zoom)
            // ----------------------------------------------------
            if (point_num >= 2) {
                int16_t x0 = tp_x[0];
                int16_t y0 = tp_y[0];
                int16_t x1 = tp_x[1];
                int16_t y1 = tp_y[1];

                // 두 점 사이의 피타고라스 거리 계산
                float dx = (float)(x0 - x1);
                float dy = (float)(y0 - y1);
                float current_dist = sqrtf(dx * dx + dy * dy);

                if (is_multi_touch) {
                    // 이전 프레임과의 거리 차이
                    float dist_diff = current_dist - prev_pinch_dist;

                    // 두 손가락이 멀어지면(dist_diff > 0) -> 카메라 거리 줄임 (확대)
                    // 두 손가락이 좁아지면(dist_diff < 0) -> 카메라 거리 늘림 (축소)
                    cam_distance -= dist_diff * 1.5f; // 1.5f는 줌 감도 (조절 가능)

                    // 줌 제한 범위 설정 (최대 확대 / 최대 축소)
                    if (cam_distance < 150.0f)  cam_distance = 150.0f;  // 너무 가까우면 단면이 잘림
                    if (cam_distance > 2000.0f) cam_distance = 2000.0f; // 너무 멀어지면 사방이 잘림
                }
                prev_pinch_dist = current_dist;
                is_multi_touch = true;
                was_touched = false; // 멀티터치 중에는 회전 로직 튀지 않도록 초기화
            }
            // ----------------------------------------------------
            // ☝️  [B] 싱글터치 (1개): 화면 회전 (Rotate)
            // ----------------------------------------------------
            else if (point_num == 1) {

                // 멀티터치에서 1개 손가락으로 전환된 순간 회전 튐 방지
                if (is_multi_touch) {
                    is_multi_touch = false;
                    was_touched = false;
                }

                int16_t current_x = tp_x[0];
                int16_t current_y = tp_y[0];

                if (was_touched) {
                    int16_t dx = current_x - prev_touch_x;
                    int16_t dy = current_y - prev_touch_y;

                    cam_angle_x += dy * 0.01f;
                    cam_angle_y += dx * 0.01f;

                    if (cam_angle_y > 1.4f)  cam_angle_y = 1.4f;
                    if (cam_angle_y < -1.4f) cam_angle_y = -1.4f;
                }

                prev_touch_x = current_x;
                prev_touch_y = current_y;
                was_touched = true;
            }
        } else {
            // 손가락을 모두 떼었을 때 플래그 해제
            was_touched = false;
            is_multi_touch = false;
        }

        // ===================================================
        // 🎥 [2] 삼각함수 기반 구면(Orbital) 좌표계 계산
        // ===================================================
        int32_t cam_x = (int32_t)(cam_distance * sinf(cam_angle_x) * cosf(cam_angle_y));
        int32_t cam_y = (int32_t)(cam_distance * sinf(cam_angle_y));
        int32_t cam_z = (int32_t)(-cam_distance * cosf(cam_angle_x) * cosf(cam_angle_y));

        // 계산된 3D 좌표로 카메라 이동
        camera.setPosition(cam_x, cam_y, cam_z);

        ////cube->rotate(0, 1, 0);           // 1 degree per frame around Y
        ////cube->rotate(3, 4, 5);
        cube->rotate(1, 1, 0);
        sphere->rotate(0, 1, 1);
        plane->rotate(1, 0, 1);
        scene.render();                  // colour buffer now contains the frame

        // -----------------------------------------------------------
        // 2. [동기화] "다음으로 교체할 버퍼"가 이전 DMA 전송을 마쳤는지 확인
        //    (첫 번째 프레임에서는 이전 전송이 없으므로 바이패스하도록 세마포어 미리 Give 필요)
        // -----------------------------------------------------------
        if (xSemaphoreTake(lcd_trans_done_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "LCD DMA Timeout!");
        }

        drawBitmap(panel_handle, width, height);

        // Circulate frame buffer
        draw_idx = 1 - draw_idx;
        scene.setFramebuffer(fb[draw_idx]);

        // 🚀 프레임 카운트 증가
        frame_count++;

        // 현재 시간 측정
        uint32_t current_time = int64_t(esp_timer_get_time() / 1000);

        // 1초(1000ms)가 경과했는지 체크
        if (current_time - last_time >= 1000) {
            // 터미널에 FPS 출력
            ESP_LOGI("JetFPS", "Current FPS: %lu", frame_count);

            // 변수 초기화
            frame_count = 0;
            last_time = current_time;
        }
    }
}

void drawBitmap(esp_lcd_panel_handle_t panel_handle, uint32_t width, uint32_t height)
{

    //ESP_LOGI(TAG, "esp_lcd_panel_draw_bitmap() - 1");
    ESP_ERROR_CHECK(panel_handle->draw_bitmap(panel_handle, 0, 0, width, height, fb[draw_idx]));
}

