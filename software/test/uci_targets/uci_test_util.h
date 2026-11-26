// Shared helpers for the UCI target host tests: one UCI command in, the reply
// blocks and the status out, as the command interface task sees them.
#ifndef UCI_TEST_UTIL_H
#define UCI_TEST_UTIL_H

#include <string>
#include <vector>
#include <stdio.h>
#include "uci_fakes.h"
#include "command_intf.h"

static int passed = 0, failed = 0;

static void check(bool ok, const char *name, const std::string &detail = "")
{
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : " - ", detail.c_str());
    if (ok) passed++; else failed++;
}

// One UCI command as the firmware sees it: message[0] = target, message[1] = command,
// parameters from message[2], length = total byte count.
struct Cmd {
    std::vector<uint8_t> buf;
    Message msg;
    Cmd(uint8_t target, uint8_t command) : buf(2, 0) { buf[0] = target; buf[1] = command; }
    Cmd &u8(uint8_t v) { buf.push_back(v); return *this; }
    Cmd &le16(uint32_t v) { u8(v & 0xFF); return u8((v >> 8) & 0xFF); }
    Cmd &le32(uint32_t v) { le16(v & 0xFFFF); return le16(v >> 16); }
    Cmd &str(const char *s) { while (*s) u8((uint8_t)*s++); return *this; }
};

struct Result {
    std::vector<uint8_t> reply;   // all reply blocks, concatenated
    int blocks;
    std::string status;
};

// parse_command, then get_more_data until the reply says last_part (the command
// interface task does the same, one 512-byte block per host handshake)
static Result run(Cmd &c)
{
    c.buf.resize(c.buf.size() + 8, 0); // the targets write a terminator at message[length]
    c.msg.message = c.buf.data();
    c.msg.length = (int)c.buf.size() - 8;
    c.msg.last_part = true;
    Message *reply = NULL, *status = NULL;
    Result r;
    r.blocks = 0;
    command_targets[c.buf[0] & CMD_IF_MAX_TARGET]->parse_command(&c.msg, &reply, &status);
    for (;;) {
        r.blocks++;
        r.reply.insert(r.reply.end(), reply->message, reply->message + reply->length);
        if (reply->last_part) break;
        command_targets[c.buf[0] & CMD_IF_MAX_TARGET]->get_more_data(&reply, &status);
    }
    r.status.assign((const char *)status->message, status->length);
    return r;
}

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(seed + i * 7 + (i >> 8));
    return v;
}

static int finish(const char *suite)
{
    printf("%s: %d passed, %d failed\n", suite, passed, failed);
    return failed ? 1 : 0;
}

#endif
