# The Elite II / C64 Ultimate before "WiFi Enabled" was stored there: the menu's Enable and
# Disable (and the Yes of "Enable it?") only ask the module and set and save nothing, and the
# module is not told the setting when it is found.
# variants: 2
s/^    set_enabled(false);$/    wifi.RadioOff();/
s/^    set_enabled(true);$/    wifi.RadioOn();/
s/^                    wifi_enable();$//
s/^                    wifi_disable();$//
