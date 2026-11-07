# Model: "WiFi Enabled" before the WiFi task sent the radio requests: the hook sent the radio off
# or on itself, in the caller's task, and armed the wait for the confirmation from there
# (which the task, waiting without a bound, did not see until its next event); a reset of
# the settings did not apply the setting.
# variants: 1 0 2
/^void WiFi :: RadioOn()/,/^}/s/^    radio_setting_due = true;$//
/^void WiFi :: RadioOn()/,/^}/s/^    sendEvent(EVENT_RADIO_SETTING);$/    wifi_switch_on(\&radio_switch); wifi_enable();/
/^void WiFi :: RadioOff()/,/^}/s/^    radio_setting_due = true;$//
/^void WiFi :: RadioOff()/,/^}/s/^    sendEvent(EVENT_RADIO_SETTING);$/    if (!wifi_switch_unsupported(wifi_disable())) { wifi_switch_asked_off(\&radio_switch, xTaskGetTickCount(), pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS)); } else { Disable(); if (uart) { uart->ModuleCtrl(ESP_MODE_OFF); } }/
s/^        apply_enabled(on);$//
