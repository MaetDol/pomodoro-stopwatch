#include "pomodoro.h"

static bool enableRtcWakePin(uint8_t pin) {
  gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  if (!rtc_gpio_is_valid_gpio(gpio)) {
    return false;
  }
  rtc_gpio_pullup_en(gpio);
  rtc_gpio_pulldown_dis(gpio);
  return gpio_wakeup_enable(gpio, GPIO_INTR_LOW_LEVEL) == ESP_OK;
}

void configureLightSleepWakeup() {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);

  bool anyConfigured = false;
  anyConfigured |= enableRtcWakePin(ENC_BTN);
  anyConfigured |= enableRtcWakePin(ENC_A);
  anyConfigured |= enableRtcWakePin(ENC_B);

  if (anyConfigured) {
    esp_err_t err = esp_sleep_enable_gpio_wakeup();
    if (err != ESP_OK) {
      Serial.print("Failed to enable GPIO wakeup: ");
      Serial.println(err);
    }
  } else {
    Serial.println("No RTC-capable pins available for GPIO wakeup.");
  }
}

void tftEnterSleepSeqSoftOnly(){
  gDisplay.isAwake = false;
  tft.startWrite(); tft.sendCommand(0x28); tft.endWrite(); // DISPOFF
  delay(10);
  tft.startWrite(); tft.sendCommand(0x10); tft.endWrite(); // SLPIN
  delay(120);
}

void tftExitSleepSeqSoftOnly(){
  gDisplay.isAwake = true;
  tft.startWrite(); tft.sendCommand(0x11); tft.endWrite(); // SLPOUT
  delay(120);
  tft.startWrite(); tft.sendCommand(0x29); tft.endWrite(); // DISPON
}

