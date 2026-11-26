/* Stand-in: entropy_source() is radio_entropy.h's. The host draws from the OS, so a source is always there. */
enum { ENTROPY_NONE = 0, ENTROPY_HOST = 1 };
static inline int entropy_source(void) { return ENTROPY_HOST; }
