# Model: a Disabled set by a reset or load of the settings (no change hook) says nothing of
# Wake on Wi-Fi; the effectuate applies it silently.
# variants: 2
/^            UserInterface :: postMessage("Wake on Wi-Fi needs WiFi enabled");$/d
