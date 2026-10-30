# The AP list before EVENT_DISABLED emptied it: the list of the last scan stays pickable.
# variants: 1 0 2
s/^ *wifi_aps\.num_records = 0; \/\/ nothing to pick while the radio is off$//
