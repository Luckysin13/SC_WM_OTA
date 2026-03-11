#ifndef HARDWARE_PINS_H
#define HARDWARE_PINS_H

#include "config/hardware_config.h"
#include "compat/compat.h"
#include "driver/gpio.h"


// =============================================================================
// HARDWARE PIN INITIALIZATION
// =============================================================================
// Configures GPIO pins for smoker controller hardware
// =============================================================================

namespace HardwarePins {

// Initialize all GPIO pins
inline void init() {
  // Configure reset pin with internal pullup
  // Pull LOW (connect to ground) during boot to trigger factory reset
  gpio_config_t reset_config{};
  reset_config.pin_bit_mask = 1ULL << PIN_RESET_SSID;
  reset_config.mode = GPIO_MODE_INPUT;
  reset_config.pull_up_en = GPIO_PULLUP_ENABLE;
  reset_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  reset_config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&reset_config);

  gpio_config_t led_config{};
  led_config.pin_bit_mask = 1ULL << PIN_WIFI_LED;
  led_config.mode = GPIO_MODE_OUTPUT;
  led_config.pull_up_en = GPIO_PULLUP_DISABLE;
  led_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  led_config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&led_config);
}

// Check if factory reset button is pressed
// Returns true if pin is pulled LOW
inline bool isResetPressed() {
  return (gpio_get_level(static_cast<gpio_num_t>(PIN_RESET_SSID)) == 0);
}

} // namespace HardwarePins

#endif // HARDWARE_PINS_H
