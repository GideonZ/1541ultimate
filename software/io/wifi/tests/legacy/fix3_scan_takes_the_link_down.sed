# The task before "Keep the WiFi interface up while the AP list is scanned": a rescan takes
# the interface down, and the task is not connected after the scan.
# variants: 1 0 2
s/^ *state_before_scan = state;$/                netstack->link_down();/
s/state = state_before_scan;/state = eWifi_NotConnected;/
