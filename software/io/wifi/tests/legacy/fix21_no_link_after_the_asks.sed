# Model: when the asks for a radio off had run out, the user was told and the link state was
# taken from the module: with "WiFi Enabled" stored as Disabled and a module whose radio
# stayed on, the interface came up and stayed up, and nothing asked again.
# variants: 1 0 2
/UserInterface :: postMessage("WiFi module did not turn its radio off");/a\
                        wifi_switch_on(&radio_switch); state = eWifi_NotConnected; if ((wifi_is_connected(conn) == 0) && conn) { wifi_modem_enable(true); netstack->link_up(); state = eWifi_Connected; } RefreshRoot(); break;
