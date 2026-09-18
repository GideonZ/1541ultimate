// Regression tests for GideonZ/1541ultimate#823: a G64's track metadata used to
// program the drive engine's memory bounds without ever being checked against
// the buffer that holds the image.
//
// The four problems in that issue, and where each is pinned down below:
//
//   1. parameter RAM programmed from uninitialised locals   -> TrackParameters
//   2. track length never validated against the buffer      -> ValidatedLength
//   3. half-tracks inherit the previous track's length      -> TrackParameters
//   4. divide by zero on a zero-length track                -> ValidatedLength
//
// And what checking the buffer alone still let through, found in review of the
// first fix: a truncated file whose track header arrived but whose track data
// did not -> TruncatedImage.
//
// And what checking the declared extent still let through, found in review of
// the second fix: a track short enough that the MFM mapping's own, larger
// assumption about it does not hold -> MfmHeader.

#include "../../io/usb/tests/host_test/host_test.h"
#include "../gcr_track_bounds.h"

namespace {

// The real constants, so the numbers here are the ones the firmware uses.
const uint32_t kMaxSize      = (0x1EF8 * 80) + (12 + (168 * 10));  // GCRIMAGE_MAXSIZE
const int      kMaxTrackLen  = 0x1EF8;                             // GCRIMAGE_MAXTRACKLEN
const int      kDummyLen     = 0x1E0C;                             // GCRIMAGE_DUMMYTRACKLEN
const uint32_t kRotationSpeed = 50000000 / 20;                     // CLOCK_FREQ / 20 on the U64
const int      kMfmHeaderSize = 2 + (32 * 5);                      // MFM_TRACK_HEADER_SIZE

} // namespace

// ---------------------------------------------------------------- problem 2 --
//
// The third argument is how much of the buffer the file filled. Passing
// kMaxSize here models an image that filled it completely, which is what the
// buffer-overrun cases below are about; the truncated cases further down pass
// the smaller number a short file leaves behind.

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

// ------------------------------------------------------------ truncation --
//
// Reported by @chrisgleissner against the first version of this fix, which
// bounded the track by the size of the buffer rather than by what the file
// delivered. The buffer is not zeroed between mounts, so what lies past the
// end of a short file is the previous image.

TEST(TruncatedImage, RejectsATrackWhoseDataNeverArrived)
{
    // The reported case, exactly: a 14-byte GCR-1541 file. Track 0 points at
    // offset 12, the two length bytes are there, and they declare a full
    // 0x1E0C track. Against the buffer this passes; against the file it must
    // not, or gcr_data+14 .. gcr_data+7705 go to the drive unread.
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, 12, 14, kMaxTrackLen), 0);
}

TEST(TruncatedImage, RejectsATrackCutShortByOneByte)
{
    const uint32_t bytes_read = 12 + 2 + 0x1E0C - 1;
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, 12, bytes_read, kMaxTrackLen), 0);
}

TEST(TruncatedImage, AcceptsATrackEndingExactlyWhereTheFileDoes)
{
    const uint32_t bytes_read = 12 + 2 + 0x1E0C;
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, 12, bytes_read, kMaxTrackLen), 0x1E0C);
}

TEST(TruncatedImage, DoesNotCareHowLargeTheBufferIs)
{
    // Same track, same short file. The only difference between these two calls
    // used to be the answer: the buffer was the bound, so the whole image was
    // judged by memory that had nothing to do with it.
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, 12, kMaxSize, kMaxTrackLen), 0x1E0C);
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, 12, 14, kMaxTrackLen), 0);
}

TEST(TruncatedImage, SurvivesAnEmptyRead)
{
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, 12, 0, kMaxTrackLen), 0);
    EXPECT_EQ(gcr_validated_track_length(0x1E0C, 0, 0, kMaxTrackLen), 0);
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

// ------------------------------------------------------------- problems 1+3 --

TEST(TrackParameters, ProgramsARealTrackFromItsOwnLength)
{
    uint32_t address = 0, param = 0;
    gcr_track_parameters(0x00800000, 0x1E0C, 0x00900000, kDummyLen, kRotationSpeed,
                         &address, &param);
    EXPECT_EQ(address, 0x00800000u);
    EXPECT_EQ(param & 0xFFFFu, 0x1E0Bu);                       // last valid offset
    EXPECT_EQ(param >> 16, kRotationSpeed / 0x1E0C);            // bit time
}

TEST(TrackParameters, GivesAnAbsentTrackTheDummyAndASafeLength)
{
    // Problem 1: before the fix the else branch read two locals that the if
    // branch had never written, so the first track of an image whose tracks
    // are all absent programmed dummy_track with whatever was on the stack.
    uint32_t address = 0, param = 0;
    gcr_track_parameters(0, 0, 0x00900000, kDummyLen, kRotationSpeed, &address, &param);
    EXPECT_EQ(address, 0x00900000u);
    EXPECT_EQ(param & 0xFFFFu, (uint32_t)GCR_EMPTY_TRACK_PARAM);
    EXPECT_EQ(param >> 16, kRotationSpeed / (uint32_t)kDummyLen);
}

TEST(TrackParameters, DoesNotCarryTheNeighbourLengthIntoAnAbsentTrack)
{
    // Problem 3: add_blank_tracks() fills only even indices, so every odd one
    // takes the absent path on every image. It used to be handed the dummy
    // track's address together with the length left behind by the preceding
    // track, a pairing that is safe only because the longest entry in
    // track_lengths[] happens to equal GCRIMAGE_DUMMYTRACKLEN exactly.
    //
    // What has to hold is that the absent case does not depend on what came
    // before it. So: program a long real track, then an absent one, and
    // require the same answer as an absent track programmed on its own.
    uint32_t alone_address = 0, alone_param = 0;
    gcr_track_parameters(0, 0, 0x00900000, kDummyLen, kRotationSpeed,
                         &alone_address, &alone_param);

    uint32_t address = 0, param = 0;
    gcr_track_parameters(0x00800000, kMaxTrackLen, 0x00900000, kDummyLen, kRotationSpeed,
                         &address, &param);
    gcr_track_parameters(0, 0, 0x00900000, kDummyLen, kRotationSpeed, &address, &param);

    EXPECT_EQ(address, alone_address);
    EXPECT_EQ(param, alone_param);
    EXPECT_TRUE((int)((param & 0xFFFFu) + 1) <= kDummyLen);
}

TEST(TrackParameters, TreatsAZeroLengthTrackAsAbsent)
{
    // Problem 4 again, one layer down: an address with a zero length must not
    // reach the division either.
    uint32_t address = 0, param = 0;
    gcr_track_parameters(0x00800000, 0, 0x00900000, kDummyLen, kRotationSpeed,
                         &address, &param);
    EXPECT_EQ(address, 0x00900000u);
    EXPECT_EQ(param & 0xFFFFu, (uint32_t)GCR_EMPTY_TRACK_PARAM);
}

TEST(TrackParameters, TreatsANegativeLengthAsAbsent)
{
    uint32_t address = 0, param = 0;
    gcr_track_parameters(0x00800000, -1, 0x00900000, kDummyLen, kRotationSpeed,
                         &address, &param);
    EXPECT_EQ(address, 0x00900000u);
}

// --------------------------------------------------------- the MFM header --
//
// Reported by @chrisgleissner against the second version of this fix. The
// length check above proves that the bytes a track declares were delivered.
// map_gcr_image_to_mfm() then makes a second and larger assumption about the
// same track: that it is long enough to carry the MFM metadata area, which it
// reads whole and reserves the remainder behind.

TEST(MfmHeader, RejectsATrackTooShortToCarryTheMetadata)
{
    EXPECT_FALSE(gcr_track_can_hold_mfm_header(1, kMfmHeaderSize));
    EXPECT_FALSE(gcr_track_can_hold_mfm_header(kMfmHeaderSize - 1, kMfmHeaderSize));
}

TEST(MfmHeader, AcceptsATrackExactlyAsLongAsTheMetadata)
{
    EXPECT_TRUE(gcr_track_can_hold_mfm_header(kMfmHeaderSize, kMfmHeaderSize));
    EXPECT_EQ(gcr_mfm_reserved_space(kMfmHeaderSize, kMfmHeaderSize), 0u);
}

TEST(MfmHeader, ReservesWhatLiesBehindTheMetadata)
{
    EXPECT_EQ(gcr_mfm_reserved_space(0x1E0C, kMfmHeaderSize), (uint32_t)(0x1E0C - 162));
    EXPECT_EQ(gcr_mfm_reserved_space(1000, kMfmHeaderSize), 838u);
}

TEST(MfmHeader, NeverUnderflowsTheReservedSpace)
{
    // The field is a uint32_t and gates every track write. A short track used
    // to reach it as a subtraction, so "1 - 162" became 4294967135 and
    // UpdateTrack() admitted a write of any size at all.
    EXPECT_EQ(gcr_mfm_reserved_space(1, kMfmHeaderSize), 0u);
    EXPECT_EQ(gcr_mfm_reserved_space(0, kMfmHeaderSize), 0u);
    EXPECT_EQ(gcr_mfm_reserved_space(-1, kMfmHeaderSize), 0u);
    for (int len = -4; len < kMfmHeaderSize; len++) {
        EXPECT_EQ(gcr_mfm_reserved_space(len, kMfmHeaderSize), 0u);
    }
}

TEST(MfmHeader, TheReportedEndOfBufferCase)
{
    // @chrisgleissner's file, exactly: GCRIMAGE_MAXSIZE bytes long, track 0
    // pointing at GCRIMAGE_MAXSIZE - 3, declaring 0x8001 -- one byte of track,
    // marked MFM -- with a sector count of 32 in the final byte of the file.
    const uint32_t offset = kMaxSize - 3;
    const uint16_t declared = 0x8001;

    // The header is readable and the one declared byte really was delivered,
    // so the length check passes it. That is correct and not the bug.
    EXPECT_TRUE(gcr_track_header_is_readable(offset, kMaxSize));
    EXPECT_EQ(gcr_validated_track_length(declared, offset, kMaxSize, kMaxTrackLen), 1);

    // The mapping is where it has to stop. Believing the marker would read the
    // full 162-byte metadata area from a track holding one byte at the very end
    // of the buffer, so the sector loop runs past the allocation.
    const int length = gcr_validated_track_length(declared, offset, kMaxSize, kMaxTrackLen);
    EXPECT_FALSE(gcr_track_can_hold_mfm_header(length, kMfmHeaderSize));
    EXPECT_EQ(gcr_mfm_reserved_space(length, kMfmHeaderSize), 0u);
}

TEST(MfmHeader, TreatsAnAbsurdHeaderSizeAsNoHeader)
{
    EXPECT_FALSE(gcr_track_can_hold_mfm_header(0x1E0C, 0));
    EXPECT_FALSE(gcr_track_can_hold_mfm_header(0x1E0C, -162));
    EXPECT_EQ(gcr_mfm_reserved_space(0x1E0C, 0), 0u);
}
