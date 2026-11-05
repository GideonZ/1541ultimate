# Model: the menu's connect actions asked "Enable it?" only in state Disabled: once the asks
# for a radio off had run out (Link Down, the radio off still awaited) they sent their
# request straight away, which went nowhere.
# variants: 1 0 2
s/((state == eWifi_Disabled) || wifi.RadioOffAwaited())/(state == eWifi_Disabled)/
