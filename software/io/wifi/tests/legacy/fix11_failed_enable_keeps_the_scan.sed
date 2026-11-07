# Model: the scan asked for while Disabled before a failed Enable dropped it: it kept waiting for
# the radio, and a later unrelated link loss started it.
# variants: 1 0 2
s/^ *event_when_enabled = 0; \/\/ the radio does not come on: nothing is to follow it$//
