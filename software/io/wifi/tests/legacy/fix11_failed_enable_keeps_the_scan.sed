# The scan asked for while Disabled before a failed Enable dropped it: it kept waiting for
# the radio, and a later unrelated link loss started it.
# variants: 1 0 2
s/if (wifi_enable() != 0) {/if ((wifi_enable() != 0) \&\& 0) {/
