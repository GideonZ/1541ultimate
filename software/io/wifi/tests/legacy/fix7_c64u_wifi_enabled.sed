# The Elite II / C64 Ultimate before "WiFi Enabled" was stored there: the menu's Enable and
# Disable (and the Yes of "Enable it?") only ask the module and set and save nothing, and the
# module is not told the setting when it is found.
# variants: 2
s/^    set_enabled(false);$/    wifi.RadioOff();/
s/^    set_enabled(true);$/    wifi.RadioOn();/
s/if (wifi_switch_wants_off(&radio_switch, radio_setting_applied != 0) \&\&/if (0 \&\&/
s/^                result = wifi_enable();$/                result = 0;/
