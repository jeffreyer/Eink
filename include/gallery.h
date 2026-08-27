#ifndef GALLERY_H
#define GALLERY_H

#include <Arduino.h>
#include <vector>

// 相册模块接口
int gallery_setup(void);
int gallery_unload(void);

// 图片信息结构
struct ImageInfo {
    String filename;
    String path;
    size_t size;
    uint32_t timestamp;
};

// 获取所有图片列表
std::vector<ImageInfo> gallery_list_images();

std::vector<ImageInfo> gallery_get_images();

// 删除图片
bool gallery_delete_image(const char* filename);

// 显示图片
bool gallery_display_image(const char* filename);

// 获取当前图片索引
int gallery_get_current_index();

// 设置当前图片索引
void gallery_set_current_index(int index);

// 模块按键钩子：KEY_DOWN 显示下一张 / KEY_UP 显示上一张
int gallery_next_image(void);
int gallery_prev_image(void);

// 模块定时唤醒钩子：循环模式下返回循环间隔（秒），否则 0
int gallery_wake_interval(void);

// 保存上传的图片数据
bool gallery_save_image(const char* filename, const uint8_t* data, size_t size);

// 设置显示模式 (0=固定显示, 1=循环显示)
void gallery_set_display_mode(int mode);

// 设置循环间隔（分钟，1-1440）
void gallery_set_cycle_interval(int minutes);

// 获取显示模式
int gallery_get_display_mode();

// 获取循环间隔
int gallery_get_cycle_interval();

// 执行循环切换
void gallery_do_cycle();

#endif // GALLERY_H
