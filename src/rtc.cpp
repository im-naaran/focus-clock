#include "rtc.h"

#include "config.h"

using namespace AppConfig;

static void ds1302WriteByte(uint8_t value) {
  pinMode(PIN_DS1302_IO, OUTPUT);
  for (uint8_t i = 0; i < 8; ++i) {
    digitalWrite(PIN_DS1302_IO, value & 0x01);
    digitalWrite(PIN_DS1302_SCLK, HIGH);
    delayMicroseconds(2);
    digitalWrite(PIN_DS1302_SCLK, LOW);
    delayMicroseconds(2);
    value >>= 1;
  }
}

static uint8_t ds1302ReadByte() {
  uint8_t value = 0;
  pinMode(PIN_DS1302_IO, INPUT);
  for (uint8_t i = 0; i < 8; ++i) {
    if (digitalRead(PIN_DS1302_IO)) {
      value |= (1 << i);
    }
    digitalWrite(PIN_DS1302_SCLK, HIGH);
    delayMicroseconds(2);
    digitalWrite(PIN_DS1302_SCLK, LOW);
    delayMicroseconds(2);
  }
  return value;
}

static void ds1302IdleIo() {
  pinMode(PIN_DS1302_IO, OUTPUT);
  digitalWrite(PIN_DS1302_IO, LOW);
}

static uint8_t fromBcd(uint8_t value) {
  return ((value >> 4) * 10) + (value & 0x0F);
}

static uint8_t toBcd(uint8_t value) {
  return ((value / 10) << 4) | (value % 10);
}

static bool bcdValid(uint8_t value) {
  return (value & 0x0F) <= 9 && ((value >> 4) & 0x0F) <= 9;
}

static uint8_t ds1302ReadRegister(uint8_t reg) {
  digitalWrite(PIN_DS1302_CE, LOW);
  digitalWrite(PIN_DS1302_SCLK, LOW);
  digitalWrite(PIN_DS1302_CE, HIGH);
  delayMicroseconds(4);
  ds1302WriteByte(0x81 | ((reg & 0x1F) << 1));
  const uint8_t value = ds1302ReadByte();
  digitalWrite(PIN_DS1302_CE, LOW);
  ds1302IdleIo();
  delayMicroseconds(4);
  return value;
}

static void ds1302WriteRegister(uint8_t reg, uint8_t value) {
  digitalWrite(PIN_DS1302_CE, LOW);
  digitalWrite(PIN_DS1302_SCLK, LOW);
  digitalWrite(PIN_DS1302_CE, HIGH);
  delayMicroseconds(4);
  ds1302WriteByte(0x80 | ((reg & 0x1F) << 1));
  ds1302WriteByte(value);
  digitalWrite(PIN_DS1302_CE, LOW);
  ds1302IdleIo();
  delayMicroseconds(4);
}

static bool fieldsValid(const RtcTime &time) {
  return time.second < 60 &&
         time.minute < 60 &&
         time.hour < 24 &&
         time.date >= 1 && time.date <= 31 &&
         time.month >= 1 && time.month <= 12 &&
         time.day >= 1 && time.day <= 7 &&
         time.year >= 2000 && time.year <= 2099;
}

static bool rawTimeRegistersValid(uint8_t rawSecond,
                                  uint8_t rawMinute,
                                  uint8_t rawHour,
                                  uint8_t rawDate,
                                  uint8_t rawMonth,
                                  uint8_t rawDay,
                                  uint8_t rawYear) {
  return (rawSecond & 0x80) == 0 &&
         bcdValid(rawSecond & 0x7F) &&
         bcdValid(rawMinute & 0x7F) &&
         bcdValid(rawHour & 0x3F) &&
         bcdValid(rawDate & 0x3F) &&
         bcdValid(rawMonth & 0x1F) &&
         bcdValid(rawDay & 0x07) &&
         bcdValid(rawYear);
}

static bool bcdSecondWithinWriteWindow(uint8_t rawSecond, uint8_t expectedSecond) {
  if ((rawSecond & 0x80) != 0) {
    return false;
  }
  if (!bcdValid(rawSecond & 0x7F)) {
    return false;
  }
  const uint8_t actual = fromBcd(rawSecond & 0x7F);
  return actual == expectedSecond || actual == static_cast<uint8_t>((expectedSecond + 1) % 60);
}

void rtcBegin() {
  pinMode(PIN_DS1302_CE, OUTPUT);
  pinMode(PIN_DS1302_SCLK, OUTPUT);
  pinMode(PIN_DS1302_IO, OUTPUT);
  digitalWrite(PIN_DS1302_CE, LOW);
  digitalWrite(PIN_DS1302_SCLK, LOW);
  digitalWrite(PIN_DS1302_IO, LOW);
}

bool rtcReadTime(RtcTime &time) {
  const uint8_t rawSecond = ds1302ReadRegister(0x00);
  const uint8_t rawMinute = ds1302ReadRegister(0x01);
  const uint8_t rawHour = ds1302ReadRegister(0x02);
  const uint8_t rawDate = ds1302ReadRegister(0x03);
  const uint8_t rawMonth = ds1302ReadRegister(0x04);
  const uint8_t rawDay = ds1302ReadRegister(0x05);
  const uint8_t rawYear = ds1302ReadRegister(0x06);

  time.second = fromBcd(rawSecond & 0x7F);
  time.minute = fromBcd(rawMinute & 0x7F);
  time.hour = fromBcd(rawHour & 0x3F);
  time.date = fromBcd(rawDate & 0x3F);
  time.month = fromBcd(rawMonth & 0x1F);
  time.day = fromBcd(rawDay & 0x07);
  time.year = 2000 + fromBcd(rawYear);
  time.valid = rawTimeRegistersValid(rawSecond,
                                     rawMinute,
                                     rawHour,
                                     rawDate,
                                     rawMonth,
                                     rawDay,
                                     rawYear) &&
               fieldsValid(time);
  return time.valid;
}

RtcRawRegisters rtcReadRawRegisters() {
  RtcRawRegisters raw;
  raw.second = ds1302ReadRegister(0x00);
  raw.minute = ds1302ReadRegister(0x01);
  raw.hour = ds1302ReadRegister(0x02);
  raw.date = ds1302ReadRegister(0x03);
  raw.month = ds1302ReadRegister(0x04);
  raw.day = ds1302ReadRegister(0x05);
  raw.year = ds1302ReadRegister(0x06);
  ds1302IdleIo();
  raw.control = ds1302ReadRegister(0x07);
  ds1302IdleIo();
  return raw;
}

bool rtcSetTime(const RtcTime &time) {
  if (!fieldsValid(time)) {
    return false;
  }

  ds1302WriteRegister(0x07, 0x00);
  ds1302WriteRegister(0x00, toBcd(time.second) & 0x7F);
  ds1302WriteRegister(0x01, toBcd(time.minute));
  ds1302WriteRegister(0x02, toBcd(time.hour));
  ds1302WriteRegister(0x03, toBcd(time.date));
  ds1302WriteRegister(0x04, toBcd(time.month));
  ds1302WriteRegister(0x05, toBcd(time.day));
  ds1302WriteRegister(0x06, toBcd(static_cast<uint8_t>(time.year - 2000)));
  ds1302WriteRegister(0x07, 0x80);
  ds1302IdleIo();

  delay(20);
  const bool ok = bcdSecondWithinWriteWindow(ds1302ReadRegister(0x00), time.second) &&
                  ds1302ReadRegister(0x01) == toBcd(time.minute) &&
                  ds1302ReadRegister(0x02) == toBcd(time.hour) &&
                  ds1302ReadRegister(0x03) == toBcd(time.date) &&
                  ds1302ReadRegister(0x04) == toBcd(time.month) &&
                  ds1302ReadRegister(0x05) == toBcd(time.day) &&
                  ds1302ReadRegister(0x06) == toBcd(static_cast<uint8_t>(time.year - 2000));
  ds1302IdleIo();
  return ok;
}
