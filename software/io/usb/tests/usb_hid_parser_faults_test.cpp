// The HID report descriptor parser reads whatever a USB device hands it, so a
// broken or hostile descriptor must not crash it, leak, or run for minutes.
//
// Two groups of tests:
//
//  - Faults: shapes that crashed, leaked or looped on the parser before the fix.
//    Each runs in a forked child so a crash or a hang is reported as a failure of
//    that one test.
//
//  - Identity: a corpus of descriptors that the parser handled correctly before the
//    fix. For each, a hash of every created item (usage, flags, size, count, range,
//    report id, application usage) and of the keyboard and mouse fields the driver
//    selects is pinned in usb_hid_parser_goldens.inc. The hashes were produced by the
//    parser as it stood before the fix, so the fix may not change any of them. A
//    mismatch prints the items that were actually created.

#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <functional>
#include <new>
#include <string>
#include <vector>

// Count every fifo operation: each Fifo method enters a safe section, so this
// bounds the work the parser does on the usage fifos independently of wall time.
static long g_fifo_ops = 0;
#define ENTER_SAFE_SECTION ++g_fifo_ops;
#define LEAVE_SAFE_SECTION

#include "host_test/host_test.h"

#ifndef HID_DECODER_UNDER_TEST
#define HID_DECODER_UNDER_TEST "../hid_decoder.h"
#endif
#include HID_DECODER_UNDER_TEST

// Live heap blocks, counted through the global operator new/delete.
static long g_live = 0;

void *operator new(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p) {
		throw std::bad_alloc();
	}
	g_live++;
	return p;
}
void *operator new[](size_t n) { return operator new(n); }
void operator delete(void *p) noexcept
{
	if (p) {
		g_live--;
		free(p);
	}
}
void operator delete(void *p, size_t) noexcept { operator delete(p); }
void operator delete[](void *p) noexcept { operator delete(p); }
void operator delete[](void *p, size_t) noexcept { operator delete(p); }

namespace {

typedef std::vector<uint8_t> Bytes;

Bytes cat(std::initializer_list<Bytes> parts)
{
	Bytes r;
	for (const Bytes& p : parts) {
		r.insert(r.end(), p.begin(), p.end());
	}
	return r;
}

Bytes rep(const Bytes& b, int n)
{
	Bytes r;
	for (int i = 0; i < n; i++) {
		r.insert(r.end(), b.begin(), b.end());
	}
	return r;
}

// Short items, tag bytes as in the HID 1.11 specification, section 6.2.2.
const Bytes APP = { 0xA1, 0x01 };
const Bytes PHYS = { 0xA1, 0x00 };
const Bytes LOGC = { 0xA1, 0x02 };
const Bytes END = { 0xC0 };
const Bytes PUSH = { 0xA4 };
const Bytes POP = { 0xB4 };
const Bytes IVAR = { 0x81, 0x02 };
const Bytes IARR = { 0x81, 0x00 };
const Bytes IREL = { 0x81, 0x06 };
const Bytes ICON = { 0x81, 0x01 };
const Bytes OVAR = { 0x91, 0x02 };
const Bytes OCON = { 0x91, 0x01 };
const Bytes OARR = { 0x91, 0x00 };
const Bytes FVAR = { 0xB1, 0x02 };

Bytes id(uint8_t n) { return { 0x85, n }; }
Bytes cnt(uint8_t n) { return { 0x95, n }; }
Bytes sz(uint8_t n) { return { 0x75, n }; }
Bytes cnt4(uint32_t n) { return { 0x97, (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24) }; }

const Bytes XY = { 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F };
const Bytes MOUSE_HEAD = { 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00 };
const Bytes BTN3 = { 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
	0x95, 0x01, 0x75, 0x05, 0x81, 0x01 };

// HID 1.11 appendix B.1 boot keyboard, B.2 boot mouse; a gamepad with a nested
// physical collection and a PUSH/POP pair.
const Bytes BOOT_KEYBOARD = { 0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
	0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01,
	0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08,
	0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0 };
const Bytes BOOT_MOUSE = { 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01,
	0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0 };
const Bytes GAMEPAD = { 0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0xA1, 0x00, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25,
	0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02, 0xC0, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0C, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x0C, 0x81, 0x02, 0xA4, 0x05, 0x01, 0x09, 0x32, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02, 0xB4,
	0x75, 0x04, 0x95, 0x01, 0x81, 0x03, 0x05, 0x08, 0x19, 0x01, 0x29, 0x02, 0x75, 0x01, 0x95, 0x02, 0x91, 0x02,
	0xC0 };

Bytes mouseCol(int rid)
{
	Bytes r = cat({ { 0x05, 0x01, 0x09, 0x02 }, APP, { 0x09, 0x01 }, PHYS });
	if (rid) {
		r = cat({ r, id(rid) });
	}
	return cat({ r, { 0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01 }, cnt(5), sz(1), IVAR, cnt(1), sz(3), ICON,
		{ 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F }, sz(16), cnt(2), IREL,
		{ 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F }, sz(8), cnt(1), IREL, END, END });
}

Bytes vendorIn(int rid, int count)
{
	return cat({ { 0x06, 0x00, 0xFF, 0x09, 0x01 }, APP, id(rid), { 0x09, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x00 }, sz(8),
		{ 0x96, (uint8_t)count, (uint8_t)(count >> 8) }, IVAR, END });
}

Bytes vendorFeature(int rid, int count)
{
	return cat({ { 0x06, 0x00, 0xFF, 0x09, 0x01 }, APP, id(rid), { 0x09, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x00 }, sz(8),
		{ 0x96, (uint8_t)count, (uint8_t)(count >> 8) }, FVAR, END });
}

Bytes consumer(int rid)
{
	return cat({ { 0x05, 0x0C, 0x09, 0x01 }, APP, id(rid), { 0x19, 0x00, 0x2A, 0x3C, 0x02, 0x15, 0x00, 0x26, 0x3C, 0x02 },
		sz(16), cnt(1), IARR, END });
}

Bytes kbdArr(int rid, uint8_t umax, Bytes logmax)
{
	Bytes r = cat({ { 0x05, 0x01, 0x09, 0x06 }, APP });
	if (rid) {
		r = cat({ r, id(rid) });
	}
	return cat({ r, { 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(8), IVAR, cnt(1), sz(8), ICON,
		cnt(6), sz(8), { 0x15, 0x00 }, logmax, { 0x05, 0x07, 0x19, 0x00, 0x29, umax }, IARR, cnt(5), sz(1),
		{ 0x05, 0x08, 0x19, 0x01, 0x29, 0x05 }, OVAR, cnt(1), sz(3), OCON, END });
}

Bytes nkro(int rid)
{
	return cat({ { 0x05, 0x01, 0x09, 0x06 }, APP, id(rid), { 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01 },
		sz(1), cnt(8), IVAR, { 0x19, 0x00, 0x29, 0xE7 }, sz(1), { 0x95, 0xE8 }, IVAR, END });
}

Bytes touch(int rid, int contacts)
{
	Bytes r = cat({ { 0x05, 0x0D, 0x09, 0x04 }, APP, id(rid) });
	for (int c = 0; c < contacts; c++) {
		r = cat({ r, { 0x09, 0x22 }, LOGC, { 0x09, 0x42, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(1), IVAR, sz(7), ICON,
			{ 0x09, 0x51, 0x25, 0x0F }, sz(8), IVAR, { 0x05, 0x01, 0x26, 0xFF, 0x7F, 0x09, 0x30 }, sz(16), IVAR,
			{ 0x09, 0x31 }, IVAR, { 0x05, 0x0D }, END });
	}
	return cat({ r, { 0x09, 0x54, 0x25, 0x0A }, sz(8), cnt(1), IVAR, END });
}

// A mouse whose X/Y/button items sit `depth` physical collections deep inside the
// application collection. With `stray`, every level but the innermost also leaves one
// usage (Wheel) unused, which a correct parser drops when the collection ends.
Bytes nestedMouse(int depth, bool stray)
{
	Bytes r = cat({ { 0x05, 0x01, 0x09, 0x02 }, APP });
	for (int i = 0; i < depth; i++) {
		r = cat({ r, { 0x09, 0x01 }, PHYS });
		if (stray && i < depth - 1) {
			r = cat({ r, { 0x09, 0x38 } });
		}
	}
	r = cat({ r, BTN3, XY, sz(8), cnt(2), IREL, rep(END, depth + 1) });
	return r;
}

// The same mouse with the application collection only: the expected result for
// any nesting depth.
Bytes plainMouse()
{
	return cat({ { 0x05, 0x01, 0x09, 0x02 }, APP, BTN3, XY, sz(8), cnt(2), IREL, END });
}

// A mouse where X/Y are declared in the application collection AFTER a deep
// nest of collections that each leave a stray Wheel usage behind.
Bytes mouseAfterNest(int depth)
{
	Bytes r = cat({ { 0x05, 0x01, 0x09, 0x02 }, APP, BTN3 });
	for (int i = 0; i < depth; i++) {
		r = cat({ r, { 0x09, 0x01 }, PHYS, { 0x09, 0x38 } });
	}
	r = cat({ r, rep(END, depth), XY, sz(8), cnt(2), IREL, END });
	return r;
}

// Everything the parser makes of a descriptor, as text.
struct Verdict {
	int rc;
	long leaked;
	long fifo_ops;
	bool mouse;
	bool keyboard;
	std::string text;
};

template <class L>
void dumpLocation(std::string& out, const char *name, const L& l)
{
	char b[200];
	snprintf(b, sizeof(b), " %s[r%d o%d l%d c%d %d..%d f%02x]", name, l.report, l.offset, l.length, l.count, l.min,
		l.max, l.flags);
	out += b;
}

template <class R>
void dumpReports(std::string& out, const char *kind, R *const *list)
{
	char b[200];
	for (int i = 0; i < 256; i++) {
		if (!list[i]) {
			continue;
		}
		snprintf(b, sizeof(b), "%s id=%u appl=%08x n=%d\n", kind, list[i]->reportId, list[i]->applicationUsage,
			list[i]->items.get_elements());
		out += b;
		for (int t = 0; t < list[i]->items.get_elements(); t++) {
			auto *x = list[i]->items[t];
			snprintf(b, sizeof(b), "  %08x f%02x len=%u cnt=%u %d..%d\n", x->usage, x->flags, x->length, x->count,
				(int)x->min, (int)x->max);
			out += b;
		}
	}
}

// The parser types under test.
struct Prod {
	typedef ::HidReportParser Parser;
	typedef ::HidItemList List;
	typedef ::t_hid_mouse_fields MF;
	typedef ::t_hid_keyboard_fields KF;
};

template <class V>
Verdict parse(const Bytes& d)
{
	Verdict v;
	v.text.reserve(1 << 23);
	const char *buf0 = v.text.data();
	long before = g_live;
	long ops0 = g_fifo_ops;
	{
		typename V::Parser parser;
		typename V::List items;
		v.rc = parser.decode(&items, d.data(), (int)d.size());
		v.fifo_ops = g_fifo_ops - ops0;
		typename V::MF mf;
		typename V::KF kf;
		v.mouse = items.locateMouseFields(mf);
		v.keyboard = items.locateKeyboardFields(kf);
		char b[100];
		snprintf(b, sizeof(b), "rc=%d hasID=%d mouse=%d kbd=%d\n", v.rc, items.hasReportID, v.mouse, v.keyboard);
		v.text += b;
		dumpReports(v.text, "in", items.inputReportList);
		dumpReports(v.text, "out", items.outputReportList);
		dumpReports(v.text, "feat", items.featureReportList);
		v.text += "M:";
		dumpLocation(v.text, "x", mf.mouse_x);
		dumpLocation(v.text, "y", mf.mouse_y);
		snprintf(b, sizeof(b), " has=%d%d%d%d%d", mf.has_button1, mf.has_button2, mf.has_button3, mf.has_wheel_v,
			mf.has_wheel_h);
		v.text += b;
		if (mf.has_button1) {
			dumpLocation(v.text, "b1", mf.button1);
		}
		if (mf.has_button2) {
			dumpLocation(v.text, "b2", mf.button2);
		}
		if (mf.has_button3) {
			dumpLocation(v.text, "b3", mf.button3);
		}
		if (mf.has_wheel_v) {
			dumpLocation(v.text, "wv", mf.wheel_v);
		}
		if (mf.has_wheel_h) {
			dumpLocation(v.text, "wh", mf.wheel_h);
		}
		v.text += "\nK:";
		snprintf(b, sizeof(b), " bitmap=%d", kf.has_bitmap_keys);
		v.text += b;
		for (int q = 0; q < 8; q++) {
			if (kf.has_modifier[q]) {
				dumpLocation(v.text, "mod", kf.modifiers[q]);
			}
		}
		if (kf.has_key_array) {
			dumpLocation(v.text, "arr", kf.key_array);
		}
		v.text += "\n";
	}
	v.leaked = g_live - before;
	if (v.text.data() != buf0) {
		fprintf(stderr, "test bug: result text outgrew its buffer\n");
		abort();
	}
	return v;
}

// Run `body` in a child; its return value is the child's exit status. A signal
// (crash, or the alarm for a hang) comes back as "killed by signal N".
std::string inChild(const std::function<int()>& body, unsigned seconds = 20)
{
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid == 0) {
		alarm(seconds);
		_exit(body());
	}
	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFSIGNALED(status)) {
		return "killed by signal " + std::to_string(WTERMSIG(status));
	}
	if (WEXITSTATUS(status) == 0) {
		return "ok";
	}
	return "check " + std::to_string(WEXITSTATUS(status)) + " failed";
}

// Records the first failed check's code and goes on, so one run lists every fault.
#define CHECK_CHILD(cond, code, msg) \
	do { \
		if (!(cond)) { \
			fprintf(stderr, "    %s\n", msg); \
			if (!bad) { \
				bad = code; \
			} \
		} \
	} while (0)

// HID_TEST_ONLY=<test name> runs just that test, to see how each one fares on the
// old parser.
#define SELECTED(name) \
	do { \
		const char *only = getenv("HID_TEST_ONLY"); \
		if (only && strcmp(only, #name) != 0) { \
			return; \
		} \
	} while (0)

#define EXPECT_CHILD_OK(name, ...) \
	do { \
		std::string verdict = inChild(__VA_ARGS__); \
		if (verdict != "ok") { \
			fprintf(stderr, "  %s: %s\n", name, verdict.c_str()); \
		} \
		EXPECT_EQ(verdict, std::string("ok")); \
	} while (0)

std::string linesWith(const std::string& text, const char *needle)
{
	std::string r;
	size_t p = 0;
	while (p < text.size()) {
		size_t e = text.find('\n', p);
		std::string line = text.substr(p, e - p);
		if (line.find(needle) != std::string::npos) {
			r += line + "\n";
		}
		p = e + 1;
	}
	return r;
}

std::string inputLengths(const Verdict& v)
{
	// "len=N" of every input item, in order.
	std::string r;
	size_t p = 0;
	size_t end = v.text.find("out id=");
	if (end == std::string::npos) {
		end = v.text.find("M:");
	}
	while ((p = v.text.find("len=", p)) != std::string::npos && p < end) {
		size_t q = v.text.find(' ', p);
		r += v.text.substr(p + 4, q - p - 4) + ",";
		p = q;
	}
	return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Faults

TEST(HidParserFaultsTest, ItemAfterEndCollectionWithNothingOpen)
{
	SELECTED(ItemAfterEndCollectionWithNothingOpen);
	// Expected: the extra END is ignored and the rest parses as if it were absent.
	struct Case {
		const char *name;
		Bytes d;
		Bytes same_as;
	};
	std::vector<Case> cases = {
		{ "END then boot mouse", cat({ END, BOOT_MOUSE }), BOOT_MOUSE },
		{ "boot mouse, extra END, boot mouse", cat({ BOOT_MOUSE, END, BOOT_MOUSE }), cat({ BOOT_MOUSE, BOOT_MOUSE }) },
		{ "five ENDs then boot keyboard", cat({ rep(END, 5), BOOT_KEYBOARD }), BOOT_KEYBOARD },
		{ "END, GLOBAL item, END, usage, Input", cat({ END, { 0x05, 0x01 }, END, { 0x09, 0x02 }, { 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(1), IVAR }),
			cat({ { 0x05, 0x01 }, { 0x09, 0x02 }, { 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(1), IVAR }) },
	};
	for (const Case& c : cases) {
		EXPECT_CHILD_OK(c.name, [&]() {
			int bad = 0;
			Verdict v = parse<Prod>(c.d);
			Verdict e = parse<Prod>(c.same_as);
			CHECK_CHILD(v.rc == 0, 1, "rc is not 0");
			CHECK_CHILD(v.leaked == 0, 2, "leaked");
			// Same items and fields as the descriptor without the extra END.
			CHECK_CHILD(v.text == e.text, 3, "result differs from the descriptor without the extra END");
			return bad;
		});
	}
}

TEST(HidParserFaultsTest, BootMouseWithExtraEndStillFindsTheMouse)
{
	SELECTED(BootMouseWithExtraEndStillFindsTheMouse);
	// One END too many, before or after the mouse: the mouse is still found.
	EXPECT_CHILD_OK("extra END before", [&]() {
		int bad = 0;
		Verdict v = parse<Prod>(cat({ END, BOOT_MOUSE }));
		CHECK_CHILD(v.rc == 0, 1, "rc is not 0");
		CHECK_CHILD(v.mouse, 2, "mouse not found");
		return bad;
	});
	EXPECT_CHILD_OK("extra END after", [&]() {
		int bad = 0;
		Verdict v = parse<Prod>(cat({ BOOT_MOUSE, END }));
		CHECK_CHILD(v.rc == 0, 1, "rc is not 0");
		CHECK_CHILD(v.mouse, 2, "mouse not found");
		return bad;
	});
}

TEST(HidParserFaultsTest, CollectionsNestedDeeperThanEightBehaveLikeShallowOnes)
{
	SELECTED(CollectionsNestedDeeperThanEightBehaveLikeShallowOnes);
	Verdict plain = parse<Prod>(plainMouse());
	EXPECT_TRUE(plain.mouse);
	const int depths[] = { 7, 8, 9, 12, 100, 3000 };
	for (int depth : depths) {
		for (int stray = 0; stray < 2; stray++) {
			std::string name = "depth " + std::to_string(depth) + (stray ? " with stray usages" : "");
			EXPECT_CHILD_OK(name.c_str(), [&]() {
				int bad = 0;
				Verdict v = parse<Prod>(nestedMouse(depth, stray));
				CHECK_CHILD(v.rc == 0, 1, "rc is not 0");
				CHECK_CHILD(v.leaked == 0, 2, "leaked");
				CHECK_CHILD(v.mouse, 3, "mouse not found");
				CHECK_CHILD(v.text == plain.text, 4, "result differs from the unnested mouse");
				return bad;
			});
		}
	}
}

TEST(HidParserBehaviourTest, MouseFieldsAreWhereTheDescriptorPutsThem)
{
	SELECTED(MouseFieldsAreWhereTheDescriptorPutsThem);
	// Explicit values, not a comparison with another parse: 3 button bits, 5 pad
	// bits, then X and Y as 8-bit relative fields.
	const int depths[] = { 1, 9, 50 };
	for (int depth : depths) {
		std::string name = "depth " + std::to_string(depth);
		EXPECT_CHILD_OK(name.c_str(), [&]() {
			int bad = 0;
			Prod::Parser parser;
			Prod::List items;
			Bytes d = nestedMouse(depth, true);
			CHECK_CHILD(parser.decode(&items, d.data(), (int)d.size()) == 0, 1, "rc is not 0");
			Prod::MF mf;
			CHECK_CHILD(items.locateMouseFields(mf), 2, "mouse not found");
			CHECK_CHILD(mf.mouse_x.offset == 8 && mf.mouse_x.length == 8, 3, "X is not at bit 8, 8 bits");
			CHECK_CHILD(mf.mouse_y.offset == 16 && mf.mouse_y.length == 8, 4, "Y is not at bit 16, 8 bits");
			CHECK_CHILD(mf.has_button1 && mf.button1.offset == 0, 5, "button 1 is not at bit 0");
			CHECK_CHILD(!mf.has_wheel_v, 6, "a wheel appeared");
			return bad;
		});
	}
}

TEST(HidParserFaultsTest, StrayUsagesOfAClosedCollectionDoNotReachItsParent)
{
	SELECTED(StrayUsagesOfAClosedCollectionDoNotReachItsParent);
	// After a deep nest closes, X and Y come from the application collection's own
	// usages. A leftover Wheel usage from the nest must not take the place of X.
	Verdict plain = parse<Prod>(cat({ { 0x05, 0x01, 0x09, 0x02 }, APP, BTN3, XY, sz(8), cnt(2), IREL, END }));
	const int depths[] = { 3, 8, 9, 20 };
	for (int depth : depths) {
		std::string name = "depth " + std::to_string(depth);
		EXPECT_CHILD_OK(name.c_str(), [&]() {
			int bad = 0;
			Verdict v = parse<Prod>(mouseAfterNest(depth));
			CHECK_CHILD(v.rc == 0, 1, "rc is not 0");
			CHECK_CHILD(v.leaked == 0, 2, "leaked");
			CHECK_CHILD(v.mouse, 3, "mouse not found");
			CHECK_CHILD(v.text == plain.text, 4, "result differs from the mouse without the nest");
			return bad;
		});
	}
}

TEST(HidParserFaultsTest, CollectionsLeftOpenAtTheEndAreReleased)
{
	SELECTED(CollectionsLeftOpenAtTheEndAreReleased);
	const int depths[] = { 1, 5, 8, 9, 40 };
	for (int depth : depths) {
		std::string name = "open " + std::to_string(depth);
		EXPECT_CHILD_OK(name.c_str(), [&]() {
			int bad = 0;
			Verdict v = parse<Prod>(rep(APP, depth));
			CHECK_CHILD(v.rc == 0, 1, "rc is not 0");
			CHECK_CHILD(v.leaked == 0, 2, "leaked");
			return bad;
		});
	}
}

TEST(HidParserFaultsTest, PushesLeftOpenAtTheEndAreReleased)
{
	SELECTED(PushesLeftOpenAtTheEndAreReleased);
	const int counts[] = { 1, 3, 8, 9, 12, 200 };
	for (int n : counts) {
		std::string name = std::to_string(n) + " PUSH, no POP";
		EXPECT_CHILD_OK(name.c_str(), [&]() {
			int bad = 0;
			Verdict v = parse<Prod>(cat({ sz(8), rep(PUSH, n), sz(4), cnt(1), IVAR }));
			CHECK_CHILD(v.rc == 0, 1, "rc is not 0");
			CHECK_CHILD(v.leaked == 0, 2, "leaked");
			return bad;
		});
	}
}

TEST(HidParserFaultsTest, PopRestoresTheGlobalsAtAnyPushDepth)
{
	SELECTED(PopRestoresTheGlobalsAtAnyPushDepth);
	// Report Size is i+1 after the i-th PUSH. Popping back out, the size seen by
	// each Input must be the one that was current before that PUSH: N, N-1, ..., 1.
	const int depths[] = { 8, 9, 12, 200 };
	for (int n : depths) {
		std::string name = std::to_string(n) + " nested PUSH";
		EXPECT_CHILD_OK(name.c_str(), [&]() {
			int bad = 0;
			Bytes d = sz(1);
			for (int i = 1; i <= n; i++) {
				d = cat({ d, PUSH, sz(i + 1) });
			}
			for (int i = n; i >= 1; i--) {
				d = cat({ d, POP, cnt(1), IVAR });
			}
			Verdict v = parse<Prod>(d);
			CHECK_CHILD(v.rc == 0, 1, "rc is not 0");
			CHECK_CHILD(v.leaked == 0, 2, "leaked");
			std::string expect;
			for (int i = n; i >= 1; i--) {
				expect += std::to_string(i) + ",";
			}
			CHECK_CHILD(inputLengths(v) == expect, 3, "wrong Report Size after POP");
			return bad;
		});
	}
}

TEST(HidParserFaultsTest, PartialPopsAndRepushesKeepTheRightGlobals)
{
	SELECTED(PartialPopsAndRepushesKeepTheRightGlobals);
	EXPECT_CHILD_OK("push 12, pop 5, push 3, pop all", [&]() {
		int bad = 0;
		Bytes d = sz(1);
		int level = 1;
		std::vector<int> stack;
		std::string expect;
		auto push = [&]() { d = cat({ d, PUSH }); stack.push_back(level); level++; d = cat({ d, sz(level) }); };
		auto pop = [&]() {
			int bad = 0;
			d = cat({ d, POP, cnt(1), IVAR });
			level = stack.back();
			stack.pop_back();
			expect += std::to_string(level) + ",";
		};
		for (int i = 0; i < 12; i++) push();
		for (int i = 0; i < 5; i++) pop();
		for (int i = 0; i < 3; i++) push();
		while (!stack.empty()) pop();
		Verdict v = parse<Prod>(d);
		CHECK_CHILD(v.rc == 0, 1, "rc is not 0");
		CHECK_CHILD(v.leaked == 0, 2, "leaked");
		CHECK_CHILD(inputLengths(v) == expect, 3, "wrong Report Size after POP");
		return bad;
	});
}

TEST(HidParserBehaviourTest, PopWithNothingPushedIsIgnored)
{
	SELECTED(PopWithNothingPushedIsIgnored);
	Verdict v = parse<Prod>(cat({ sz(5), POP, cnt(1), IVAR }));
	EXPECT_EQ(v.rc, 0);
	EXPECT_EQ(inputLengths(v), std::string("5,"));
	EXPECT_EQ(v.leaked, 0L);
}

TEST(HidParserFaultsTest, ArrayWithAHugeLogicalRangeStopsWhenTheUsagesAreUsedUp)
{
	SELECTED(ArrayWithAHugeLogicalRangeStopsWhenTheUsagesAreUsedUp);
	// Logical range -2^31 .. 2^31-1 is 2^32 values, but three usages are queued.
	Bytes huge = cat({ { 0x05, 0x09, 0x19, 0x01, 0x29, 0x03 }, { 0x17, 0x00, 0x00, 0x00, 0x80, 0x27, 0xFF, 0xFF, 0xFF, 0x7F }, sz(8), cnt(1), IARR,
		{ 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(3), IVAR });
	Bytes small = cat({ { 0x05, 0x09, 0x19, 0x01, 0x29, 0x03 }, { 0x15, 0x00, 0x25, 0x0A }, sz(8), cnt(1), IARR,
		{ 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(3), IVAR });
	EXPECT_CHILD_OK("2^32 range", [&]() {
		int bad = 0;
		Verdict v = parse<Prod>(huge);
		CHECK_CHILD(v.rc == 0, 1, "rc is not 0");
		CHECK_CHILD(v.leaked == 0, 2, "leaked");
		CHECK_CHILD(v.fifo_ops < 1000, 3, "too many fifo operations");
		// The Var item looks the same as after a small range that also uses up the
		// three usages.
		Verdict s = parse<Prod>(small);
		CHECK_CHILD(linesWith(v.text, "f02 len=1 cnt=1 0..1") == linesWith(s.text, "f02 len=1 cnt=1 0..1"), 4, "Var item differs");
		CHECK_CHILD(!linesWith(v.text, "f02 len=1 cnt=1 0..1").empty(), 5, "no Var item");
		return bad;
	}, 5);
}

TEST(HidParserFaultsTest, ArrayRangeAtTheEndOfTheIntRangeDoesNotOverflow)
{
	SELECTED(ArrayRangeAtTheEndOfTheIntRangeDoesNotOverflow);
	// Logical range INT_MAX-1 .. INT_MAX is two values. Five usages are queued; two
	// are consumed by the array and the Var item takes the next three.
	Bytes top = cat({ { 0x05, 0x09, 0x19, 0x01, 0x29, 0x05 }, { 0x17, 0xFE, 0xFF, 0xFF, 0x7F, 0x27, 0xFF, 0xFF, 0xFF, 0x7F }, sz(8), cnt(1), IARR,
		{ 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(3), IVAR });
	EXPECT_CHILD_OK("INT_MAX-1..INT_MAX", [&]() {
		int bad = 0;
		Verdict v = parse<Prod>(top);
		CHECK_CHILD(v.rc == 0, 1, "rc is not 0");
		// Usages 3, 4 and 5 land on the three Var items.
		CHECK_CHILD(v.text.find("00090003 f02 len=1") != std::string::npos, 2, "usage 3 missing");
		CHECK_CHILD(v.text.find("00090004 f02 len=1") != std::string::npos, 3, "usage 4 missing");
		CHECK_CHILD(v.text.find("00090005 f02 len=1") != std::string::npos, 4, "usage 5 missing");
		CHECK_CHILD(v.leaked == 0, 5, "leaked");
		return bad;
	}, 5);
}

TEST(HidParserFaultsTest, UsageRangeIsBoundedByTheFifoNotByTheRange)
{
	SELECTED(UsageRangeIsBoundedByTheFifoNotByTheRange);
	// Usage Minimum 0, Usage Maximum 0xFFFF: 65,536 usages asked for, a fifo that
	// holds 256. Each such range costs at most a fifo's worth of operations.
	const int reps = 100;
	Bytes one = cat({ { 0x05, 0x09, 0x1A, 0x00, 0x00, 0x2A, 0xFF, 0xFF, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(4), IVAR });
	EXPECT_CHILD_OK("100 full-range usages", [&]() {
		int bad = 0;
		Verdict v = parse<Prod>(rep(one, reps));
		CHECK_CHILD(v.rc == 0, 1, "rc is not 0");
		CHECK_CHILD(v.leaked == 0, 2, "leaked");
		CHECK_CHILD(v.fifo_ops < (long)reps * 400, 3, "too many fifo operations");
		return bad;
	}, 5);
}

TEST(HidParserBehaviourTest, FullUsageFifoStillHoldsTheFirst256Usages)
{
	SELECTED(FullUsageFifoStillHoldsTheFirst256Usages);
	// The first 256 usages of a long range are kept; the 257th and later are not.
	// 300 Var items: the first 256 get usages 0..255, the rest repeat the last.
	Bytes d = cat({ { 0x05, 0x09, 0x1A, 0x00, 0x00, 0x2A, 0xFF, 0xFF, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(255), IVAR, cnt(45), IVAR });
	Verdict v = parse<Prod>(d);
	EXPECT_EQ(v.rc, 0);
	EXPECT_TRUE(v.text.find("00090000 f02") != std::string::npos);
	EXPECT_TRUE(v.text.find("000900fe f02") != std::string::npos);
	EXPECT_TRUE(v.text.find("000900ff f02") != std::string::npos);
	EXPECT_TRUE(v.text.find("00090100") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Identity: descriptors the old parser handles correctly.

namespace {

struct Corpus {
	std::string name;
	Bytes d;
};

std::vector<Corpus> identityCorpus()
{
	std::vector<Corpus> c;
	auto add = [&](const char *n, const Bytes& d) { c.push_back({ n, d }); };

	add("HID 1.11 boot keyboard", BOOT_KEYBOARD);
	add("HID 1.11 boot mouse", BOOT_MOUSE);
	add("nested gamepad with PUSH/POP", GAMEPAD);
	add("mouse with wheel, no ID", mouseCol(0));
	add("mouse with wheel, ID 1", mouseCol(1));
	add("mouse + consumer IDs", cat({ mouseCol(1), consumer(2) }));
	add("NKRO keyboard + consumer", cat({ nkro(1), consumer(2) }));
	add("NKRO keyboard + consumer + vendor 64", cat({ nkro(1), consumer(2), vendorIn(3, 64) }));
	add("boot keyboard (ID 1) + vendor 64", cat({ kbdArr(1, 0x65, { 0x25, 0x65 }), vendorIn(0x20, 64) }));
	add("mouse + vendor 63", cat({ mouseCol(1), vendorIn(0x10, 63) }));
	add("mouse + vendor 64", cat({ mouseCol(1), vendorIn(0x10, 64) }));
	add("mouse + vendor 255", cat({ mouseCol(1), vendorIn(0x10, 255) }));
	add("mouse + touch 10 contacts", cat({ mouseCol(1), touch(2, 10) }));
	add("mouse + touch 5 contacts", cat({ mouseCol(1), touch(2, 5) }));
	add("two IDs of 40 bytes each (80 total)", cat({ vendorIn(1, 40), vendorIn(2, 40) }));
	add("two IDs of 63 bytes", cat({ vendorIn(1, 63), vendorIn(2, 63) }));
	add("ID + 63 bytes", vendorIn(1, 63));
	add("ID + 64 bytes", vendorIn(1, 64));
	add("no ID, 64 Var bytes", cat({ { 0x15, 0x00, 0x26, 0xFF, 0x00 }, sz(8), cnt(64), IVAR }));
	add("no ID, 80 Var bytes", cat({ { 0x15, 0x00, 0x26, 0xFF, 0x00 }, sz(8), cnt(80), IVAR }));
	add("keyboard ID 1 + feature report of 300 bytes", cat({ kbdArr(1, 0x65, { 0x25, 0x65 }), vendorFeature(2, 300) }));
	add("mouse + feature report of 200 bytes", cat({ mouseCol(1), vendorFeature(2, 200) }));
	add("keyboard + mouse on two IDs", cat({ kbdArr(1, 0x65, { 0x25, 0x65 }), mouseCol(2) }));
	add("report ID 1 with 600 bits, then a mouse on ID 2",
		cat({ MOUSE_HEAD, id(1), sz(8), cnt(75), ICON, id(2), BTN3, XY, sz(8), cnt(2), IREL, END, END }));
	add("ID 1 600 bits, ID 2 mouse, ID 1 more",
		cat({ MOUSE_HEAD, id(1), sz(8), cnt(75), ICON, id(2), BTN3, XY, sz(8), cnt(2), IREL, id(1), XY, sz(8), cnt(2), IREL, END, END }));
	add("mouse after 56 pad bytes", cat({ { 0x05, 0x01, 0x09, 0x02 }, APP, { 0x09, 0x01 }, PHYS, id(1), sz(8), cnt(56), ICON, XY, sz(8), cnt(2), IREL, END, END }));
	add("mouse after 70 pad bytes", cat({ { 0x05, 0x01, 0x09, 0x02 }, APP, { 0x09, 0x01 }, PHYS, id(1), sz(8), cnt(70), ICON, XY, sz(8), cnt(2), IREL, END, END }));
	add("mouse after 200 bit pads", cat({ MOUSE_HEAD, sz(200), cnt(3), ICON, XY, sz(8), cnt(2), IREL, END, END }));
	add("report ID 300 clamps to 255", cat({ { 0x86, 0x2C, 0x01 }, { 0x15, 0x00, 0x25, 0x01 }, sz(8), cnt(4), IVAR }));
	add("boot mouse with one extra trailing END", cat({ BOOT_MOUSE, END }));
	add("boot keyboard with one extra trailing END", cat({ BOOT_KEYBOARD, END }));
	add("END alone", END);
	add("empty application collection", cat({ { 0x05, 0x01, 0x09, 0x02 }, APP, END }));
	add("8 collections open, mouse inside", nestedMouse(7, false));
	add("8 collections open with stray usages", nestedMouse(7, true));
	add("8 collections open, mouse after the nest", mouseAfterNest(7));
	add("collections left open", rep(APP, 8));
	add("8 PUSH then 8 POP around a mouse",
		cat({ { 0x05, 0x01, 0x09, 0x02 }, APP, sz(8), cnt(2), rep(PUSH, 8), sz(16), cnt(4), POP, BTN3, XY, IREL, rep(POP, 7), END }));
	add("PUSH, change, POP restores size",
		cat({ { 0x05, 0x01, 0x09, 0x02 }, APP, sz(8), cnt(2), PUSH, sz(16), cnt(4), POP, BTN3, XY, IREL, END }));
	add("POP with nothing pushed", cat({ sz(5), POP, cnt(1), IVAR }));

	// arrays
	add("keyboard array 0..101, logical 0..101", kbdArr(0, 0x65, { 0x25, 0x65 }));
	add("keyboard array 0..255, logical 0..255", kbdArr(0, 0xFF, { 0x26, 0xFF, 0x00 }));
	add("keyboard array 0..255, logical 0..-1", kbdArr(0, 0xFF, { 0x25, 0xFF }));
	add("keyboard array 0..101, logical 0..255", kbdArr(0, 0x65, { 0x26, 0xFF, 0x00 }));
	add("keyboard array 0..255, logical 0..101", kbdArr(0, 0xFF, { 0x25, 0x65 }));
	add("array usages 1-3, logical 0..10, then Var",
		cat({ { 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x09, 0x10, 0x15, 0x00, 0x25, 0x0A }, sz(8), cnt(1), IARR, { 0x25, 0x01 }, sz(1), cnt(2), IVAR }));
	add("array usages 1-5, logical 1..2, then Var",
		cat({ { 0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x01, 0x25, 0x02 }, sz(8), cnt(1), IARR, { 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(3), IVAR }));
	add("array with no usages, logical 0..1000, then Var",
		cat({ { 0x15, 0x00, 0x26, 0xE8, 0x03 }, sz(8), cnt(1), IARR, { 0x05, 0x09, 0x09, 0x07, 0x25, 0x01 }, sz(1), cnt(2), IVAR }));
	add("array range 0, logical min above max, then Var",
		cat({ { 0x05, 0x09, 0x19, 0x01, 0x29, 0x04, 0x15, 0x05, 0x25, 0x02 }, sz(8), cnt(1), IARR, { 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(4), IVAR }));
	add("array, logical -3..3, 10 usages, then Var",
		cat({ { 0x05, 0x09, 0x19, 0x01, 0x29, 0x0A, 0x15, 0xFD, 0x25, 0x03 }, sz(8), cnt(1), IARR, { 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(5), IVAR }));
	add("array of 3 then array of 2, 10 usages",
		cat({ { 0x05, 0x09, 0x19, 0x01, 0x29, 0x0A, 0x15, 0x00, 0x25, 0x02 }, sz(8), cnt(1), IARR, { 0x25, 0x03 }, IARR, { 0x25, 0x01 }, sz(1), cnt(3), IVAR }));
	add("keyboard modifiers + count-0 key array",
		cat({ { 0x05, 0x01, 0x09, 0x06 }, APP, { 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(8), IVAR, sz(8), cnt(0), { 0x25, 0x65, 0x19, 0, 0x29, 0x65 }, IARR, END }));
	add("keyboard modifiers + size-0 key array",
		cat({ { 0x05, 0x01, 0x09, 0x06 }, APP, { 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(8), IVAR, sz(0), cnt(6), { 0x25, 0x65, 0x19, 0, 0x29, 0x65 }, IARR, END }));
	add("count-0 array only", cat({ { 0x05, 0x01, 0x09, 0x06 }, APP, { 0x05, 0x07, 0x19, 0, 0x29, 0x65, 0x15, 0, 0x25, 0x65 }, sz(8), cnt(0), IARR, END }));
	add("count-0 array pops its range, then Var",
		cat({ MOUSE_HEAD, { 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0, 0x25, 1 }, sz(8), cnt(0), IARR, XY, sz(8), cnt(2), IREL, END, END }));
	add("size-0 array pops its range, then Var",
		cat({ MOUSE_HEAD, { 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0, 0x25, 1 }, sz(0), cnt(1), IARR, XY, sz(8), cnt(2), IREL, END, END }));
	add("array followed by a Var item that takes the leftover usages",
		cat({ MOUSE_HEAD, { 0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x02 }, sz(8), cnt(1), IARR, { 0x25, 0x01 }, sz(1), cnt(5), IVAR, sz(3), ICON, XY, sz(8), cnt(2), IREL, END, END }));
	add("size-0 Var + real field", cat({ { 0x09, 0x01, 0x09, 0x02, 0x15, 0x00, 0x25, 0xFF }, sz(0), cnt(1), IVAR, sz(8), IVAR }));
	add("size-0 mouse X/Y", cat({ { 0x05, 0x01, 0x09, 0x02 }, APP, { 0x09, 0x01 }, PHYS, XY, sz(0), cnt(2), IREL, END, END }));
	add("count-0 Var with a usage range, then Var",
		cat({ MOUSE_HEAD, { 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0, 0x25, 1 }, sz(1), cnt(0), IVAR, cnt(3), IVAR, sz(5), cnt(1), ICON, XY, sz(8), cnt(2), IREL, END, END }));

	// outputs, features
	add("LED output leaves usages for the next input array",
		cat({ { 0x05, 0x01, 0x09, 0x06 }, APP, { 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(8), IVAR,
			{ 0x05, 0x08, 0x19, 0x01, 0x29, 0x05 }, cnt(3), OVAR, cnt(1), sz(5), OCON, sz(8), cnt(6), { 0x25, 0x65 }, IARR, END }));
	add("output array pops its range, input Var takes the rest",
		cat({ { 0x05, 0x01, 0x09, 0x06 }, APP, { 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(8), IVAR,
			{ 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x02 }, cnt(1), sz(8), OARR, { 0x15, 0x00, 0x25, 0x01 }, cnt(2), sz(1), IVAR,
			sz(6), cnt(1), ICON, sz(8), cnt(6), { 0x05, 0x07, 0x25, 0x65, 0x19, 0, 0x29, 0x65 }, IARR, END }));
	add("feature between inputs",
		cat({ { 0x05, 0x01, 0x09, 0x06 }, APP, { 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(8), IVAR,
			{ 0x05, 0x08, 0x19, 0x01, 0x29, 0x05 }, cnt(5), FVAR, sz(8), cnt(6), { 0x05, 0x07, 0x25, 0x65, 0x19, 0, 0x29, 0x65 }, IARR, END }));
	add("output on another ID, input resumes ID 1",
		cat({ { 0x05, 0x01, 0x09, 0x06 }, APP, id(1), { 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(8), IVAR,
			id(2), { 0x05, 0x08, 0x19, 0x01, 0x29, 0x05 }, cnt(5), OVAR, cnt(3), OCON, id(1), sz(8), cnt(6), { 0x05, 0x07, 0x25, 0x65, 0x19, 0, 0x29, 0x65 }, IARR, END }));
	add("feature report of 70 bytes with a mouse after it",
		cat({ { 0x06, 0x00, 0xFF, 0x09, 0x01 }, APP, id(2), sz(8), cnt(70), FVAR, END, mouseCol(1) }));

	// usage ranges
	add("usage range 0..0xFFFF, 4 Var items", cat({ { 0x05, 0x09, 0x1A, 0x00, 0x00, 0x2A, 0xFF, 0xFF, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(4), IVAR }));
	add("usage range 0..0xFFFF, 300 Var items", cat({ { 0x05, 0x09, 0x1A, 0x00, 0x00, 0x2A, 0xFF, 0xFF, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(255), IVAR, cnt(45), IVAR }));
	add("usage range 0..0xFFFF twice, 300 Var items",
		cat({ { 0x05, 0x09, 0x1A, 0x00, 0x00, 0x2A, 0xFF, 0xFF, 0x1A, 0x00, 0x10, 0x2A, 0xFF, 0x10, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(255), IVAR, cnt(45), IVAR }));
	add("200 usages, then a range of 1000",
		cat({ { 0x05, 0x09, 0x19, 0x00, 0x29, 0xC7, 0x19, 0x00, 0x2A, 0xE7, 0x03, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(255), IVAR, cnt(100), IVAR }));
	add("255 single usages, then a range",
		cat({ { 0x05, 0x09, 0x19, 0x00, 0x29, 0xFE, 0x19, 0x00, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(255), IVAR, cnt(10), IVAR }));
	add("usage minimum above maximum", cat({ { 0x05, 0x09, 0x19, 0x09, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(3), IVAR }));
	add("usage maximum alone", cat({ { 0x05, 0x09, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(5), IVAR }));
	add("usage range of one", cat({ { 0x05, 0x09, 0x19, 0x07, 0x29, 0x07, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(2), IVAR }));
	add("usage range in a nested collection",
		cat({ { 0x05, 0x09, 0x19, 0x01, 0x29, 0x03 }, APP, { 0x19, 0x05, 0x29, 0x09, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(5), IVAR, END }));

	// the rest
	add("truncated item", cat({ BOOT_MOUSE, { 0x05 } }));
	add("unknown item tag", cat({ BOOT_MOUSE, { 0xF4, 0x01 } }));
	add("size-0 constant, count 512", cat({ sz(0), { 0x96, 0x00, 0x02 }, rep(IVAR, 50) }));
	add("feature 1 bit count 60000", cat({ { 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt4(60000), FVAR }));
	add("vendor first with 70 bytes constant then a mouse", cat({ { 0x06, 0x00, 0xFF, 0x09, 0x01 }, APP, sz(8), cnt(70), ICON, END, BOOT_MOUSE }));
	add("count-0 Var first in an application, then a mouse", cat({ { 0x06, 0x00, 0xFF, 0x09, 0x01 }, APP, sz(8), cnt(0), IVAR, END, BOOT_MOUSE }));
	add("size-0 constant first in an application, then a mouse", cat({ { 0x06, 0x00, 0xFF, 0x09, 0x01 }, APP, sz(0), cnt(1), ICON, END, BOOT_MOUSE }));
	add("physical min/max, unit, exponent",
		cat({ { 0x05, 0x01, 0x09, 0x02 }, APP, { 0x35, 0x00, 0x46, 0xFF, 0x00, 0x55, 0x0E, 0x65, 0x13 }, BTN3, XY, sz(8), cnt(2), IREL, END }));
	add("string, designator and delimiter items",
		cat({ { 0x05, 0x01, 0x09, 0x02 }, APP, { 0x39, 0x01, 0x49, 0x02, 0x59, 0x03, 0x79, 0x01, 0x89, 0x02, 0x99, 0x03, 0xA9, 0x01 }, BTN3, XY, sz(8), cnt(2), IREL, END }));
	// more shapes: fields straddling the 64-byte input buffer, interleaved IDs, outputs
	// with more count than usages
	add("padding 61 bytes then 16-bit X/Y",
		cat({ MOUSE_HEAD, sz(8), cnt(61), ICON, { 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F }, sz(16), cnt(2), IREL, END, END }));
	add("padding 63 bytes then 16-bit X/Y",
		cat({ MOUSE_HEAD, sz(8), cnt(63), ICON, { 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F }, sz(16), cnt(2), IREL, END, END }));
	add("ID 1, padding 62 bytes then X/Y", cat({ MOUSE_HEAD, id(1), sz(8), cnt(62), ICON, XY, sz(8), cnt(2), IREL, END, END }));
	add("ID 1, padding 64 bytes then X/Y", cat({ MOUSE_HEAD, id(1), sz(8), cnt(64), ICON, XY, sz(8), cnt(2), IREL, END, END }));
	add("keyboard with padding so the key array straddles 512 bits",
		cat({ { 0x05, 0x01, 0x09, 0x06 }, APP, { 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(8), IVAR, sz(8), cnt(59), ICON,
			{ 0x15, 0, 0x25, 0x65, 0x19, 0, 0x29, 0x65 }, sz(8), cnt(6), IARR, END }));
	add("LED output with more count than usages, then input",
		cat({ { 0x05, 0x01, 0x09, 0x06 }, APP, { 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(8), IVAR,
			{ 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x05, 0x07 }, cnt(8), OVAR, { 0x19, 0xE0, 0x29, 0xE7 }, cnt(8), sz(1), IVAR, sz(8), cnt(6),
			{ 0x25, 0x65, 0x19, 0, 0x29, 0x65 }, IARR, END }));
	add("output Var x300 then input", cat({ { 0x09, 0x01, 0x09, 0x02, 0x09, 0x03, 0x15, 0, 0x25, 1 }, sz(1), cnt(1), OVAR, FVAR, IVAR }));
	add("Var items with no usages at all", cat({ { 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(40), IVAR, cnt(3), OVAR, FVAR }));
	add("input count 3000 with an ID", cat({ id(3), { 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt4(3000), IVAR }));
	add("two applications, second one with its own ID", cat({ plainMouse(), { 0x05, 0x01, 0x09, 0x06 }, APP, id(2), { 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(8), IVAR, END }));
	add("logical collection inside application with usage in it",
		cat({ { 0x05, 0x0D, 0x09, 0x04 }, APP, { 0x09, 0x22 }, LOGC, { 0x09, 0x42, 0x15, 0x00, 0x25, 0x01 }, sz(1), cnt(1), IVAR, END, END }));
	add("report ID changes between items of one application", cat({ MOUSE_HEAD, id(1), BTN3, id(2), XY, sz(8), cnt(2), IREL, id(1), sz(8), cnt(1), IVAR, END, END }));
	return c;
}

} // namespace

namespace {

struct Golden {
	const char *name;
	uint64_t hash;
};

const Golden GOLDENS[] = {
#include "usb_hid_parser_goldens.inc"
};

uint64_t fnv1a(const std::string& text)
{
	uint64_t h = 1469598103934665603ULL;
	for (size_t i = 0; i < text.size(); i++) {
		h = (h ^ (uint8_t)text[i]) * 1099511628211ULL;
	}
	return h;
}

} // namespace

TEST(HidParserIdentityTest, ItemsAndFieldsMatchTheResultsPinnedBeforeTheFix)
{
	SELECTED(ItemsAndFieldsMatchTheResultsPinnedBeforeTheFix);
	std::vector<Corpus> corpus = identityCorpus();
	EXPECT_EQ(corpus.size(), sizeof(GOLDENS) / sizeof(GOLDENS[0]));
	int different = 0;
	for (const Corpus& c : corpus) {
		const Golden *g = 0;
		for (const Golden& candidate : GOLDENS) {
			if (c.name == candidate.name) {
				g = &candidate;
			}
		}
		Verdict now = parse<Prod>(c.d);
		if (!g) {
			different++;
			fprintf(stderr, "  NO GOLDEN: %s\n", c.name.c_str());
		} else if (fnv1a(now.text) != g->hash) {
			different++;
			fprintf(stderr, "  DIFFERENT: %s (hash %016llx, pinned %016llx)\n%s", c.name.c_str(),
				(unsigned long long)fnv1a(now.text), (unsigned long long)g->hash, now.text.substr(0, 3000).c_str());
		}
		if (now.leaked != 0) {
			different++;
			fprintf(stderr, "  LEAK: %s: %ld blocks\n", c.name.c_str(), now.leaked);
		}
	}
	EXPECT_EQ(different, 0);
}

TEST(HidParserIdentityTest, CorpusCoversKeyboardsMiceAndNeither)
{
	SELECTED(CorpusCoversKeyboardsMiceAndNeither);
	int kbd = 0, mouse = 0, neither = 0;
	for (const Corpus& c : identityCorpus()) {
		Verdict now = parse<Prod>(c.d);
		kbd += now.keyboard;
		mouse += now.mouse;
		neither += !now.keyboard && !now.mouse;
	}
	EXPECT_TRUE(kbd >= 10);
	EXPECT_TRUE(mouse >= 15);
	EXPECT_TRUE(neither >= 10);
}
