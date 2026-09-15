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

JSON_Object *make_root(JSON_List *events)
{
    return JSON::Obj()->add("events", events);
}

bool validate(JSON *root, InputParsedEvent events[INPUT_API_MAX_EVENTS], int &event_count, int &error_index,
    std::string &err)
{
    char buffer[INPUT_API_ERROR_SIZE] = { 0 };
    bool ok = input_api_validate_batch(root, events, event_count, error_index, buffer, sizeof(buffer));
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

TEST(JsonValueTest, RendersFullSignedIntegerRange)
{
    JSON_Integer minimum(INT32_MIN);
    JSON_Integer maximum(INT32_MAX);
    JSON_Integer negative(-1);
    JSON_Integer zero(0);
    EXPECT_EQ(std::string("-2147483648"), std::string(minimum.render()));
    EXPECT_EQ(std::string("2147483647"), std::string(maximum.render()));
    EXPECT_EQ(std::string("-1"), std::string(negative.render()));
    EXPECT_EQ(std::string("0"), std::string(zero.render()));
}

TEST(InputApiValidationTest, ParsesValidBatchAndPreservesEventDetails)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(
        JSON::List()
            ->add(make_keyboard_event("press", { "a", "left_shift" }))
            ->add(make_joystick_event(2, "tap", { "up", "fire", "fire2", "fire3" }))
            ->add(make_release_all_event()));

    ASSERT_TRUE(validate(root, events, event_count, error_index, err));
    EXPECT_EQ(3, event_count);
    EXPECT_EQ(-1, error_index);
    EXPECT_EQ(INPUT_PARSED_KEYBOARD, events[0].kind);
    EXPECT_EQ(INPUT_PARSED_PRESS, events[0].transition);
    EXPECT_EQ(2, events[0].keyboard_count);
    EXPECT_EQ(input_api_find_keyboard_input("a"), events[0].keyboard_index[0]);
    EXPECT_EQ(input_api_find_keyboard_input("left_shift"), events[0].keyboard_index[1]);
    EXPECT_EQ(INPUT_PARSED_JOYSTICK, events[1].kind);
    EXPECT_EQ(2, events[1].port);
    EXPECT_EQ(INPUT_PARSED_TAP, events[1].transition);
    EXPECT_EQ((1 << 0) | (1 << 4) | (1 << 5) | (1 << 6), events[1].joystick_mask);
    EXPECT_EQ(INPUT_PARSED_RELEASE_ALL, events[2].kind);

    delete root;
}

TEST(InputApiValidationTest, RejectsUnknownRootField)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(JSON::List()->add(make_release_all_event()));
    ((JSON_Object *)root)->add("extra", true);

    EXPECT_FALSE(validate(root, events, event_count, error_index, err));
    EXPECT_EQ(-1, error_index);
    EXPECT_EQ(std::string("Unknown field `extra`."), err);

    delete root;
}

TEST(InputApiValidationTest, RejectsLateInvalidEventAndReportsIndex)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(
        JSON::List()
            ->add(make_release_all_event())
            ->add(make_keyboard_event("press", { "left_shift" }))
            ->add(make_joystick_event(3, "press", { "up" })));

    EXPECT_FALSE(validate(root, events, event_count, error_index, err));
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
        int error_index = -1;
        std::string err;
        const char *transition = keyboard_map[i].restore ? "tap" : "press";

        JSON *root = make_root(JSON::List()->add(make_keyboard_event(transition, { keyboard_map[i].name })));

        EXPECT_TRUE(validate(root, events, event_count, error_index, err));
        delete root;
    }
}

// `restore` is an edge on the NMI line, not a matrix column, and route_input.cc
// skips it when it builds the live matrix for a press and for a release. Both
// transitions have to stay rejected, or the call answers 200 and does nothing.
TEST(InputApiValidationTest, RejectsRestorePress)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(JSON::List()->add(make_keyboard_event("press", { "restore" })));

    EXPECT_FALSE(validate(root, events, event_count, error_index, err));
    EXPECT_EQ(0, error_index);
    EXPECT_EQ(std::string("`restore` is only valid with transition `tap`."), err);

    delete root;
}

TEST(InputApiValidationTest, RejectsRestoreRelease)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(JSON::List()->add(make_keyboard_event("release", { "restore" })));

    EXPECT_FALSE(validate(root, events, event_count, error_index, err));
    EXPECT_EQ(0, error_index);
    EXPECT_EQ(std::string("`restore` is only valid with transition `tap`."), err);

    delete root;
}

// A restore tap on its own keeps working, which is what every caller written
// against the previous rule sends.
TEST(InputApiValidationTest, AcceptsRestoreTapAlone)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(JSON::List()->add(make_keyboard_event("tap", { "restore" })));

    EXPECT_TRUE(validate(root, events, event_count, error_index, err));
    EXPECT_EQ(1, event_count);
    EXPECT_EQ(1, events[0].keyboard_count);

    delete root;
}

// The relaxation: matrix keys may share a tap with `restore`, in either order,
// which is what C= plus RESTORE and RUN/STOP plus RESTORE need.
TEST(InputApiValidationTest, AcceptsRestoreTapWithMatrixKeys)
{
    const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();

    for (int order = 0; order < 2; order++) {
        InputParsedEvent events[INPUT_API_MAX_EVENTS];
        int event_count = 0;
        int error_index = -1;
        std::string err;

        JSON *root = make_root(JSON::List()->add((order == 0)
            ? make_keyboard_event("tap", { "commodore", "restore" })
            : make_keyboard_event("tap", { "restore", "commodore" })));

        EXPECT_TRUE(validate(root, events, event_count, error_index, err));
        EXPECT_EQ(1, event_count);
        EXPECT_EQ(2, events[0].keyboard_count);

        int restore_count = 0;
        int matrix_count = 0;
        for (int i = 0; i < events[0].keyboard_count; i++) {
            if (keyboard_map[events[0].keyboard_index[i]].restore) {
                restore_count++;
            } else {
                matrix_count++;
            }
        }
        EXPECT_EQ(1, restore_count);
        EXPECT_EQ(1, matrix_count);

        delete root;
    }
}

// The batch limit still applies with `restore` in the list, and a duplicate is
// still refused, so relaxing the rule did not open either of those.
TEST(InputApiValidationTest, RejectsDuplicateRestoreInTap)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(
        JSON::List()->add(make_keyboard_event("tap", { "restore", "commodore", "restore" })));

    EXPECT_FALSE(validate(root, events, event_count, error_index, err));
    EXPECT_EQ(0, error_index);
    EXPECT_TRUE(err.find("appears more than once") != std::string::npos);

    delete root;
}

TEST(InputApiValidationTest, RejectsKeyboardDuplicateInputs)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(JSON::List()->add(make_keyboard_event("tap", { "a", "a" })));

    EXPECT_FALSE(validate(root, events, event_count, error_index, err));
    EXPECT_EQ(std::string("`a` appears more than once in `inputs`."), err);

    delete root;
}

TEST(InputApiValidationTest, AcceptsEveryDocumentedJoystickInput)
{
    const InputJoystickMapEntry *joystick_map = input_api_joystick_map();
    for (int i = 0; i < input_api_joystick_map_count(); i++) {
        InputParsedEvent events[INPUT_API_MAX_EVENTS];
        int event_count = 0;
        int error_index = -1;
        std::string err;

        JSON *root = make_root(JSON::List()->add(make_joystick_event(1, "press", { joystick_map[i].name })));

        EXPECT_TRUE(validate(root, events, event_count, error_index, err));
        delete root;
    }
}

TEST(InputApiValidationTest, RejectsJoystickDuplicateAndUnknownInputs)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    std::string err;

    JSON *duplicate = make_root(JSON::List()->add(make_joystick_event(1, "tap", { "up", "up" })));
    EXPECT_FALSE(validate(duplicate, events, event_count, error_index, err));
    EXPECT_EQ(std::string("`up` appears more than once in `inputs`."), err);
    delete duplicate;

    JSON *unknown = make_root(JSON::List()->add(make_joystick_event(1, "tap", { "jump" })));
    EXPECT_FALSE(validate(unknown, events, event_count, error_index, err));
    EXPECT_EQ(std::string("`jump` is not a valid joystick input."), err);
    delete unknown;
}

TEST(InputApiValidationTest, AcceptsAllSevenJoystickInputsInOneEvent)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    std::string err;

    JSON *root = make_root(JSON::List()->add(
        make_joystick_event(2, "press", { "up", "down", "left", "right", "fire", "fire2", "fire3" })));

    ASSERT_TRUE(validate(root, events, event_count, error_index, err));
    ASSERT_EQ(1, event_count);
    EXPECT_EQ(0x7F, events[0].joystick_mask);

    delete root;
}

TEST(InputApiValidationTest, RejectsReleaseAllExtraFields)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    std::string err;

    JSON_Object *event = make_release_all_event();
    event->add("port", 1);
    JSON *root = make_root(JSON::List()->add(event));

    EXPECT_FALSE(validate(root, events, event_count, error_index, err));
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

TEST(JsonValueTest, ReplacedPrimitivesRetainTheirParent)
{
    JSON_Object object;
    object.add("value", 0);

    object.set("value", 123);
    EXPECT_EQ(eInteger, object.get("value")->type());
    EXPECT_EQ(static_cast<JSON *>(&object), object.get("value")->parent);

    object.set("value", true);
    EXPECT_EQ(eBool, object.get("value")->type());
    EXPECT_EQ(static_cast<JSON *>(&object), object.get("value")->parent);

    object.set("value", "replacement");
    EXPECT_EQ(eString, object.get("value")->type());
    EXPECT_EQ(static_cast<JSON *>(&object), object.get("value")->parent);
}

namespace {

JSON *parse_or_fail(const char *text)
{
    int tokens = 0;
    JSON *root = parse_json_text(text, tokens);
    return (tokens > 0) ? root : NULL;
}

std::string first_error(const char *text, int &error_index)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    std::string err;
    JSON *root = parse_or_fail(text);
    if (!root) {
        return "unparsed";
    }
    if (validate(root, events, event_count, error_index, err)) {
        err = "ok";
    }
    delete root;
    return err;
}

} // namespace

TEST(InputApiMouseTest, ParsesEveryMouseEventShape)
{
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    std::string err;
    JSON *root = parse_or_fail(
        "{\"events\":["
        "{\"kind\":\"mouse\",\"inputs\":[\"left\",\"middle\",\"right\"],\"transition\":\"press\"},"
        "{\"kind\":\"mouse\",\"move\":{\"x\":-127,\"y\":127}},"
        "{\"kind\":\"mouse\",\"move\":{\"y\":-3}},"
        "{\"kind\":\"mouse\",\"wheel\":{\"vertical\":2,\"horizontal\":-64}},"
        "{\"kind\":\"mouse\",\"path\":[[1,2],[-3,4]],\"interval_ms\":20},"
        "{\"kind\":\"mouse\",\"path\":[[0,0]]},"
        "{\"kind\":\"mouse\",\"inputs\":[\"right\"],\"transition\":\"tap\"}"
        "]}");
    ASSERT_TRUE(root != NULL);
    ASSERT_TRUE(validate(root, events, event_count, error_index, err));
    EXPECT_EQ(7, event_count);
    EXPECT_EQ(INPUT_PARSED_MOUSE_BUTTONS, events[0].kind);
    EXPECT_EQ(INPUT_PARSED_PRESS, events[0].transition);
    EXPECT_EQ(0x07, events[0].mouse_buttons);
    EXPECT_EQ(INPUT_PARSED_MOUSE_MOVE, events[1].kind);
    EXPECT_EQ(-127, events[1].mouse_x);
    EXPECT_EQ(127, events[1].mouse_y);
    EXPECT_EQ(0, events[2].mouse_x);
    EXPECT_EQ(-3, events[2].mouse_y);
    EXPECT_EQ(INPUT_PARSED_MOUSE_WHEEL, events[3].kind);
    EXPECT_EQ(2, events[3].wheel_vertical);
    EXPECT_EQ(-64, events[3].wheel_horizontal);
    EXPECT_EQ(INPUT_PARSED_MOUSE_PATH, events[4].kind);
    EXPECT_EQ(2, events[4].path_steps);
    EXPECT_EQ(20, events[4].path_interval_ms);
    EXPECT_TRUE(events[4].path != NULL);
    EXPECT_EQ(INPUT_API_DEFAULT_MOUSE_PATH_INTERVAL_MS, events[5].path_interval_ms);
    EXPECT_EQ(INPUT_PARSED_TAP, events[6].transition);
    EXPECT_EQ(0x02, events[6].mouse_buttons);
    delete root;
}

TEST(InputApiMouseTest, RejectsMalformedMouseEvents)
{
    struct Case { const char *event; const char *error; };
    static const Case cases[] = {
        { "{\"kind\":\"mouse\"}", "A mouse event needs exactly one of `inputs`, `move`, `wheel`, or `path`." },
        { "{\"kind\":\"mouse\",\"move\":{\"x\":1},\"wheel\":{\"vertical\":1}}",
          "A mouse event needs exactly one of `inputs`, `move`, `wheel`, or `path`." },
        { "{\"kind\":\"mouse\",\"move\":{\"x\":128}}", "`move.x` must be -127..127." },
        { "{\"kind\":\"mouse\",\"move\":{\"y\":-128}}", "`move.y` must be -127..127." },
        { "{\"kind\":\"mouse\",\"move\":{\"z\":1}}", "Unknown field `move.z`." },
        { "{\"kind\":\"mouse\",\"move\":{}}", "`move` must have `x` or `y`." },
        { "{\"kind\":\"mouse\",\"move\":[1,2]}", "`move` must be an object." },
        { "{\"kind\":\"mouse\",\"move\":{\"x\":\"1\"}}", "`move.x` must be an integer." },
        { "{\"kind\":\"mouse\",\"wheel\":{}}", "`wheel` must turn at least one wheel." },
        { "{\"kind\":\"mouse\",\"wheel\":{\"vertical\":65}}", "`wheel.vertical` must be -64..64." },
        { "{\"kind\":\"mouse\",\"inputs\":[\"left\"]}", "`transition` is required." },
        { "{\"kind\":\"mouse\",\"inputs\":[\"thumb\"],\"transition\":\"press\"}",
          "`thumb` is not a valid mouse input." },
        { "{\"kind\":\"mouse\",\"inputs\":[\"left\",\"left\"],\"transition\":\"press\"}",
          "`left` appears more than once in `inputs`." },
        { "{\"kind\":\"mouse\",\"inputs\":[],\"transition\":\"press\"}", "`inputs` must contain 1..3 entries." },
        { "{\"kind\":\"mouse\",\"move\":{\"x\":1},\"transition\":\"press\"}",
          "`transition` is only valid with `inputs`." },
        { "{\"kind\":\"mouse\",\"move\":{\"x\":1},\"interval_ms\":10}", "`interval_ms` is only valid with `path`." },
        { "{\"kind\":\"mouse\",\"path\":[]}", "`path` must contain 1..256 steps." },
        { "{\"kind\":\"mouse\",\"path\":[[1]]}", "`path[0]` must be an [x, y] pair." },
        { "{\"kind\":\"mouse\",\"path\":[[1,2],[3,\"4\"]]}", "`path[1]` must hold integers." },
        { "{\"kind\":\"mouse\",\"path\":[[1,200]]}", "`path[0]` values must be -127..127." },
        { "{\"kind\":\"mouse\",\"path\":[[1,2]],\"interval_ms\":19}", "`interval_ms` must be 20..1000." },
        { "{\"kind\":\"mouse\",\"port\":1,\"move\":{\"x\":1}}", "Unknown field `port`." },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        std::string text = std::string("{\"events\":[") + cases[i].event + "]}";
        int error_index = -2;
        EXPECT_EQ(std::string(cases[i].error), first_error(text.c_str(), error_index));
        EXPECT_EQ(0, error_index);
    }
}

TEST(InputApiMouseTest, AcceptsTheLongestPathAndRejectsOneStepMore)
{
    std::string steps;
    for (int i = 0; i < INPUT_API_MAX_MOUSE_PATH_STEPS; i++) {
        steps += (i ? ",[1,-1]" : "[1,-1]");
    }
    int error_index = -1;
    std::string ok = std::string("{\"events\":[{\"kind\":\"mouse\",\"path\":[") + steps + "]}]}";
    EXPECT_EQ(std::string("ok"), first_error(ok.c_str(), error_index));
    std::string long_path = std::string("{\"events\":[{\"kind\":\"mouse\",\"path\":[") + steps + ",[1,1]]}]}";
    EXPECT_EQ(std::string("`path` must contain 1..256 steps."), first_error(long_path.c_str(), error_index));
}

TEST(InputApiMouseTest, UnknownKindListsMouse)
{
    int error_index = -1;
    EXPECT_EQ(std::string("`kind` must be one of `keyboard`, `joystick`, `mouse`, or `release_all`."),
        first_error("{\"events\":[{\"kind\":\"trackball\"}]}", error_index));
}

#include "../rest_mouse_queue.h"

namespace {

std::string drain(RestMouseQueue &queue)
{
    // One line per report: buttons dx dy wheel pan wait.
    std::string out;
    RestMouseReport report;
    while (queue.takeDue(1000, 4, report)) {
        char line[64];
        sprintf(line, "%d %d %d %d %d %d\n", report.buttons, report.dx, report.dy, report.wheel, report.pan,
            report.wait_ticks);
        out += line;
    }
    return out;
}

bool parse_batch(const char *text, InputParsedEvent events[INPUT_API_MAX_EVENTS], int &count, JSON *&root)
{
    int error_index = -1;
    std::string err;
    root = parse_or_fail(text);
    return root && validate(root, events, count, error_index, err);
}

} // namespace

TEST(RestMouseQueueTest, BuildsReportsWithButtonsCarriedAndTimingKept)
{
    static InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int count = 0;
    JSON *root = NULL;
    ASSERT_TRUE(parse_batch(
        "{\"events\":["
        "{\"kind\":\"mouse\",\"inputs\":[\"left\"],\"transition\":\"press\"},"
        "{\"kind\":\"mouse\",\"path\":[[1,2],[3,4],[-5,-6]],\"interval_ms\":32},"
        "{\"kind\":\"mouse\",\"wheel\":{\"vertical\":-2,\"horizontal\":1}},"
        "{\"kind\":\"mouse\",\"inputs\":[\"middle\"],\"transition\":\"tap\"},"
        "{\"kind\":\"mouse\",\"inputs\":[\"left\"],\"transition\":\"release\"},"
        "{\"kind\":\"mouse\",\"move\":{\"x\":-127,\"y\":127}}"
        "]}", events, count, root));
    RestMouseQueue queue;
    queue.clear(0);
    int needed = 0;
    for (int i = 0; i < count; i++) {
        needed += RestMouseQueue::reportsFor(events[i]);
        queue.append(events[i], 8, 5);
    }
    EXPECT_EQ(11, needed);
    EXPECT_EQ(11, queue.pending());
    // The path's interval is 32ms, seven 5ms ticks; a tap holds for 8.
    EXPECT_EQ(std::string(
        "1 0 0 0 0 0\n"
        "1 1 2 0 0 0\n"
        "1 3 4 0 0 7\n"
        "1 -5 -6 0 0 7\n"
        "1 0 0 -1 0 0\n"
        "1 0 0 -1 0 0\n"
        "1 0 0 0 1 0\n"
        "5 0 0 0 0 0\n"
        "1 0 0 0 0 8\n"
        "0 0 0 0 0 0\n"
        "0 -127 127 0 0 0\n"), drain(queue));
    delete root;
}

TEST(RestMouseQueueTest, WaitsUntilAReportIsDue)
{
    static InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int count = 0;
    JSON *root = NULL;
    ASSERT_TRUE(parse_batch("{\"events\":[{\"kind\":\"mouse\",\"path\":[[1,0],[1,0]],\"interval_ms\":20}]}",
        events, count, root));
    RestMouseQueue queue;
    queue.append(events[0], 8, 5);
    RestMouseReport report;
    // The first report of an idle queue is due at once; the next one after the
    // path's 20ms and never sooner than the minimum spacing.
    EXPECT_TRUE(queue.takeDue(0, 0, report));
    EXPECT_FALSE(queue.takeDue(3, 4, report));
    EXPECT_TRUE(queue.takeDue(4, 4, report));
    EXPECT_FALSE(queue.takeDue(100, 4, report));
    delete root;
}

TEST(RestMouseQueueTest, ABatchFitsOnlyWhenEveryPartFits)
{
    InputParsedEvent wheel;
    memset(&wheel, 0, sizeof(wheel));
    wheel.kind = INPUT_PARSED_MOUSE_WHEEL;
    wheel.wheel_vertical = 64;
    wheel.wheel_horizontal = 64;
    InputParsedEvent release_all;
    memset(&release_all, 0, sizeof(release_all));
    release_all.kind = INPUT_PARSED_RELEASE_ALL;
    InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int needed = 0;
    int room = 0;

    // Eight turns of 128 reports fill the queue exactly; a ninth does not fit.
    for (int i = 0; i < 9; i++) {
        events[i] = wheel;
    }
    EXPECT_TRUE(RestMouseQueue::batchFits(events, 8, RestMouseQueue::CAPACITY, needed, room));
    EXPECT_FALSE(RestMouseQueue::batchFits(events, 9, RestMouseQueue::CAPACITY, needed, room));
    EXPECT_EQ(9 * 128, needed);
    EXPECT_EQ((int)RestMouseQueue::CAPACITY, room);

    // A release_all empties a full queue for what follows it...
    events[0] = release_all;
    EXPECT_TRUE(RestMouseQueue::batchFits(events, 9, 0, needed, room));

    // ...but what comes before it is queued first and has to fit too.
    for (int i = 0; i < 63; i++) {
        events[i] = wheel;
    }
    events[63] = release_all;
    EXPECT_FALSE(RestMouseQueue::batchFits(events, 64, RestMouseQueue::CAPACITY, needed, room));
}

TEST(RestMouseQueueTest, ClearKeepsTheButtonsHeldNow)
{
    RestMouseQueue queue;
    InputParsedEvent release;
    memset(&release, 0, sizeof(release));
    release.kind = INPUT_PARSED_MOUSE_BUTTONS;
    release.transition = INPUT_PARSED_RELEASE;
    release.mouse_buttons = 0x02;
    queue.clear(0x03);
    queue.append(release, 8, 5);
    EXPECT_EQ(std::string("1 0 0 0 0 0\n"), drain(queue));
    EXPECT_EQ(RestMouseQueue::CAPACITY, queue.room());
    EXPECT_EQ(1, RestMouseQueue::ticksFor(1, 5));
    EXPECT_EQ(2, RestMouseQueue::ticksFor(6, 5));
    EXPECT_EQ(200, RestMouseQueue::ticksFor(1000, 5));
}
