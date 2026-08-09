#pragma once

#include <Arduino.h>

void displayBegin();
void displayClear();
void displayInvalidateCache();
void displaySetContrast(uint8_t contrast);
void displaySleep();
void displayWake();

void displayPrintLine(uint8_t page, const char *text);
void displayPrintLineCentered(uint8_t page, const char *text);
void displayPrintScaledLineCentered(uint8_t page, const char *text, uint8_t scale);
void displayDrawDialog(const char *message);

void displayWritePageBitmap(uint8_t x,
                            uint8_t firstPage,
                            uint8_t width,
                            uint8_t pageCount,
                            const uint8_t *data);
void displayClearPageRegion(uint8_t x,
                            uint8_t firstPage,
                            uint8_t width,
                            uint8_t pageCount);
void displaySetWifiConnectedIcon(bool visible);
