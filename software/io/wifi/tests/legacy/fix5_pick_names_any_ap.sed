# The AP list's Connect before it sent the AP picked: the request names the SSID only, and
# carries no BSSID or channel.
# variants: 1 0 2
s/wifi_wifi_connect(bap->ssid, password, bap->auth, bap->bssid, bap->channel)/wifi_wifi_connect(bap->ssid, password, bap->auth)/
s/memcpy(args->bssid, bssid, 6);//
