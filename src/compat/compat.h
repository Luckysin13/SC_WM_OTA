#ifndef COMPAT_H
#define COMPAT_H

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"

class String {
public:
  String() = default;
  String(const char *value) : data_(value ? value : "") {}
  String(const std::string &value) : data_(value) {}
  String(char value) : data_(1, value) {}
  String(int value) { formatInt(value); }
  String(unsigned int value) { formatUInt(value); }
  String(unsigned long value) { formatULong(value); }
  String(long value) { formatLong(value); }
  String(double value, int decimals = 2) { formatDouble(value, decimals); }

  size_t length() const { return data_.length(); }
  bool isEmpty() const { return data_.empty(); }
  const char *c_str() const { return data_.c_str(); }

  int toInt() const { return std::atoi(data_.c_str()); }
  double toDouble() const { return std::atof(data_.c_str()); }

  int indexOf(char ch, size_t fromIndex = 0) const {
    size_t pos = data_.find(ch, fromIndex);
    return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
  }

  int indexOf(const char *substr, size_t fromIndex = 0) const {
    size_t pos = data_.find(substr ? substr : "", fromIndex);
    return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
  }

  int lastIndexOf(char ch) const {
    size_t pos = data_.rfind(ch);
    return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
  }

  int lastIndexOf(const char *substr) const {
    size_t pos = data_.rfind(substr ? substr : "");
    return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
  }

  String substring(size_t start, size_t end = std::string::npos) const {
    if (start >= data_.size()) {
      return String();
    }
    if (end == std::string::npos || end > data_.size()) {
      end = data_.size();
    }
    return String(data_.substr(start, end - start));
  }

  bool startsWith(const char *prefix) const {
    if (!prefix) {
      return false;
    }
    size_t len = std::strlen(prefix);
    return data_.compare(0, len, prefix) == 0;
  }

  bool endsWith(const char *suffix) const {
    if (!suffix) {
      return false;
    }
    size_t len = std::strlen(suffix);
    if (len > data_.size()) {
      return false;
    }
    return data_.compare(data_.size() - len, len, suffix) == 0;
  }

  String &operator=(const String &other) = default;
  String &operator=(const char *value) {
    data_ = value ? value : "";
    return *this;
  }

  String operator+(const String &other) const {
    return String(data_ + other.data_);
  }

  String operator+(const char *other) const {
    return String(data_ + (other ? other : ""));
  }

  String &operator+=(const String &other) {
    data_ += other.data_;
    return *this;
  }

  String &operator+=(const char *other) {
    data_ += other ? other : "";
    return *this;
  }

  bool operator==(const String &other) const { return data_ == other.data_; }
  bool operator!=(const String &other) const { return data_ != other.data_; }
  bool operator==(const char *other) const {
    return data_ == (other ? other : "");
  }
  bool operator!=(const char *other) const { return !(*this == other); }

  std::string str() const { return data_; }

private:
  std::string data_;

  void formatInt(int value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    data_ = buffer;
  }

  void formatUInt(unsigned int value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%u", value);
    data_ = buffer;
  }

  void formatLong(long value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%ld", value);
    data_ = buffer;
  }

  void formatULong(unsigned long value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%lu", value);
    data_ = buffer;
  }

  void formatDouble(double value, int decimals) {
    char format[8];
    std::snprintf(format, sizeof(format), "%%.%df", decimals);
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), format, value);
    data_ = buffer;
  }
};

// Include debug config if available, otherwise default to enabled
#if __has_include("config/debug_config.h")
#include "config/debug_config.h"
#endif

#ifndef DEBUG_ENABLED
#define DEBUG_ENABLED 1
#endif

class SerialClass {
public:
  void begin(unsigned long baud) {
    (void)baud;
  }

#if DEBUG_ENABLED
  void println() { std::printf("\n"); }
  void println(const char *msg) { std::printf("%s\n", msg ? msg : ""); }
  void println(const String &msg) { std::printf("%s\n", msg.c_str()); }

  void print(const char *msg) { std::printf("%s", msg ? msg : ""); }
  void print(const String &msg) { std::printf("%s", msg.c_str()); }

  void printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
  }
#else
  // When DEBUG_ENABLED is 0, all serial output becomes no-ops
  void println() { }
  void println(const char *) { }
  void println(const String &) { }

  void print(const char *) { }
  void print(const String &) { }

  void printf(const char *, ...) { }
#endif
};

inline String operator+(const char *lhs, const String &rhs) {
  return String(std::string(lhs ? lhs : "") + rhs.str());
}

inline SerialClass Serial;

inline void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
inline uint32_t millis() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

class IPAddress {
public:
  IPAddress() { addr_.addr = 0; }
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    IP4_ADDR(&addr_, a, b, c, d);
  }

  bool fromString(const char *str) {
    if (!str) {
      return false;
    }
    return ip4addr_aton(str, &addr_) != 0;
  }

  String toString() const {
    const char *ip = ip4addr_ntoa(&addr_);
    return String(ip ? ip : "0.0.0.0");
  }

  ip4_addr_t raw() const { return addr_; }

private:
  ip4_addr_t addr_{};
};

namespace ESP {
inline void restart() { esp_restart(); }
}

#endif // COMPAT_H
