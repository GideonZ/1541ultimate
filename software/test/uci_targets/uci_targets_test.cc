// Smoke test of the host harness: drives the UNMODIFIED Dos (UCI target 1/2) and
// ControlTarget (UCI target 4) through command_targets[], exactly as the command
// interface task does. See uci_fakes.h for the control API.
#include <string>
#include <vector>
#include <stdio.h>
#include "uci_test_util.h"
#include "dos.h"
#include "control_target.h"

int main()
{
    uci_fake::init();

    // 1. DOS IDENTIFY
    {
        uci_fake::reset();
        Cmd c(1, DOS_CMD_IDENTIFY);
        Result r = run(c);
        check(!r.reply.empty() && r.status == "00,OK", "1 DOS IDENTIFY", std::string(r.reply.begin(), r.reply.end()) + " / " + r.status);
    }

    // 2. DOS OPEN, READ_DATA (whole file), CLOSE
    {
        uci_fake::reset();
        std::vector<uint8_t> bytes = pattern(1300, 3);
        uci_fake::fs_add_file("/Usb0/a.prg", bytes);
        Cmd cd(1, DOS_CMD_CHANGE_DIR); cd.str("/Usb0");
        Result rc = run(cd);
        Cmd open(1, DOS_CMD_OPEN_FILE); open.u8(FA_READ).str("a.prg");
        Result ro = run(open);
        Cmd rd(1, DOS_CMD_READ_DATA); rd.le16((uint32_t)bytes.size());
        Result rr = run(rd);
        Cmd close(1, DOS_CMD_CLOSE_FILE);
        Result rcl = run(close);
        char d[160];
        snprintf(d, sizeof d, "cd=%s open=%s read=%zu bytes in %d blocks close=%s open_files=%d",
                 rc.status.c_str(), ro.status.c_str(), rr.reply.size(), rr.blocks, rcl.status.c_str(), uci_fake::open_file_count());
        check(rc.status == "00,OK" && ro.status == "00,OK" && rr.reply == bytes && rcl.status == "00,OK" &&
              uci_fake::open_file_count() == 0, "2 DOS OPEN/READ_DATA/CLOSE", d);
    }

    // 3. DOS LOAD_REU of a 300 KB file
    {
        uci_fake::reset();
        const uint32_t len = 300 * 1024;
        std::vector<uint8_t> bytes = pattern(len, 11);
        uci_fake::fs_add_file("/Usb0/big.bin", bytes);
        Cmd cd(1, DOS_CMD_CHANGE_DIR); cd.str("/Usb0");
        run(cd);
        Cmd open(1, DOS_CMD_OPEN_FILE); open.u8(FA_READ).str("big.bin");
        run(open);
        Cmd load(1, DOS_CMD_LOAD_REU); load.le32(0x010000).le32(len);
        Result rl = run(load);
        Cmd close(1, DOS_CMD_CLOSE_FILE);
        run(close);
        bool match = memcmp(uci_fake::reu() + 0x010000, bytes.data(), len) == 0;
        std::string lens;
        for (size_t i = 0; i < uci_fake::io_log.size(); i++) {
            char t[32];
            snprintf(t, sizeof t, "%s%c%u", i ? "," : "", uci_fake::io_log[i].is_write ? 'w' : 'r', uci_fake::io_log[i].requested);
            lens += t;
        }
        check(match && rl.status == "00,OK" && uci_fake::open_file_count() == 0, "3 DOS LOAD_REU 300 KB",
              "status=" + rl.status + " io_log=[" + lens + "]");
    }

    // 4. DOS MOUNT_DISK
    {
        uci_fake::reset();
        uci_fake::fs_add_file("/Usb0/x.d64", pattern(174848, 5));
        uci_fake::install_drive(0, 8, true);
        uci_fake::next_subsys_result = SSRET_OK;
        Cmd cd(1, DOS_CMD_CHANGE_DIR); cd.str("/Usb0");
        run(cd);
        Cmd mnt(1, DOS_CMD_MOUNT_DISK); mnt.u8(8).str("x.d64");
        Result r = run(mnt);
        bool one = uci_fake::subsys_calls.size() == 1;
        char d[200];
        snprintf(d, sizeof d, "status=%s calls=%zu", r.status.c_str(), uci_fake::subsys_calls.size());
        std::string det = d;
        if (one) {
            const uci_fake::SubsysCall &s = uci_fake::subsys_calls[0];
            snprintf(d, sizeof d, " [subsys=%d func=0x%x mode=%d path=%s file=%s]", s.subsysID, s.functionID, s.mode, s.path.c_str(), s.filename.c_str());
            det += d;
        }
        check(r.status == "00,OK" && one, "4 DOS MOUNT_DISK x.d64 on drive 8", det);
    }

    // 5. CONTROL IDENTIFY
    {
        uci_fake::reset();
        Cmd c(4, CTRL_CMD_IDENTIFY);
        Result r = run(c);
        check(r.status == "00,OK", "5 CONTROL IDENTIFY", std::string(r.reply.begin(), r.reply.end()) + " / " + r.status);
    }

    // 6. CONTROL FREEZE with next result SSRET_NO_LOCK: record only
    {
        uci_fake::reset();
        uci_fake::next_subsys_result = SSRET_NO_LOCK;
        Cmd c(4, CTRL_CMD_FREEZE);
        Result r = run(c);
        char d[128];
        snprintf(d, sizeof d, "recorded status='%s' (len %zu), subsys calls=%zu (not asserted)", r.status.c_str(), r.status.size(), uci_fake::subsys_calls.size());
        check(true, "6 CONTROL FREEZE with SSRET_NO_LOCK", d);
    }

    // 7. Control API self-check: abort bit register, per-call I/O hook, error_string
    {
        uci_fake::reset();
        CMD_IF_STATUSBYTE |= CMD_ABORT_DATA;
        bool abort_set = (CMD_IF_STATUSBYTE & CMD_ABORT_DATA) != 0;
        int hook_calls = 0;
        uci_fake::on_io = [&](const uci_fake::IoEvent &e) { hook_calls++; if (hook_calls == 2) CMD_IF_STATUSBYTE |= CMD_ABORT_DATA; };
        uci_fake::fs_add_file("/Usb0/h.bin", pattern(2000, 1));
        Cmd cd(1, DOS_CMD_CHANGE_DIR); cd.str("/Usb0");
        run(cd);
        Cmd open(1, DOS_CMD_OPEN_FILE); open.u8(FA_READ).str("h.bin");
        run(open);
        Cmd rd(1, DOS_CMD_READ_DATA); rd.le16(2000);
        run(rd); // 4 blocks of 512: the hook fires on every File::read
        Cmd close(1, DOS_CMD_CLOSE_FILE);
        run(close);
        bool err_ok = strcmp(SubsysCommand::error_string(SSRET_NO_LOCK), "Could not obtain lock of subsystem") == 0;
        char d[128];
        snprintf(d, sizeof d, "abort_set=%d hook_calls=%d error_string=%d", abort_set, hook_calls, err_ok);
        check(abort_set && hook_calls == 4 && err_ok, "7 control API (abort bit, I/O hook, error_string)", d);
    }

    // 8. CONTROL LOAD_REU through the REU preloader (config-driven, absolute address 0x01000000)
    {
        uci_fake::reset();
        std::vector<uint8_t> img = pattern(0x100000, 9);
        uci_fake::fs_add_file("/Usb0/img.reu", img);
        uci_fake::cfg.set(CFG_C64_REU_EN, 1);
        uci_fake::cfg.set(CFG_C64_REU_SIZE, 3); // 1 MB
        uci_fake::cfg.set(CFG_C64_REU_OFFS, 0);
        uci_fake::cfg.set_string(CFG_C64_REU_IMG, "/Usb0/img.reu");
        Cmd c(4, CTRL_CMD_LOAD_REU); c.u8(0).u8(0).u8(0);
        Result r = run(c);
        check(r.status == "00,OK" && memcmp(uci_fake::reu(), img.data(), img.size()) == 0 && uci_fake::open_file_count() == 0,
              "8 CONTROL LOAD_REU via REUPreloader", "status=" + r.status + " reply=" + std::string(r.reply.begin() + 4, r.reply.end()));
    }

    return finish("uci_targets");
}
