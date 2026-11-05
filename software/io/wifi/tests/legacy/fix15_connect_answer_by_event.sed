# Model: the connect feedback before the module's answer was left in the WiFi object: the answer
# went only as an event, lost when the task's queue was full, and an acceptance did not time
# the outcome; any join ended the pick; a bare EVENT_DISCONNECTED for the radio start of a
# module without the reason field was taken for the outcome.
# variants: 1 0 2
s/^    connect_answered = attempt;$/    if (sendEvent(EVENT_CONNECT_ANSWER)) { connect_answered = attempt; }/
s/^                    feedback\.accepted(answer_at);$//
s/feedback\.onConnected(ev2->ssid);/feedback.cancel();/
s/(state == eWifi_Disabled) || RadioOffAwaited());$/false);/
