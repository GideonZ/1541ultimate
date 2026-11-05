# Model: the AP list before it followed the radio off: an open list was not refreshed when the
# radio went off, and while the radio off waited for its confirmation it said "Scanning...".
# variants: 1 0 2
s/^ *RefreshAPs(); \/\/ an open AP list shows that$//
s/((state == eWifi_Disabled) || RadioOffAwaited()) ? "WiFi is disabled"/(state == eWifi_Disabled) ? "WiFi is disabled"/
