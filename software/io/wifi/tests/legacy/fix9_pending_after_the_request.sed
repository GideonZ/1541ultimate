# Model: the AP list's Connect before the task noted the attempt ahead of the request: the attempt
# was queued for the task after the module had accepted the request, behind any outcome
# the module reported in the meantime (which then left it pending, to time out as a
# failure), and a refused request was told by the user interface itself. The module's
# EVENT_DISCONNECTED for its radio start came before the attempt then, and was not told
# from an outcome.
# variants: 1 0 2
s/if (!wifi\.sendConnectEvent(EVENT_CONNECT_PENDING, bap->ssid)) {/if (0) {/
s/^    if (result != 0) {$/    if ((result != 0) || !wifi.sendConnectEvent(EVENT_CONNECT_PENDING, bap->ssid)) { char msg[64]; wifi_connect_failed_text(msg, sizeof(msg), bap->ssid); UserInterface :: postMessage(msg); } if (0) {/
s/if (!wifi_disconnect_is_radio_start(buf->data, buf->size) \&\&$/if (/
