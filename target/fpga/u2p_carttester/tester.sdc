create_clock -name input_clock -period 20 [get_ports {RMII_REFCLK}]
create_clock -name cart_clock -period 40 [get_ports {SLOT_PHI2}]

