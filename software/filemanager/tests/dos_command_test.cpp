#include "../../io/usb/tests/host_test/host_test.h"

#include "../dos.h"
#include "home_directory.h"

#include <string>

namespace {

// The DOS command targets register themselves into command_targets[] when the
// dos1/dos2 globals in dos.cc are constructed.
CommandTarget *dos_target()
{
    return command_targets[1];
}

std::string message_text(Message *message)
{
    if (!message || !message->message) {
        return std::string();
    }
    return std::string((const char *)message->message, message->length);
}

// Issues a single-byte DOS command and reports the status message text. The
// root filesystem is enough here: "/" always resolves and a name below it
// never does, which is the whole distinction COPY_HOME_PATH gets wrong.
std::string run_command(uint8_t command_id)
{
    uint8_t buffer[64];
    Message command = { 2, true, buffer };
    buffer[0] = 1;            // target id
    buffer[1] = command_id;

    Message *reply = 0;
    Message *status = 0;
    dos_target()->parse_command(&command, &reply, &status);
    return message_text(status);
}

const char *STATUS_OK = "00,OK";

class HomeDirectoryGuard
{
public:
    HomeDirectoryGuard(const char *path) { HomeDirectory::setHomeDirectory(path); }
    ~HomeDirectoryGuard() { HomeDirectory::setHomeDirectory("/"); }
};

} // namespace

TEST(DosCopyHomePathTest, ReportsOkWhenHomeDirectoryResolves)
{
    HomeDirectoryGuard home("/");

    EXPECT_EQ(run_command(DOS_CMD_COPY_HOME_PATH), std::string(STATUS_OK));
}

TEST(DosCopyHomePathTest, ReportsFailureWhenHomeDirectoryIsMissing)
{
    HomeDirectoryGuard home("/does_not_exist");

    EXPECT_NE(run_command(DOS_CMD_COPY_HOME_PATH), std::string(STATUS_OK));
}

// DOS_CMD_CHANGE_DIR already reports failure correctly; COPY_HOME_PATH must
// behave the same way, since it is documented as the equivalent operation.
// This is the assertion that actually pins the bug: before the fix the two
// disagreed, because COPY_HOME_PATH fell through and overwrote the status.
TEST(DosCopyHomePathTest, MatchesChangeDirFailureStatus)
{
    const char *missing = "/does_not_exist";
    HomeDirectoryGuard home(missing);
    const std::string home_status = run_command(DOS_CMD_COPY_HOME_PATH);

    uint8_t buffer[64];
    Message command = { 0, true, buffer };
    buffer[0] = 1;
    buffer[1] = DOS_CMD_CHANGE_DIR;
    strcpy((char *)buffer + 2, missing);
    command.length = 2 + strlen(missing);

    Message *reply = 0;
    Message *status = 0;
    dos_target()->parse_command(&command, &reply, &status);

    EXPECT_EQ(home_status, message_text(status));
}
