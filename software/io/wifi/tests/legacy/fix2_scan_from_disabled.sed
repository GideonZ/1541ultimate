# The Elite II / C64 Ultimate task before "do not scan from Disabled on the control module":
# a rescan asked for while Disabled is sent on (and starts the radio).
# variants: 2
s/if ((state == eWifi_Disabled) || RadioOffAwaited()) {/if (0) {/
