# Model: when the asks for a radio off had run out, the module kept sending its frames
# (CMD_MODEM_ON), which the WiFi task took into the interface while the link was held down.
# variants: 1 0 2
/^ *wifi_modem_enable(false);$/d
