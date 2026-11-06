# Model: a refusal of the control module (a recovery) to a radio off asked for again at a
# deadline taken as any other answer: the asks go on, and the user is told of a failure.
# variants: 2
s/^                    if (wifi_switch_refused(wifi_disable())) {$/                    if ((wifi_disable(), false)) {/
