#include <assert.h>

#define REQUIRE(expr) do { \
    bool require_ok = (expr); \
    if (!require_ok) { \
        printf("%s: ASSERT FAILED %s:%d: %s\n", testname, __FILE__, __LINE__, #expr); \
    } \
    assert(require_ok); \
} while (0)
