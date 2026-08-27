#include "gallery.h"
#include "common.h"
#ifdef INK6
#include "eink6.h"
#else
#include "Display_EPD_W21.h"
#include "GUI_Paint.h"
#include "eink.h"
#endif
#include <SPIFFS.h>
#include <dirent.h>
#include <sys/stat.h>
#include <Preferences.h>
#include "ble_config.h"
#include "lua_hardware_api.h"

// 墨水屏尺寸定义
#ifdef INK6
#define EPD_WIDTH 240
#define EPD_HEIGHT 240
#else
#define EPD_WIDTH 200
#define EPD_HEIGHT 200
#endif

#define GALLERY_DIR "/spiffs/gallery"
#define MAX_IMAGES 100

// 外部变量
extern unsigned char BlackImage[ALLSCREEN_BYTES];

// 当前显示的图片索引
static int s_current_image_index = 0;
static std::vector<ImageInfo> s_image_list;
static bool s_initialized = false;

// 相册配置
static int s_display_mode = 0;           // 0=固定显示, 1=循环显示
static int s_cycle_interval = 60;        // 循环间隔时间（分钟）1-1440
static int s_rotation = 0;               // 显示方向（0, 90, 180, 270度）
static unsigned long s_last_display_time = 0;  // 上次显示时间

// 前置声明
static bool gallery_display_by_index(int index);
static void gallery_ensure_initialized(void);
static void rotate_image(uint8_t* image, int width, int height, int degrees);

#ifdef INK6
// 旋转图像数据（4位色深位图格式，每字节2个像素，高位在前）
static void rotate_image6(uint8_t* image, int width, int height, int degrees) {
    if (degrees == 0) return;

    int total_bytes = width * height / 2;

    uint8_t* temp = (uint8_t*)malloc(total_bytes);
    if (!temp) {
        Serial.println("Gallery: Failed to allocate rotation buffer");
        return;
    }

    memcpy(temp, image, total_bytes);
    memset(image, 0, total_bytes);

    // 辅助函数：获取像素值（4位）
    auto get_pixel = [temp, width](int x, int y) -> uint8_t {
        int pixel_index = y * width + x;
        int byte_index = pixel_index / 2;
        int nibble_offset = (1 - (pixel_index % 2)) * 4;  // 高位在前
        return (temp[byte_index] >> nibble_offset) & 0x0F;
    };

    // 辅助函数：设置像素值（4位）
    auto set_pixel = [image, width](int x, int y, uint8_t value) {
        int pixel_index = y * width + x;
        int byte_index = pixel_index / 2;
        int nibble_offset = (1 - (pixel_index % 2)) * 4;  // 高位在前
        image[byte_index] |= (value & 0x0F) << nibble_offset;
    };

    if (degrees == 90) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint8_t pixel = get_pixel(x, y);
                set_pixel(height - 1 - y, x, pixel);
            }
        }
    } else if (degrees == 180) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint8_t pixel = get_pixel(x, y);
                set_pixel(width - 1 - x, height - 1 - y, pixel);
            }
        }
    } else if (degrees == 270) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint8_t pixel = get_pixel(x, y);
                set_pixel(y, width - 1 - x, pixel);
            }
        }
    }

    free(temp);
    Serial.printf("Gallery: Image rotated %d degrees (4-bit format)\n", degrees);
}
#endif

// 旋转图像数据（2位色深位图格式，每字节4个像素）
static void rotate_image(uint8_t* image, int width, int height, int degrees) {
    if (degrees == 0) return;

    // 2位色深，每像素2位，每字节4个像素
    int total_bytes = width * height / 4;

    // 创建临时缓冲区
    uint8_t* temp = (uint8_t*)malloc(total_bytes);
    if (!temp) {
        Serial.println("Gallery: Failed to allocate rotation buffer");
        return;
    }

    memcpy(temp, image, total_bytes);
    memset(image, 0, total_bytes);

    // 辅助函数：获取像素值（2位）
    auto get_pixel = [temp, width](int x, int y) -> uint8_t {
        int pixel_index = y * width + x;
        int byte_index = pixel_index / 4;
        int bit_offset = (3 - (pixel_index % 4)) * 2;  // 高位在前
        return (temp[byte_index] >> bit_offset) & 0x03;
    };

    // 辅助函数：设置像素值（2位）
    auto set_pixel = [image, width](int x, int y, uint8_t value) {
        int pixel_index = y * width + x;
        int byte_index = pixel_index / 4;
        int bit_offset = (3 - (pixel_index % 4)) * 2;  // 高位在前
        image[byte_index] |= (value & 0x03) << bit_offset;
    };

    if (degrees == 90) {
        // 90度顺时针旋转: (x,y) -> (height-1-y, x)
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint8_t pixel = get_pixel(x, y);
                int new_x = height - 1 - y;
                int new_y = x;
                set_pixel(new_x, new_y, pixel);
            }
        }
    } else if (degrees == 180) {
        // 180度旋转: (x,y) -> (width-1-x, height-1-y)
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint8_t pixel = get_pixel(x, y);
                int new_x = width - 1 - x;
                int new_y = height - 1 - y;
                set_pixel(new_x, new_y, pixel);
            }
        }
    } else if (degrees == 270) {
        // 270度顺时针旋转: (x,y) -> (y, width-1-x)
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint8_t pixel = get_pixel(x, y);
                int new_x = y;
                int new_y = width - 1 - x;
                set_pixel(new_x, new_y, pixel);
            }
        }
    }

    free(temp);
    Serial.printf("Gallery: Image rotated %d degrees (2-bit format)\n", degrees);
}

// 执行循环切换
void gallery_do_cycle() {
    if (!s_initialized || s_image_list.empty()) {
        return;
    }

    // 切换到下一张图片
    s_current_image_index++;
    if (s_current_image_index >= (int)s_image_list.size()) {
        s_current_image_index = 0;  // 循环到第一张
    }
    save_config_ns("gallery", "img_index", s_current_image_index);

    gallery_display_by_index(s_current_image_index);
    s_last_display_time = millis();  // 更新时间
}

// 模块按键钩子：KEY_DOWN 显示下一张（复用循环切换逻辑，避免重复代码）
int gallery_next_image(void) {
    gallery_ensure_initialized();
    gallery_do_cycle();
    return 0;
}

// 模块按键钩子：KEY_UP 显示上一张
int gallery_prev_image(void) {
    gallery_ensure_initialized();
    if (!s_initialized || s_image_list.empty()) {
        return 0;
    }

    s_current_image_index--;
    if (s_current_image_index < 0) {
        s_current_image_index = (int)s_image_list.size() - 1;  // 回绕到最后一张
    }
    save_config_ns("gallery", "img_index", s_current_image_index);
    gallery_display_by_index(s_current_image_index);
    s_last_display_time = millis();
    return 0;
}

// 模块定时唤醒钩子：循环播放时按循环间隔定时唤醒切换，否则不启用定时唤醒
int gallery_wake_interval(void) {
    // 30 秒断电快路径在模块 setup 前查询：静态配置尚未加载，直接从 NVS 读取。
    // 此时必然刚完成一次图片刷新（存在图片），因此不检查图片列表
    if (!s_initialized) {
        int display_mode = load_config_ns("gallery", "display_mode");
        int cycle_interval = load_config_ns("gallery", "cycle_interval");
        if (cycle_interval < 1 || cycle_interval > 1440) {
            cycle_interval = 60;
        }
        return display_mode == 1 ? cycle_interval * 60 : 0;
    }

    if (s_display_mode == 1 && !s_image_list.empty()) {
        return s_cycle_interval * 60;  // 分钟 → 秒
    }
    return 0;
}

// 配置管理函数
// 设置显示模式
void gallery_set_display_mode(int mode) {
    if (mode >= 0 && mode <= 1) {
        s_display_mode = mode;
        s_last_display_time = millis();  // 重置计时
        Serial.printf("Gallery: Display mode set to %d\n", mode);
    }
}

// 设置循环间隔
void gallery_set_cycle_interval(int minutes) {
    if (minutes >= 1 && minutes <= 1440) {
        s_cycle_interval = minutes;
        s_last_display_time = millis();  // 重置计时
        Serial.printf("Gallery: Cycle interval set to %d minutes\n", minutes);
    }
}

// 获取显示模式
int gallery_get_display_mode() {
    return s_display_mode;
}

// 获取循环间隔
int gallery_get_cycle_interval() {
    return s_cycle_interval;
}

std::vector<ImageInfo> gallery_get_images() {
    return s_image_list;
}

// 扫描相册目录，获取所有图片
std::vector<ImageInfo> gallery_list_images() {
    std::vector<ImageInfo> images;

    DIR* dir = opendir(GALLERY_DIR);
    if (!dir) {
        return images;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        String filename = String(entry->d_name);

        // 只处理.img文件
        if (filename.endsWith(".img")) {
            String full_path = String(GALLERY_DIR) + "/" + filename;

            struct stat st;
            if (stat(full_path.c_str(), &st) == 0) {
                ImageInfo info;
                info.filename = filename;
                info.path = full_path;
                info.size = st.st_size;
                info.timestamp = st.st_mtime;

                images.push_back(info);
            }
        }
    }

    closedir(dir);

    // 按时间戳排序（最新的在前）
    std::sort(images.begin(), images.end(), [](const ImageInfo& a, const ImageInfo& b) {
        return a.timestamp > b.timestamp;
    });

    Serial.printf("Gallery: Found %d images\n", images.size());
    return images;
}

// 删除图片
bool gallery_delete_image(const char* filename) {
    String full_path = String(GALLERY_DIR) + "/" + filename;

    if (remove(full_path.c_str()) == 0) {
        Serial.printf("Gallery: Deleted %s\n", filename);

        // 重新扫描图片列表
        s_image_list = gallery_list_images();

        // 调整当前索引
        if (s_current_image_index >= (int)s_image_list.size()) {
            s_current_image_index = s_image_list.size() - 1;
        }
        if (s_current_image_index < 0) {
            s_current_image_index = 0;
        }

        return true;
    }

    Serial.printf("Gallery: Failed to delete %s\n", filename);
    return false;
}

// 显示图片
bool gallery_display_image(const char* filename) {
    String full_path = String(GALLERY_DIR) + "/" + filename;

    FILE* fp = fopen(full_path.c_str(), "r");
    if (!fp) {
        Serial.printf("Gallery: Failed to open %s\n", full_path.c_str());
        return false;
    }

    size_t read_size = fread(BlackImage, 1, ALLSCREEN_BYTES, fp);
    fclose(fp);

    if (read_size != ALLSCREEN_BYTES) {
        Serial.printf("Gallery: Invalid image size: %d (expected %d)\n", read_size, ALLSCREEN_BYTES);
        return false;
    }

    if (ble_config_is_enabled()) {
        s_display_mode = load_config_ns("gallery", "display_mode");
        s_cycle_interval = load_config_ns("gallery", "cycle_interval");
        s_rotation = load_config_ns("gallery", "rotation");
    }

    #ifdef INK6
    if (s_rotation != 0) {
        rotate_image6(BlackImage, EPD_WIDTH, EPD_HEIGHT, s_rotation);
    }
    epdDisplayImage(BlackImage,sizeof(BlackImage));

    #else
    // 应用旋转
    if (s_rotation != 0) {
        rotate_image(BlackImage, EPD_WIDTH, EPD_HEIGHT, s_rotation);
    }

    EPD_init_Fast2();
    PIC_display(BlackImage);
    EPD_sleep();
    #endif

    Serial.printf("Gallery: Displayed %s (rotation: %d deg)\n", filename, s_rotation);
    return true;
}

// 按索引显示图片
static bool gallery_display_by_index(int index) {
    if (index < 0 || index >= (int)s_image_list.size()) {
        return false;
    }

    s_current_image_index = index;
    return gallery_display_image(s_image_list[index].filename.c_str());
}

// 获取当前图片索引
int gallery_get_current_index() {
    return s_current_image_index;
}

// 设置当前图片索引
void gallery_set_current_index(int index) {
    if (index >= 0 && index < (int)s_image_list.size()) {
        s_current_image_index = index;
    }
}

// 保存上传的图片数据
bool gallery_save_image(const char* filename, const uint8_t* data, size_t size) {
    if (size != ALLSCREEN_BYTES) {
        Serial.printf("Gallery: Invalid image size: %d (expected %d)\n", size, ALLSCREEN_BYTES);
        return false;
    }

    // 确保目录存在
    struct stat st;
    if (stat(GALLERY_DIR, &st) != 0) {
        mkdir(GALLERY_DIR, 0755);
    }

    String full_path = String(GALLERY_DIR) + "/" + filename;

    FILE* fp = fopen(full_path.c_str(), "w");
    if (!fp) {
        Serial.printf("Gallery: Failed to create %s\n", full_path.c_str());
        return false;
    }

    size_t written = fwrite(data, 1, size, fp);
    fclose(fp);

    if (written != size) {
        Serial.printf("Gallery: Failed to write complete data\n");
        remove(full_path.c_str());
        return false;
    }

    Serial.printf("Gallery: Saved %s (%d bytes)\n", filename, size);

    // 重新扫描图片列表
    s_image_list = gallery_list_images();

    return true;
}

// 初始化相册状态（配置 + 图片列表），可重复安全调用；
// 按键唤醒跳过 setup 绘制后，上一张/下一张钩子内部懒初始化
static void gallery_ensure_initialized(void) {
    if (s_initialized) {
        return;
    }

    // 从common.cpp的配置系统加载配置
    s_display_mode = load_config_ns("gallery", "display_mode");
    s_cycle_interval = load_config_ns("gallery", "cycle_interval");
    s_rotation = load_config_ns("gallery", "rotation");
    s_current_image_index = load_config_ns("gallery", "img_index");

    // 使用默认值如果配置为0
    if (s_cycle_interval == 0) {
        s_cycle_interval = 60;
    }

    // 验证范围
    if (s_display_mode < 0 || s_display_mode > 1) {
        s_display_mode = 0;
    }
    if (s_cycle_interval < 1 || s_cycle_interval > 1440) {
        s_cycle_interval = 60;
    }
    if (s_rotation != 0 && s_rotation != 90 && s_rotation != 180 && s_rotation != 270) {
        s_rotation = 0;
    }

    Serial.printf("Gallery config: mode=%d, interval=%d min, rotation=%d deg\n",
                  s_display_mode, s_cycle_interval, s_rotation);

    // 确保SPIFFS已挂载
    if (!SPIFFS.begin(true)) {
        Serial.println("Gallery: SPIFFS mount failed");
        return;
    }

    // 扫描图片列表
    s_image_list = gallery_list_images();
    s_initialized = true;
    s_last_display_time = millis();  // 初始化时间
}

// 模块初始化：统一语义——上电/定时唤醒/切换进入时绘制"当前应显示的内容"。
// 循环模式每次绘制下一张（定时唤醒即切换），固定模式显示已保存的当前图片。
// 按键唤醒不经过 setup（由系统层跳过，按键钩子直接处理）。
// 无图片提示：UTF-8 安全按字符换行，size 4（24x24）逐行居中绘制
static void gallery_draw_empty_prompt(void) {
    lua_hardware_clear_canvas();

    const char* prompt = "请上传图片：长按上键等待蓝色指示灯亮起，通过小程序“幻彩抽屉”连接上传图片。";
    const int cell = 24;                       // size 4 中文 24x24
    const int margin = 12;
    const int max_chars = (EPD_WIDTH - margin * 2) / cell;  // 每行最多字符
    const int line_h = cell + 6;               // 行距

    // 统计总字符数（UTF-8 安全），用于垂直居中
    int total_chars = 0;
    for (const char* q = prompt; *q;) {
        uint8_t b = (uint8_t)*q++;
        if (b < 0x80) {
            total_chars++;
        } else if ((b & 0xE0) == 0xC0) {
            q++;
            total_chars++;
        } else if ((b & 0xF0) == 0xE0) {
            q += 2;
            total_chars++;
        } else if ((b & 0xF8) == 0xF0) {
            q += 3;
            total_chars++;
        } else {
            total_chars++;
        }
    }

    int total_lines = (total_chars + max_chars - 1) / max_chars;
    int y = (EPD_HEIGHT - total_lines * line_h) / 2 + 4;

    const char* p = prompt;
    while (*p) {
        char line_buf[64];
        int n = 0;
        int chars_in_line = 0;
        while (*p && chars_in_line < max_chars) {
            uint8_t b = (uint8_t)*p;
            int len = (b < 0x80) ? 1 : ((b & 0xE0) == 0xC0 ? 2 : ((b & 0xF0) == 0xE0 ? 3 : 4));
            if (n + len >= (int)sizeof(line_buf)) {
                break;
            }
            memcpy(line_buf + n, p, len);
            n += len;
            p += len;
            chars_in_line++;
        }
        line_buf[n] = '\0';
        int line_w = chars_in_line * cell;
        int x = (EPD_WIDTH - line_w) / 2;
        lua_hardware_draw_utf8(x, y, line_buf, 4, 0);
        y += line_h;
    }
}

int gallery_setup(void) {
    Serial.println("Gallery: Initializing...");
    gallery_ensure_initialized();

    if (!s_image_list.empty()) {
        if (s_display_mode == 1) {
            // 循环模式：每次进入/定时唤醒显示下一张
            Serial.printf("Gallery: Cycle mode enabled, displaying next image\n");
            s_current_image_index++;
            if (s_current_image_index >= (int)s_image_list.size()) {
                s_current_image_index = 0;  // 循环到第一张
            }
            gallery_display_by_index(s_current_image_index);
            save_config_ns("gallery", "img_index", s_current_image_index);
        } else {
            // 固定模式：显示当前图片（基于已保存的 img_index）
            if (s_current_image_index < 0 || s_current_image_index >= (int)s_image_list.size()) {
                s_current_image_index = 0;
            }
            gallery_display_by_index(s_current_image_index);
        }
    } else {
        // 无图片：清空画布并居中显示提示（中文走 GB2312 字库，随全局显示方向旋转）
        gallery_draw_empty_prompt();
#ifdef INK6
        epdDisplayImage(BlackImage, ALLSCREEN_BYTES);
#else
        EPD_init_Fast2();
        PIC_display(BlackImage);
        EPD_sleep();
#endif
    }

    Serial.printf("Gallery: Initialized with %d images\n", s_image_list.size());
    return 0;
}

// 模块卸载
int gallery_unload(void) {
    Serial.println("Gallery: Unloading...");

    s_initialized = false;
    s_image_list.clear();
    s_current_image_index = 0;
    return 0;
}
