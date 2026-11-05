# Model: the AP list before a radio off that waits for its confirmation counted as off: a pick
# from a list that still showed APs sent the connect (which turns the radio on again) without
# asking, and the list showed the APs of the last scan.
# variants: 1 0
s/if (((wifi\.getState() == eWifi_Disabled) || wifi\.RadioOffAwaited()) \&\& !NetworkLWIP_WiFi/if ((wifi.getState() == eWifi_Disabled) \&\& !NetworkLWIP_WiFi/
s/^    if (RadioOffAwaited()) {$/    if (RadioOffAwaited() \&\& (aps->num_records == 0)) {/
