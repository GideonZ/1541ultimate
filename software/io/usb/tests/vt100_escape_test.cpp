// What Keyboard_VT100 does with an ESC byte.
//
// A terminal sends ESC alone for the ESC key and ESC plus more bytes for every
// arrow, function and keypad key. The decoder reads one byte per getch() call,
// so it cannot tell the two apart when it has only the ESC: it has to wait and
// see whether anything follows. These tests hold the millisecond timer still
// and advance it deliberately, so the waiting is exercised without any real
// delay and without depending on how fast the machine running them is.
//
// The stream stands in for a socket or a UART. Both return -1 for "no byte
// right now", and SocketStream::get_char() also returns it for each byte of a
// Telnet IAC command it swallows, which is why the decoder measures the gap
// instead of treating one -1 as proof the ESC was alone.

// Ahead of the firmware headers: integer.h defines a `max` macro that the
// standard containers cannot be parsed after.
#include <deque>

#include "host_test/host_test.h"
#include "keyboard_vt100.h"

// Stream::format() is defined in the header, so the compiler emits it into this
// binary even though nothing here formats anything. The firmware's formatter
// writes to a serial port that does not exist on a host, so the symbol is
// satisfied rather than the whole of small_printf.cc being linked in.
int _my_vprintf(void (*)(char, void **), void **, const char *, va_list)
{
	return 0;
}

namespace {

// A stream whose bytes the test supplies. -1 means the stream has nothing to
// give, which is what both a quiet socket and a polled UART return.
class ScriptedStream : public Stream
{
	std::deque<int> bytes;
public:
	void feed(int c) { bytes.push_back(c); }
	void feed(const char *text)
	{
		while (*text) {
			bytes.push_back((unsigned char)*text++);
		}
	}
	int get_char(void)
	{
		if (bytes.empty()) {
			return -1;
		}
		int c = bytes.front();
		bytes.pop_front();
		return c;
	}
	bool empty(void) const { return bytes.empty(); }
};

// Poll the decoder until it produces a key or `polls` calls have gone by
// without one. Every caller here knows how many bytes it fed, so a key that
// needs more polls than that is a defect rather than a slow decoder.
int read_key(Keyboard_VT100& keyboard, int polls)
{
	for (int i = 0; i < polls; i++) {
		int key = keyboard.getch();
		if (key != -1) {
			return key;
		}
	}
	return -1;
}

} // namespace

// The complaint this exists for: a lone ESC over Telnet did nothing until
// another key was pressed, and that key was then lost.
TEST(Vt100Escape, LoneEscapeArrivesAfterTheGap)
{
	host_test_set_ms_timer(1000);
	ScriptedStream stream;
	Keyboard_VT100 keyboard(&stream);

	stream.feed('\e');
	EXPECT_EQ(keyboard.getch(), -1);  // the ESC is read, nothing is decided yet

	// Still within the gap, so the ESC may yet turn out to be an arrow key.
	host_test_advance_ms_timer(VT100_ESCAPE_ALONE_MS - 1);
	EXPECT_EQ(keyboard.getch(), -1);

	host_test_advance_ms_timer(1);
	EXPECT_EQ(keyboard.getch(), '\e');

	// Delivered once, not on every poll after it.
	EXPECT_EQ(keyboard.getch(), -1);
}

// The other half of the complaint: the byte that finally released the ESC used
// to be swallowed.
TEST(Vt100Escape, ByteAfterEscapeIsKeptAndDeliveredNext)
{
	host_test_set_ms_timer(0);
	ScriptedStream stream;
	Keyboard_VT100 keyboard(&stream);

	stream.feed("\ex");
	EXPECT_EQ(read_key(keyboard, 2), '\e');
	EXPECT_EQ(read_key(keyboard, 2), 'x');
}

// The regression the earlier attempt at this caused: a decoder that announced
// the ESC on the first -1 turned arrow keys into an ESC followed by two typed
// characters. In the monitor, ESC leaves, so the keys that followed went
// somewhere else entirely.
TEST(Vt100Escape, ArrowKeyIsNotBrokenUpByAGapInsideIt)
{
	host_test_set_ms_timer(0);
	ScriptedStream stream;
	Keyboard_VT100 keyboard(&stream);

	stream.feed('\e');
	EXPECT_EQ(keyboard.getch(), -1);

	// The rest of the sequence has not arrived. This is the -1 that must not be
	// read as "the ESC was alone": a Telnet IAC byte produces exactly this.
	EXPECT_EQ(keyboard.getch(), -1);
	host_test_advance_ms_timer(VT100_ESCAPE_ALONE_MS - 1);
	EXPECT_EQ(keyboard.getch(), -1);

	stream.feed("[B");
	EXPECT_EQ(read_key(keyboard, 2), KEY_DOWN);
}

// Every sequence the decoder knows still decodes to its key, with the whole
// sequence available from the start.
TEST(Vt100Escape, KnownSequencesStillDecode)
{
	host_test_set_ms_timer(0);
	ScriptedStream stream;
	Keyboard_VT100 keyboard(&stream);

	stream.feed("\e[A");
	EXPECT_EQ(read_key(keyboard, 3), KEY_UP);
	stream.feed("\e[C");
	EXPECT_EQ(read_key(keyboard, 3), KEY_RIGHT);
	stream.feed("\e[D");
	EXPECT_EQ(read_key(keyboard, 3), KEY_LEFT);
	stream.feed("\eOP");
	EXPECT_EQ(read_key(keyboard, 3), KEY_F1);
	stream.feed("\e[5~");
	EXPECT_EQ(read_key(keyboard, 4), KEY_PAGEUP);
	stream.feed("\eb");
	EXPECT_EQ(read_key(keyboard, 3), KEY_CTRL_B);
}

// Holding ESC repeats it rather than arming one that never arrives.
TEST(Vt100Escape, SecondEscapeReleasesTheFirstAndIsMeasuredOnItsOwn)
{
	host_test_set_ms_timer(0);
	ScriptedStream stream;
	Keyboard_VT100 keyboard(&stream);

	stream.feed("\e\e");
	EXPECT_EQ(read_key(keyboard, 2), '\e');

	// The second ESC is now the one being waited on, from when it was read.
	EXPECT_EQ(keyboard.getch(), -1);
	host_test_advance_ms_timer(VT100_ESCAPE_ALONE_MS);
	EXPECT_EQ(keyboard.getch(), '\e');
}

// The millisecond timer is 16 bits and wraps about every 65 seconds. An ESC
// pressed just before a wrap must still be delivered on the gap, not held until
// the timer has come all the way round again.
TEST(Vt100Escape, GapIsMeasuredAcrossATimerWrap)
{
	host_test_set_ms_timer((uint16_t)(0 - (VT100_ESCAPE_ALONE_MS / 2)));
	ScriptedStream stream;
	Keyboard_VT100 keyboard(&stream);

	stream.feed('\e');
	EXPECT_EQ(keyboard.getch(), -1);

	host_test_advance_ms_timer(VT100_ESCAPE_ALONE_MS);
	EXPECT_EQ(keyboard.getch(), '\e');
}

// A key the user interface pushes in is not displaced by a byte handed back.
TEST(Vt100Escape, PushedKeyOutranksAHandedBackByte)
{
	host_test_set_ms_timer(0);
	ScriptedStream stream;
	Keyboard_VT100 keyboard(&stream);

	keyboard.push_head('q');
	stream.feed("\ex");
	EXPECT_EQ(read_key(keyboard, 2), 'q');
	EXPECT_EQ(read_key(keyboard, 2), '\e');
}

// clear_buffer() drops a half-read sequence, so the ESC that armed the decoder
// must not be delivered afterwards.
TEST(Vt100Escape, ClearBufferDropsAPendingEscape)
{
	host_test_set_ms_timer(0);
	ScriptedStream stream;
	Keyboard_VT100 keyboard(&stream);

	stream.feed('\e');
	EXPECT_EQ(keyboard.getch(), -1);

	keyboard.clear_buffer();
	host_test_advance_ms_timer(VT100_ESCAPE_ALONE_MS * 2);
	EXPECT_EQ(keyboard.getch(), -1);
}
