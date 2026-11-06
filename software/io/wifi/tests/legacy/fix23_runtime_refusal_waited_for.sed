# Model: a Disable at runtime that the control module refused for a recovery taken as asked:
# its confirmation is waited for, and the radio off asked for again.
# variants: 2
s/^                } else if (wifi_switch_refused(result = wifi_disable())) {$/                } else if ((result = wifi_disable()), false) {/
