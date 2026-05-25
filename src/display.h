#pragma once

#include <Arduino.h>

// SSD1306 128x64 显示辅助接口。
// 屏幕按 8 个 page 寻址，每个 page 高 8 像素。
void displayBegin();
void displayClear();

// 全屏清除或页面模式切换后，需要同步清空文本缓存。
void displayInvalidateCache();

// 打印一行带缓存的 5x7 文本；超过可见宽度的内容会被截断。
void displayPrintLine(uint8_t page, const char *text);
void displayPrintLineCentered(uint8_t page, const char *text);

// 居中绘制放大的 5x7 文本，占用连续两个 page；当前用于主时间显示。
void displayPrintScaledLineCentered(uint8_t page, const char *text, uint8_t scale);
