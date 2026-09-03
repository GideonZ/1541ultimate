// Regression tests for GideonZ/1541ultimate#823: a G64's track metadata used to
// program the drive engine's memory bounds without ever being checked against
// the buffer that holds the image.
//
// Two of the four problems in that issue are pinned down here; the parameter
// RAM side follows in its own change.
//
//   2. track length never validated against the buffer      -> ValidatedLength
//   4. divide by zero on a zero-length track                -> ValidatedLength

#include "../../io/usb/tests/host_test/host_test.h"
#include "../gcr_track_bounds.h"

namespace {

// The real constants, so the numbers here are the ones the firmware uses.
const uint32_t kMaxSize      = (0x1EF8 * 80) + (12 + (168 * 10));  // GCRIMAGE_MAXSIZE
const int      kMaxTrackLen  = 0x1EF8;                             // GCRIMAGE_MAXTRACKLEN

} // namespace

// ---------------------------------------------------------------- problem 2 --

TEST(ValidatedLength, AcceptsALegitimateTrack)
{
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, 0x2000, kMaxSize, kMaxTrackLen), 0x1E0C);
}

TEST(ValidatedLength, KeepsTheLongestLegalTrack)
{
    EXPECT_EQ(gcr_validated_track_length(kMaxTrackLen, 0x2000, kMaxSize, kMaxTrackLen), kMaxTrackLen);
}

TEST(ValidatedLength, IgnoresTheFlagBitsAboveTheLength)
{
    // Bit 15 is the MFM marker; it must not be read as part of the length.
    EXPECT_EQ(gcr_validated_track_length(0x8000 | 0x1E0C, 0x2000, kMaxSize, kMaxTrackLen), 0x1E0C);
}

TEST(ValidatedLength, RejectsALengthAboveTheFormatMaximum)
{
    // 14 bits admit 16383, more than twice the longest real track. Before the
    // fix this was programmed as-is and the engine read and wrote past the
    // allocation.
    EXPECT_EQ(gcr_validated_track_length(0x3FFF, 0x2000, kMaxSize, kMaxTrackLen), 0);
}

TEST(ValidatedLength, RejectsATrackRunningOffTheEndOfTheBuffer)
{
    // Legal length, but placed so that it does not fit behind its own header.
    const uint32_t offset = kMaxSize - 100;
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, offset, kMaxSize, kMaxTrackLen), 0);
}

TEST(ValidatedLength, AcceptsATrackEndingExactlyAtTheBufferEnd)
{
    const uint32_t offset = kMaxSize - 0x1E0C - 2;
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, offset, kMaxSize, kMaxTrackLen), 0x1E0C);
}

TEST(ValidatedLength, RejectsATrackOneByteTooLongForTheBuffer)
{
    const uint32_t offset = kMaxSize - 0x1E0C - 1;
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, offset, kMaxSize, kMaxTrackLen), 0);
}

TEST(ValidatedLength, SurvivesAnOffsetPastTheBuffer)
{
    // The caller rejects these first, but the arithmetic here must not wrap.
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, kMaxSize + 1, kMaxSize, kMaxTrackLen), 0);
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, 0xFFFFFFFFu, kMaxSize, kMaxTrackLen), 0);
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, kMaxSize - 1, kMaxSize, kMaxTrackLen), 0);
}

// ---------------------------------------------------------------- problem 4 --

TEST(ValidatedLength, RejectsAZeroLengthTrack)
{
    // insert_disk() divides the rotation speed by the track length, so a track
    // declared zero bytes long used to reach a division by zero.
    EXPECT_EQ(gcr_validated_track_length(0x0000, 0x2000, kMaxSize, kMaxTrackLen), 0);
    EXPECT_EQ(gcr_validated_track_length(0x8000, 0x2000, kMaxSize, kMaxTrackLen), 0);
}

// ------------------------------------------------------- the header itself --

TEST(HeaderReadable, RejectsTheAbsentTrackMarker)
{
    EXPECT_FALSE(gcr_track_header_is_readable(0, 4096));
}

TEST(HeaderReadable, AcceptsAHeaderWhollyInsideWhatWasRead)
{
    EXPECT_TRUE(gcr_track_header_is_readable(4094, 4096));
}

TEST(HeaderReadable, RejectsAHeaderStraddlingTheEndOfWhatWasRead)
{
    // One byte of the length word is in the file, the other is stale buffer.
    EXPECT_FALSE(gcr_track_header_is_readable(4095, 4096));
    EXPECT_FALSE(gcr_track_header_is_readable(4096, 4096));
}

TEST(HeaderReadable, RejectsAPointerPastWhatWasRead)
{
    EXPECT_FALSE(gcr_track_header_is_readable(8192, 4096));
    EXPECT_FALSE(gcr_track_header_is_readable(0xFFFFFFFFu, 4096));
}
