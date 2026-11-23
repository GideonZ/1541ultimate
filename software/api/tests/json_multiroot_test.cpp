#include "../../io/usb/tests/host_test/host_test.h"
#include "../json.h"

#include <string.h>

extern "C" void outbyte(int)
{
}

// A body with several top-level values (e.g. POST /v1/configs with "{}{}{}")
// is malformed for every caller: they take one root and delete only that.
// jsmn gives every top-level value parent -1, so convert() used to build all
// of them, return the first and drop the rest. Build with
// -fsanitize=address and ASAN_OPTIONS=detect_leaks=1 (make json-leak) to see
// the leak itself; without the sanitizer the NULL result is what fails.

static void expect_rejected(const char *body)
{
    char text[256];
    strncpy(text, body, sizeof(text) - 1);
    text[sizeof(text) - 1] = 0;
    JSON *obj = NULL;
    int t = convert_text_to_json_objects(text, strlen(text), 1024, &obj);
    EXPECT_TRUE(t < 0);
    EXPECT_TRUE(obj == NULL);
    delete obj; // what route_configs.cc does with the result
}

TEST(JsonMultiRootTest, SeveralObjectsAreRejectedAndNothingLeaks)
{
    expect_rejected("{\"a\":1}{\"b\":\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"}[1,2,3]");
}

TEST(JsonMultiRootTest, TrailingStringIsRejectedAndNothingLeaks)
{
    expect_rejected("{} \"a string that would be copied and never freed\"");
}

TEST(JsonMultiRootTest, SingleRootStillParses)
{
    char text[] = "{\"a\":[1,2,{\"b\":\"c\"}],\"d\":true}";
    JSON *obj = NULL;
    int t = convert_text_to_json_objects(text, strlen(text), 1024, &obj);
    EXPECT_TRUE(t > 0);
    ASSERT_TRUE(obj != NULL);
    delete obj;
}
