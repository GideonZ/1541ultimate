# The menu before it asked "Enable it?" while Disabled (or, on master, refused): a scan or
# connect action while Disabled goes on without asking and without turning the radio on.
# variants: 1 0 2
s/if ((state == eWifi_Disabled) && !ask_enable(intf)) {/if (0) {/
s/if ((wifi\.getState() == eWifi_Disabled) && !NetworkLWIP_WiFi :: ask_enable(cmd->user_interface)) {/if (0) {/
s/        if (!ask_enable(intf)) {/        if (0) {/
