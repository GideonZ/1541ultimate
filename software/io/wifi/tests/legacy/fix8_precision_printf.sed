# Model: the AP list's Connect before its failure message was built by wifi_connect_failed_text: the
# SSID was printed with "%.32s", which the firmware's printf does not take (the format is
# printed as it is; with FP_SUPPORT, ".3" is read as a precision and the SSID is cut to two
# characters).
# variants: 1 0 2
s/^        wifi_connect_failed_text(msg, sizeof(msg), bap->ssid);$/        snprintf(msg, sizeof(msg), "Could not connect to %.32s", bap->ssid);/
