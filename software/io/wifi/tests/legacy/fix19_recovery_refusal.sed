# Model: the radio off refused by the control module during a recovery taken as any other
# error: waited for, asked for again, and reported to the user as not done.
# variants: 2
1a\
#define wifi_switch_refused(result) ((void)(result), false)
