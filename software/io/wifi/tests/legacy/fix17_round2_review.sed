# Model: before the second review round: the scan's wait for the radio started when "Enable
# it?" was asked, not when the Enable went out; a Disable did not refresh an open AP list until
# the module confirmed it; a failed Enable was taken as applied, so nothing asked again.
# variants: 1 0 2
s/^ *WhenEnabledFromNow();$//
s/wifi\.sendEventWhenEnabled(EVENT_RESCAN); \/\/ the radio is not on yet/wifi.sendEventWhenEnabled(EVENT_RESCAN); wifi.WhenEnabledFromNow();/
s/^                    RefreshAPs();$//
s/^ *radio_setting_applied = -1;$//
