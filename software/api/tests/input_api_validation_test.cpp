#include "../../io/usb/tests/host_test/host_test.h"
#include "../input_api.h"

#include <initializer_list>
#include <string>

extern "C" void outbyte(int)
{
}

namespace {

JSON_List *make_string_list(std::initializer_list<const char *> values)
{
    JSON_List *list = JSON::List();
    for (std::initializer_list<const char *>::const_iterator it = values.begin(); it != values.end(); ++it) {
        list->add(*it);
    }
    return list;
}

JSON_Object *make_keyboard_event(const char *transition, std::initializer_list<const char *> inputs)
{
    return JSON::Obj()
        ->add("kind", "keyboard")
        ->add("inputs", make_string_list(inputs))
        ->add("transition", transition);
}

JSON_Object *make_joystick_event(int port, const char *transition, std::initializer_list<const char *> inputs)
{
    return JSON::Obj()
        ->add("kind", "joystick")
        ->add("port", port)
        ->add("inputs", make_string_list(inputs))
        ->add("transition", transition);
}

JSON_Object *make_release_all_event(void)
{
    return JSON::Obj()->add("kind", "release_all");
}

JSON_Object *make_root(JSON_List *events, int pace_ms = 0, bool include_pace = false)
{
    JSON_Object *root = JSON::Obj()->add("events", events);
    if (include_pace) {
        root->add("pace_ms", pace_ms);
    }
    return root;
}

bool validate(JSON *root, InputParsedEvent events[INPUT_API_MAX_EVENTS], int &event_count, int &pace_ms,
    int &error_index, std::string &err)
{
    char buffer[INPUT_API_ERROR_SIZE] = { 0 };
    bool ok = input_api_validate_batch(root, events, event_count, pace_ms, error_index, buffer, sizeof(buffer));
    err = buffer;
    return ok;
}

JSON *parse_json_text(const char *text, int &tokens)
{
    size_t length = strlen(text);
    char *copy = new char[length + 1];
    memcpy(copy, text, length + 1);
    JSON *root = NULL;
    tokens = convert_text_to_json_objects(copy, length, 1024, &root);
    delete[] copy;
    return root;
}

} // namespace

TEST(InputApiValidationTest, ParsesValidBatchAndPreservesEventDetails)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int pace_ms = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(
        JSON::List()
            ->add(make_keyboard_event("press", { "a", "left_shift" }))
            ->add(make_joystick_event(2, "tap", { "up", "fire" }))
            ->add(make_release_all_event()),
        25, true);

    ASSERT_TRUE(validate(root, events, event_count, pace_ms, error_index, err));
    EXPECT_EQ(3, event_count);
    EXPECT_EQ(25, pace_ms);
    EXPECT_EQ(-1, error_index);
    EXPECT_EQ(INPUT_PARSED_KEYBOARD, events[0].kind);
    EXPECT_EQ(INPUT_PARSED_PRESS, events[0].transition);
    EXPECT_EQ(2, events[0].keyboard_count);
    EXPECT_EQ(input_api_find_keyboard_input("a"), events[0].keyboard_index[0]);
    EXPECT_EQ(input_api_find_keyboard_input("left_shift"), events[0].keyboard_index[1]);
    EXPECT_EQ(INPUT_PARSED_JOYSTICK, events[1].kind);
    EXPECT_EQ(2, events[1].port);
    EXPECT_EQ(INPUT_PARSED_TAP, events[1].transition);
    EXPECT_EQ((1 << 0) | (1 << 4), events[1].joystick_mask);
    EXPECT_EQ(INPUT_PARSED_RELEASE_ALL, events[2].kind);

    delete root;
}

TEST(InputApiValidationTest, RejectsUnknownRootField)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int pace_ms = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(JSON::List()->add(make_release_all_event()));
    ((JSON_Object *)root)->add("extra", true);

    EXPECT_FALSE(validate(root, events, event_count, pace_ms, error_index, err));
    EXPECT_EQ(-1, error_index);
    EXPECT_EQ(std::string("Unknown field `extra`."), err);

    delete root;
}

TEST(InputApiValidationTest, RejectsInvalidPaceValue)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int pace_ms = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(JSON::List()->add(make_release_all_event()));
    ((JSON_Object *)root)->add("pace_ms", 1001);

    EXPECT_FALSE(validate(root, events, event_count, pace_ms, error_index, err));
    EXPECT_EQ(std::string("`pace_ms` must be between 0 and 1000."), err);

    delete root;
}

TEST(InputApiValidationTest, RejectsLateInvalidEventAndReportsIndex)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int pace_ms = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(
        JSON::List()
            ->add(make_release_all_event())
            ->add(make_keyboard_event("press", { "left_shift" }))
            ->add(make_joystick_event(3, "press", { "up" })));

    EXPECT_FALSE(validate(root, events, event_count, pace_ms, error_index, err));
    EXPECT_EQ(2, error_index);
    EXPECT_EQ(std::string("`port` must be 1 or 2."), err);

    delete root;
}

TEST(InputApiValidationTest, AcceptsEveryDocumentedKeyboardInput)
{
    const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
    for (int i = 0; i < input_api_keyboard_map_count(); i++) {
        InputParsedEvent events[INPUT_API_MAX_EVENTS];
        int event_count = 0;
        int pace_ms = 0;
        int error_index = -1;
        std::string err;
        const char *transition = keyboard_map[i].restore ? "tap" : "press";

        JSON *root = make_root(JSON::List()->add(make_keyboard_event(transition, { keyboard_map[i].name })));

        EXPECT_TRUE(validate(root, events, event_count, pace_ms, error_index, err));
        delete root;
    }
}

TEST(InputApiValidationTest, RejectsRestoreOutsideTapAloneRule)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int pace_ms = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(JSON::List()->add(make_keyboard_event("press", { "restore" })));

    EXPECT_FALSE(validate(root, events, event_count, pace_ms, error_index, err));
    EXPECT_EQ(0, error_index);
    EXPECT_TRUE(err.find("`restore` must appear alone") != std::string::npos);

    delete root;
}

TEST(InputApiValidationTest, RejectsKeyboardDuplicateInputs)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int pace_ms = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(JSON::List()->add(make_keyboard_event("tap", { "a", "a" })));

    EXPECT_FALSE(validate(root, events, event_count, pace_ms, error_index, err));
    EXPECT_EQ(std::string("`a` appears more than once in `inputs`."), err);

    delete root;
}

TEST(InputApiValidationTest, AcceptsEveryDocumentedJoystickInput)
{
    const InputJoystickMapEntry *joystick_map = input_api_joystick_map();
    for (int i = 0; i < input_api_joystick_map_count(); i++) {
        InputParsedEvent events[INPUT_API_MAX_EVENTS];
        int event_count = 0;
        int pace_ms = 0;
        int error_index = -1;
        std::string err;

        JSON *root = make_root(JSON::List()->add(make_joystick_event(1, "press", { joystick_map[i].name })));

        EXPECT_TRUE(validate(root, events, event_count, pace_ms, error_index, err));
        delete root;
    }
}

TEST(InputApiValidationTest, RejectsJoystickDuplicateAndUnknownInputs)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int pace_ms = 0;
    int error_index = -1;
    std::string err;

    JSON *duplicate = make_root(JSON::List()->add(make_joystick_event(1, "tap", { "up", "up" })));
    EXPECT_FALSE(validate(duplicate, events, event_count, pace_ms, error_index, err));
    EXPECT_EQ(std::string("`up` appears more than once in `inputs`."), err);
    delete duplicate;

    JSON *unknown = make_root(JSON::List()->add(make_joystick_event(1, "tap", { "jump" })));
    EXPECT_FALSE(validate(unknown, events, event_count, pace_ms, error_index, err));
    EXPECT_EQ(std::string("`jump` is not a valid joystick input."), err);
    delete unknown;
}

TEST(InputApiValidationTest, RejectsReleaseAllExtraFields)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int pace_ms = 0;
    int error_index = -1;
    std::string err;

    JSON_Object *event = make_release_all_event();
    event->add("port", 1);
    JSON *root = make_root(JSON::List()->add(event));

    EXPECT_FALSE(validate(root, events, event_count, pace_ms, error_index, err));
    EXPECT_EQ(std::string("Unknown field `port`."), err);

    delete root;
}

TEST(InputApiValidationTest, RejectsMalformedJsonBeforeValidation)
{
    int tokens = 0;
    JSON *root = parse_json_text("{\"events\":[", tokens);

    EXPECT_TRUE(tokens < 0);
    EXPECT_EQ((JSON *)0, root);
}
