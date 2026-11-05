# Model: the radio setting before it was a mark for the WiFi task: a change whose event did not
# fit in the task's queue was lost; an Enable while a module without the switch answered the
# radio off was followed by the module held off; every effectuate of the settings (each close
# of the WiFi settings page) sent a request, and the one in the task was skipped.
# variants: 1 0 2
s/^    sendEvent(EVENT_RADIO_SETTING);$/    if (!sendEvent(EVENT_RADIO_SETTING)) { radio_setting_due = false; }/
s/^                    if (!netstack->enabled_in_config()) {$/                    if (1) {/
s/^                    if (netstack->enabled_in_config()) {$/                    if (0) {/
s/if (wifi\.RadioSettingChanged(on)) {/if (1) {/
