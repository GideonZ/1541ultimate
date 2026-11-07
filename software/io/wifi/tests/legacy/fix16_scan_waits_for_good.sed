# Model: a scan that waited for the radio before its wait was bounded: an Enable the module
# queued without starting its radio left it waiting, and a much later link loss started it.
# variants: 1 0 2
s/if ((int32_t)(event_when_enabled_by - xTaskGetTickCount()) > 0) {/if (1) {/
