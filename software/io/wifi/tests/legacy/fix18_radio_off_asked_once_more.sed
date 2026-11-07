# Model: the wait for a radio off's confirmation before it was bounded by a count: at the
# deadline the radio off was asked for once more, the wait ended, and the module's link
# events were taken again; the user was not told.
# variants: 1 0
s/^\( *\)\(} else \)\{0,1\}if (wifi_switch_ask_again(&radio_switch, .*)) {$/\1\2if (1) {/
/UserInterface :: postMessage("WiFi module did not turn its radio off");/d
