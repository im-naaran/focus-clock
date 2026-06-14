#pragma once

#include <Arduino.h>

void displayBegin();
void displayClear();
void displayInvalidateCache();
void displaySetContrast(uint8_t contrast);

void displayPrintLine(uint8_t page, const char *text);
void displayPrintLineCentered(uint8_t page, const char *text);
void displayPrintScaledLineCentered(uint8_t page, const char *text, uint8_t scale);
void displayDrawDialog(const char *message);
