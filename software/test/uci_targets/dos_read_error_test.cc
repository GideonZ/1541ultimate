// A failed file read must reach the C64 as a status, never as an unset pointer.
// DOS_CMD_READ_DATA answers with the file system's own message when the read fails
// (ultimate_dos-1.2: "When something goes wrong, this will be reported through the
// status channel"). get_more_data used to fill that text in without pointing the
// status at it, so the command interface copied whatever the caller's pointer held.
#include <string>
#include <vector>
#include <stdio.h>
#include "uci_test_util.h"
#include "dos.h"

static Message *const UNSET = (Message *)0x1;

struct Step {
    Message *reply, *status;
    std::string text() const
    {
        return status == UNSET ? "<status never set>" : std::string((const char *)status->message, status->length);
    }
};

// One READ_DATA command, or one follow-up block, with the status pointer left at a marker.
static Step read_data(bool first, uint32_t len)
{
    static Cmd *keep;
    Step s = { UNSET, UNSET };
    if (first) {
        delete keep;
        keep = new Cmd(1, DOS_CMD_READ_DATA);
        keep->le16(len);
        keep->buf.resize(keep->buf.size() + 8, 0);
        keep->msg.message = keep->buf.data();
        keep->msg.length = (int)keep->buf.size() - 8;
        keep->msg.last_part = true;
        command_targets[1]->parse_command(&keep->msg, &s.reply, &s.status);
    } else {
        command_targets[1]->get_more_data(&s.reply, &s.status);
    }
    return s;
}

static void open_file(const std::vector<uint8_t> &bytes)
{
    uci_fake::reset();
    uci_fake::fs_add_file("/Usb0/a.prg", bytes);
    Cmd cd(1, DOS_CMD_CHANGE_DIR); cd.str("/Usb0");
    run(cd);
    Cmd open(1, DOS_CMD_OPEN_FILE); open.u8(FA_READ).str("a.prg");
    run(open);
}

static void close_file(void)
{
    Cmd close(1, DOS_CMD_CLOSE_FILE);
    run(close);
}

int main()
{
    uci_fake::init();

    // The first block fails: empty data, the file system's message, and the transfer is over.
    open_file(pattern(1300, 3));
    uci_fake::read_result = FR_DISK_ERR;
    Step s = read_data(true, 1300);
    check(s.text() == "DISK ERROR", "read_data_first_block_fails_reports_the_file_system_message", s.text());
    check(s.reply != UNSET && s.reply && s.reply->length == 0 && s.reply->last_part,
          "read_data_first_block_fails_ends_the_transfer_with_no_data");
    close_file();

    // The second block fails: the first went out quiet, the second carries the message.
    open_file(pattern(1300, 3));
    s = read_data(true, 1300);
    check(s.reply && s.reply->length == 512 && !s.reply->last_part && s.text() == "",
          "read_data_good_block_before_the_failure_is_quiet", s.text());
    uci_fake::read_result = FR_DISK_ERR;
    s = read_data(false, 0);
    check(s.text() == "DISK ERROR" && s.reply && s.reply->last_part,
          "read_data_second_block_fails_reports_the_file_system_message", s.text());
    uci_fake::read_result = FR_OK;
    close_file();

    // A transfer without failures stays quiet and complete.
    open_file(pattern(700, 9));
    s = read_data(true, 700);
    bool first_last = s.reply->last_part;
    int first_len = s.reply->length;
    Step t = read_data(false, 0);
    // both blocks are the same data_message: compare s's fields from before the second call
    check(first_last == false && first_len == 512 && s.text() == "" && t.text() == "" && t.reply->last_part && t.reply->length == 188,
          "read_data_without_failure_is_unchanged", s.text() + "/" + t.text());
    close_file();

    return finish("dos_read_error");
}
