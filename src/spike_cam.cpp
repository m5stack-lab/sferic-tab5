// sferic-tab5 camera bring-up spike (ADR-0013 §6 `camera`).
//
// Framework decision (ADR-0013 §2): the Tab5 camera needs esp_video, an
// ESP-IDF component -> [env:cam] is `framework = espidf`. We replicate
// esp-bsp's bsp_camera_start() by hand (I2C bus 1 + PI4IOE5V6408 pin 6 to
// power the camera + esp_video_init MIPI-CSI) so we don't drag in the whole
// BSP's LVGL/display stack. Constants lifted from esp-bsp/bsp/m5stack_tab5.
//
// Serial-only: prove the capture path. Viewfinder + the M5GFX-vs-BSP-LCD
// display decision come later (ADR-0013 §4).

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>

#include "driver/i2c_master.h"
#include "esp_io_expander_pi4ioe5v6408.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linux/videodev2.h"

static const char *TAG = "cam-spike";

// --- Tab5 constants (esp-bsp/bsp/m5stack_tab5) ---
#define TAB5_I2C_PORT      I2C_NUM_1
#define TAB5_I2C_SDA       31
#define TAB5_I2C_SCL       32
#define TAB5_IOEXP_ADDR    ESP_IO_EXPANDER_I2C_PI4IOE5V6408_ADDRESS_LOW  // 0x43
#define TAB5_CAMERA_EN_PIN IO_EXPANDER_PIN_NUM_6

#define NBUF 2
static int s_fd = -1;
static uint8_t *s_buf[NBUF] = {};
static size_t s_len[NBUF] = {};
static uint16_t s_w = 0, s_h = 0;
static uint32_t s_fourcc = 0;

static i2c_master_bus_handle_t s_i2c = nullptr;

static esp_err_t powerOnCamera(void)
{
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = TAB5_I2C_PORT;
    bus.sda_io_num = (gpio_num_t)TAB5_I2C_SDA;
    bus.scl_io_num = (gpio_num_t)TAB5_I2C_SCL;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    esp_err_t e = i2c_new_master_bus(&bus, &s_i2c);
    if (e != ESP_OK) { ESP_LOGE(TAG, "i2c_new_master_bus -> %s", esp_err_to_name(e)); return e; }

    esp_io_expander_handle_t io = nullptr;
    e = esp_io_expander_new_i2c_pi4ioe5v6408(s_i2c, TAB5_IOEXP_ADDR, &io);
    if (e != ESP_OK) { ESP_LOGE(TAG, "pi4ioe new -> %s", esp_err_to_name(e)); return e; }
    e |= esp_io_expander_set_dir(io, TAB5_CAMERA_EN_PIN, IO_EXPANDER_OUTPUT);
    e |= esp_io_expander_set_output_mode(io, TAB5_CAMERA_EN_PIN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
    e |= esp_io_expander_set_level(io, TAB5_CAMERA_EN_PIN, 1);
    ESP_LOGI(TAG, "camera power enable (ioexp 0x%02x pin 6) -> %s", TAB5_IOEXP_ADDR,
             esp_err_to_name(e));
    vTaskDelay(pdMS_TO_TICKS(120));
    return e;
}

static esp_err_t videoInit(void)
{
    esp_video_init_csi_config_t csi = {};
    csi.sccb_config.init_sccb = false;
    csi.sccb_config.i2c_handle = s_i2c;
    csi.sccb_config.freq = 400000;
    csi.reset_pin = GPIO_NUM_NC;
    csi.pwdn_pin = GPIO_NUM_NC;

    esp_video_init_config_t cfg = {};
    cfg.csi = &csi;
    esp_err_t e = esp_video_init(&cfg);
    ESP_LOGI(TAG, "esp_video_init -> %s", esp_err_to_name(e));
    return e;
}

static bool streamOpen(void)
{
    s_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (s_fd < 0) { ESP_LOGE(TAG, "open(%s)", ESP_VIDEO_MIPI_CSI_DEVICE_NAME); return false; }

    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct v4l2_format fmt = {};
    fmt.type = type;
    ioctl(s_fd, VIDIOC_G_FMT, &fmt);
    uint16_t nw = fmt.fmt.pix.width, nh = fmt.fmt.pix.height;
    ESP_LOGI(TAG, "native %ux%u", nw, nh);

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = type;
    fmt.fmt.pix.width = nw;
    fmt.fmt.pix.height = nh;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    if (ioctl(s_fd, VIDIOC_S_FMT, &fmt) != 0)
        ESP_LOGW(TAG, "S_FMT RGB565 rejected");

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = type;
    ioctl(s_fd, VIDIOC_G_FMT, &fmt);
    s_w = fmt.fmt.pix.width;
    s_h = fmt.fmt.pix.height;
    s_fourcc = fmt.fmt.pix.pixelformat;
    ESP_LOGI(TAG, "stream %ux%u fourcc %c%c%c%c", s_w, s_h, s_fourcc & 0xFF,
             (s_fourcc >> 8) & 0xFF, (s_fourcc >> 16) & 0xFF, (s_fourcc >> 24) & 0xFF);

    struct v4l2_requestbuffers req = {};
    req.count = NBUF;
    req.type = type;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(s_fd, VIDIOC_REQBUFS, &req) != 0) { ESP_LOGE(TAG, "REQBUFS"); return false; }
    for (int i = 0; i < NBUF; i++) {
        struct v4l2_buffer b = {};
        b.type = type; b.memory = V4L2_MEMORY_MMAP; b.index = i;
        if (ioctl(s_fd, VIDIOC_QUERYBUF, &b) != 0) return false;
        s_len[i] = b.length;
        s_buf[i] = (uint8_t *)mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                   s_fd, b.m.offset);
        if (s_buf[i] == MAP_FAILED) return false;
        if (ioctl(s_fd, VIDIOC_QBUF, &b) != 0) return false;
    }
    int t = type;
    if (ioctl(s_fd, VIDIOC_STREAMON, &t) != 0) { ESP_LOGE(TAG, "STREAMON"); return false; }
    ESP_LOGI(TAG, "streaming, %u KB/buf", (unsigned)(s_len[0] / 1024));
    return true;
}

static void cam_task(void *)
{
    ESP_LOGI(TAG, "=== sferic-tab5 camera spike (ADR-0013 §6) ===");
    if (powerOnCamera() != ESP_OK || videoInit() != ESP_OK || !streamOpen()) {
        ESP_LOGE(TAG, "camera bring-up FAILED");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "camera bring-up PASS — capturing");

    uint32_t frames = 0;
    const int64_t t0 = esp_timer_get_time();
    while (true) {
        struct v4l2_buffer b = {};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (ioctl(s_fd, VIDIOC_DQBUF, &b) != 0) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
        const uint16_t *px = (const uint16_t *)s_buf[b.index];
        const uint32_t n = b.bytesused / 2;
        uint64_t sum = 0, cnt = 0;
        uint16_t lo = 0xFFFF, hi = 0;
        for (uint32_t i = 0; i < n; i += 101) {
            uint16_t p = px[i];
            uint16_t lum = ((p >> 11) & 0x1F) + ((p >> 5) & 0x3F) + (p & 0x1F);
            sum += lum; cnt++;
            if (lum < lo) lo = lum;
            if (lum > hi) hi = lum;
        }
        ioctl(s_fd, VIDIOC_QBUF, &b);
        if (++frames % 30 == 0) {
            const float fps = 1e6f * frames / (esp_timer_get_time() - t0);
            ESP_LOGI(TAG, "frame %lu  %lu B  %.1f fps  lum avg %llu range %u..%u  %s",
                     frames, b.bytesused, fps, (unsigned long long)(sum / cnt), lo, hi,
                     (hi > lo + 3) ? "PASS (live image)" : "flat?");
        }
    }
}

extern "C" void app_main(void)
{
    xTaskCreatePinnedToCore(cam_task, "cam", 8192, NULL, 5, NULL, 1);
}
