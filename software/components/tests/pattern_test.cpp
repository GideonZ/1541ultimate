#include "../../io/usb/tests/host_test/host_test.h"
#include "../pattern.h"

#include <string.h>

namespace {

// ensure_extension() is the naming policy used when copying a file from one
// filesystem to another. FileInfo::extension means two different things
// depending on the source filesystem:
//
//  - On FAT it is *derived* from the filename (filesystem_fat.h), truncated to
//    three characters by get_extension(). The name already carries it.
//  - On D64/T64/ISO it is CBM type *metadata* ("PRG", "SEQ", "REL", ...) that
//    does not appear in the filename at all (filesystem_d64.cc).
//
// So the extension may only be appended when the name does not already end in
// it. Rewriting it unconditionally destroys long FAT extensions.

const char *ensured(char *buffer, const char *ext)
{
    ensure_extension(buffer, ext, 100);
    return buffer;
}

} // namespace

// --- FAT sources: the name already carries its extension -------------------

TEST(EnsureExtensionTest, KeepsExtensionLongerThanThreeCharacters)
{
    char name[100] = "foo.bar1234";
    EXPECT_EQ(strcmp(ensured(name, "BAR"), "foo.bar1234"), 0);
}

TEST(EnsureExtensionTest, DistinctLongExtensionsDoNotCollide)
{
    char first[100] = "foo.bar1234";
    char second[100] = "foo.bar5678";
    ensure_extension(first, "BAR", 100);
    ensure_extension(second, "BAR", 100);
    EXPECT_NE(strcmp(first, second), 0);
}

TEST(EnsureExtensionTest, DoesNotDuplicateMatchingExtension)
{
    char name[100] = "foo.prg";
    EXPECT_EQ(strcmp(ensured(name, "PRG"), "foo.prg"), 0);
}

TEST(EnsureExtensionTest, MatchesExtensionCaseInsensitively)
{
    char name[100] = "foo.PRG";
    EXPECT_EQ(strcmp(ensured(name, "prg"), "foo.PRG"), 0);
}

TEST(EnsureExtensionTest, KeepsMultiPartExtension)
{
    char name[100] = "archive.tar.gz";
    EXPECT_EQ(strcmp(ensured(name, "GZ"), "archive.tar.gz"), 0);
}

TEST(EnsureExtensionTest, EmptyMetadataExtensionLeavesNameAlone)
{
    char name[100] = "foo.bar1234";
    EXPECT_EQ(strcmp(ensured(name, ""), "foo.bar1234"), 0);
}

TEST(EnsureExtensionTest, NameWithoutDotAndWithoutMetadataIsUnchanged)
{
    char name[100] = "readme";
    EXPECT_EQ(strcmp(ensured(name, ""), "readme"), 0);
}

// --- CBM sources: the extension is type metadata, not part of the name -----

TEST(EnsureExtensionTest, AppendsCbmTypeToNameWithoutExtension)
{
    char name[100] = "FOO";
    EXPECT_EQ(strcmp(ensured(name, "PRG"), "FOO.prg"), 0);
}

TEST(EnsureExtensionTest, AppendsCbmTypeWithoutDestroyingDotInCbmName)
{
    char name[100] = "MY.DOC";
    EXPECT_EQ(strcmp(ensured(name, "PRG"), "MY.DOC.prg"), 0);
}

TEST(EnsureExtensionTest, AppendsCbmTypeWhenNameEndsInDot)
{
    char name[100] = "FOO.";
    EXPECT_EQ(strcmp(ensured(name, "PRG"), "FOO..prg"), 0);
}

// --- buffer safety ---------------------------------------------------------

TEST(EnsureExtensionTest, RespectsBufferSizeWhenAppending)
{
    char name[12] = "abcdefgh";
    ensure_extension(name, "PRG", 12);
    EXPECT_EQ(strcmp(name, "abcdefg.prg"), 0);
}

TEST(EnsureExtensionTest, LeavesNameAloneWhenBufferCannotHoldExtension)
{
    char name[4] = "abc";
    ensure_extension(name, "PRG", 4);
    EXPECT_EQ(strcmp(name, "abc"), 0);
}
