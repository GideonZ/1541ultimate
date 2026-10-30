# The WiFi application before "Turn the radio off with Disable on the U64 and U2+L, and make
# WiFi Enabled work": no radio switch. The module is started by the setting only, never told
# to turn its radio off or on at boot, and the menu's Disable and Enable stop and start the
# application. Applied by the Makefile to the extracted code (make red).
# variants: 1 0
s/^    wifi\.Enable();$/#if U64 == 2\
    wifi.Enable();\
#endif/
s/if (wifi_switch_wants_off(&radio_switch, netstack->enabled_in_config())) {/if (0) {/
s/^            wifi_enable();$//
s/^    wifi\.RadioOff();$/    wifi.Disable();/
s/^    wifi\.RadioOn();$/    wifi.Enable();/
