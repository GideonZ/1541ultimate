/*
 * module_event_stubs.c
 *
 * The calls the event cases make into the module's code (module_event_test.cpp). Included at the
 * end of module_stubs.c.
 */

// The event handler and send_event(), as the cases call them
void module_event(int event, const wifi_event_sta_disconnected_t *data)
{
    sent_n = 0;
    wifi_event_handler(NULL, NULL, event, (void *)data);
}

void module_send_event(uint8_t evcode, uint8_t reason)
{
    sent_n = 0;
    send_event(evcode, reason);
}
