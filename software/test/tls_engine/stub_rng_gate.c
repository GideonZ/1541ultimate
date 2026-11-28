/* Host stand-in for tls_rng_gate.c (the radio's entropy gate): the host has no gate, so no draw is ever refused. */
#include "tls_rng_gate.h"
void tls_rng_gate_bind_task(void) {}
int tls_rng_gate_take_refusal(void) { return 0; }
