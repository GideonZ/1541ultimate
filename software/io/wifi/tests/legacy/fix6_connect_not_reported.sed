# The AP list's Connect before the user was told how it ended: nothing is queued for the
# task to follow, so there is no message and no timeout, and a cancelled password box
# connects with an empty password.
# variants: 1 0 2
s/if (result != 0 || !wifi\.sendConnectPending(bap->ssid)) {/if (0) {/
s/password, 64) <= 0) {/password, 64) < -99) {/
