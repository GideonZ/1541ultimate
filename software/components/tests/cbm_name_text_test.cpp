// The browser's CBM names view (issue 976): the text a PETSCII name is shown and edited as,
// and the CBM name a directory entry is listed under.
#include <stdio.h>
#include <string.h>
#include <string>

#include "host_test/host_test.h"
#include "pattern.h"
#include "file_info.h"

namespace {

const char REFUSED[] = "<refused>"; // never the result of a parse, which yields no 'r' to 'z'

std::string text_of(const char *pet)
{
    char text[80];
    if (!petscii_to_text(pet, text, sizeof(text))) {
        return "<did not fit>";
    }
    return text;
}

std::string parsed(const char *text, int len = 17)
{
    char pet[64];
    int n = text_to_petscii(text, pet, len);
    if (n < 0) {
        return REFUSED;
    }
    EXPECT_EQ(n, (int)strlen(pet));
    return std::string(pet, n);
}

// The CBM name an entry of this host name is listed under, as text, or REFUSED when the host
// name stores none.
std::string listed_as(const char *host, bool dir = false)
{
    FileInfo info(host);
    if (dir) {
        info.attrib = AM_DIR;
    }
    char pet[17];
    if (!info.get_cbm_name(pet)) {
        return REFUSED;
    }
    return text_of(pet);
}

std::string host_of(const std::string& pet, const char *suffix)
{
    char host[64];
    petscii_to_fat(pet.c_str(), host, sizeof(host));
    return std::string(host) + suffix;
}

} // namespace

TEST(CbmNameText, LettersFollowTheLowerUpperCaseSet)
{
    EXPECT_EQ(text_of("\x46\x4F\x4F"), std::string("foo"));
    EXPECT_EQ(text_of("\xC6\xCF\xCF"), std::string("FOO"));
    EXPECT_EQ(text_of("\xC6\x4F\x4F"), std::string("Foo"));
    EXPECT_EQ(text_of("\x41\x5A\xC1\xDA"), std::string("azAZ"));
}

TEST(CbmNameText, DigitsAndPunctuationStandForThemselves)
{
    EXPECT_EQ(text_of(" !\"#$%&'()*+,-./0123456789:;<=>?@[]^_"),
              std::string(" !\"#$%&'()*+,-./0123456789:;<=>?@[]^_"));
}

TEST(CbmNameText, OtherBytesAreWrittenAsEscapeGroups)
{
    EXPECT_EQ(text_of("\x5C"), std::string("{5C}"));        // pound sign
    EXPECT_EQ(text_of("\x61\x7A"), std::string("{617A}"));  // second codes of A and Z
    EXPECT_EQ(text_of("\x01\x0D\x7F"), std::string("{010D7F}"));
    EXPECT_EQ(text_of("\x41\xA0\x42"), std::string("a{A0}b"));
    EXPECT_EQ(text_of("\xA0\x41"), std::string("{A0}a"));
    EXPECT_EQ(text_of("\x41\xA0"), std::string("a{A0}"));
    EXPECT_EQ(text_of("\x41\xA0\x42\xFF\xFE\x43"), std::string("a{A0}b{FFFE}c"));
    EXPECT_EQ(text_of(""), std::string(""));
}

TEST(CbmNameText, EveryByteComesBackFromItsText)
{
    int lost = 0;
    for (int b = 1; b < 256; b++) {
        char pet[2] = { (char)b, 0 };
        if (parsed(text_of(pet).c_str()) != std::string(pet)) {
            if (lost++ < 5) {
                fprintf(stderr, "byte $%02X shown as '%s'\n", b, text_of(pet).c_str());
            }
        }
    }
    EXPECT_EQ(lost, 0);
}

TEST(CbmNameText, EveryTwoByteNameComesBackFromItsText)
{
    int lost = 0;
    for (int a = 1; a < 256; a++) {
        for (int b = 1; b < 256; b++) {
            char pet[3] = { (char)a, (char)b, 0 };
            if ((parsed(text_of(pet).c_str()) != std::string(pet)) && (lost++ < 5)) {
                fprintf(stderr, "name $%02X $%02X shown as '%s'\n", a, b, text_of(pet).c_str());
            }
        }
    }
    EXPECT_EQ(lost, 0);
}

TEST(CbmNameText, SixteenEscapedBytesFitTheBufferTheBrowserUses)
{
    char pet[17];
    char text[4 * 17 + 1];
    for (int i = 0; i < 16; i++) {
        pet[i] = (i & 1) ? (char)0xA0 : 'A'; // the longest text: a group for every other byte
    }
    pet[16] = 0;
    EXPECT_TRUE(petscii_to_text(pet, text, sizeof(text)));
    EXPECT_EQ(parsed(text), std::string(pet));
}

TEST(CbmNameText, TextThatDoesNotFitIsReportedAndStaysInItsBuffer)
{
    char text[16];
    memset(text, 'X', sizeof(text));
    EXPECT_FALSE(petscii_to_text("\x01\x02\x03\x04\x05\x06\x07\x08", text, 10));
    EXPECT_TRUE(strlen(text) < 10);
    EXPECT_EQ(text[0], '{');
    EXPECT_EQ(text[strlen(text) - 1], '}'); // a cut text still closes its group
    for (int i = 10; i < (int)sizeof(text); i++) {
        EXPECT_EQ(text[i], 'X');
    }
}

TEST(CbmNameText, EditedTextIsParsed)
{
    EXPECT_EQ(parsed("Foo"), std::string("\xC6\x4F\x4F"));
    EXPECT_EQ(parsed("{C6}oo"), std::string("\xC6\x4F\x4F"));
    EXPECT_EQ(parsed("{c6}oo"), std::string("\xC6\x4F\x4F"));
    EXPECT_EQ(parsed("a{A0}b"), std::string("\x41\xA0\x42"));
    EXPECT_EQ(parsed("{C1}{C2}"), std::string("\xC1\xC2"));
    EXPECT_EQ(parsed("{C1C2}"), std::string("\xC1\xC2"));
    EXPECT_EQ(parsed("1+2=3?"), std::string("1+2=3?"));
    EXPECT_EQ(parsed(""), std::string(""));
}

TEST(CbmNameText, TextWithoutAMeaningIsRefused)
{
    static const char *refused[] = {
        "{", "{}", "{C}", "{C1C}", "{G0}", "{C6", "a{C6", "{00}", "a{0041}",
        "a~b", "a\\b", "a|b", "a`b", "a\x7F" "b", "a\xA0" "b", "a}b", "a\tb",
    };
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        if (parsed(refused[i]) != REFUSED) {
            fprintf(stderr, "'%s' was accepted\n", refused[i]);
            EXPECT_TRUE(false);
        }
    }
}

TEST(CbmNameText, ParsingRefusesWhatDoesNotFitAndStaysInItsBuffer)
{
    EXPECT_EQ(parsed("abcd", 5), std::string("ABCD"));
    EXPECT_EQ(parsed("abcde", 5), std::string(REFUSED));
    EXPECT_EQ(parsed("abc{C1}", 5), std::string("ABC\xC1"));
    EXPECT_EQ(parsed("abc{C1C2}", 5), std::string(REFUSED));

    char pet[8];
    memset(pet, 'X', sizeof(pet));
    EXPECT_EQ(text_to_petscii("abcdefgh", pet, 5), -1);
    for (int i = 5; i < (int)sizeof(pet); i++) {
        EXPECT_EQ(pet[i], 'X');
    }
}

TEST(CbmNameOfEntry, HostNameIsReadAsTheDriveListsIt)
{
    EXPECT_EQ(listed_as("{C6CFCF}.prg"), std::string("FOO"));
    EXPECT_EQ(listed_as("FOO.PRG"), std::string("foo"));
    EXPECT_EQ(listed_as("foo.prg"), std::string("foo"));
    EXPECT_EQ(listed_as("Foo.prg"), std::string("foo")); // FAT ignores case, so does the drive
    EXPECT_EQ(listed_as("{C6}OO.prg"), std::string("Foo"));
    EXPECT_EQ(listed_as("{c6}oo.SEQ"), std::string("Foo"));
    EXPECT_EQ(listed_as("NOTES.usr"), std::string("notes"));
    EXPECT_EQ(listed_as("DATA.rel"), std::string("data"));
    EXPECT_EQ(listed_as("A{A0}B"), std::string("a{A0}b"));
    EXPECT_EQ(listed_as("A{3A}B{2A}"), std::string("a:b*"));
    EXPECT_EQ(listed_as("{2E}HIDDEN.prg"), std::string(".hidden"));
    EXPECT_EQ(listed_as("X{7B}Y{7D}Z"), std::string("x{7B}y{7D}z"));
}

TEST(CbmNameOfEntry, ExtensionThatIsNoCbmTypeIsPartOfTheName)
{
    EXPECT_EQ(listed_as("GAME.D64"), std::string("game.d64"));
    EXPECT_EQ(listed_as("NOTES.DEL"), std::string("notes.del"));
    EXPECT_EQ(listed_as("NOTES.PRGX"), std::string(REFUSED)); // the drive reads PRG, but cuts nothing
    EXPECT_EQ(listed_as("README"), std::string("readme"));
    EXPECT_EQ(listed_as("GAME.PRG{}.prg"), std::string("game.prg"));
    // The drive never stores a typed name without its extension, so it is not read as one.
    EXPECT_EQ(listed_as("GAME.PRG{}"), std::string(REFUSED));
}

TEST(CbmNameOfEntry, ReservedHostNamesKeepTheirMark)
{
    EXPECT_EQ(listed_as("{}CON.prg"), std::string("con"));
    EXPECT_EQ(listed_as("{}LPT9.PRG{}.prg"), std::string("lpt9.prg"));
    EXPECT_EQ(listed_as("CON.prg"), std::string(REFUSED)); // the drive would store {}CON.prg
}

TEST(CbmNameOfEntry, DirectoryKeepsItsWholeName)
{
    EXPECT_EQ(listed_as("MY.DIR", true), std::string("my.dir"));
    EXPECT_EQ(listed_as("{C7C1CDC5D3}", true), std::string("GAMES"));
    EXPECT_EQ(listed_as("GAMES.PRG{}", true), std::string("games.prg"));
    EXPECT_EQ(listed_as("GAMES.PRG", true), std::string(REFUSED));
}

TEST(CbmNameOfEntry, HostNameThatStoresNoCbmNameIsRefused)
{
    static const char *refused[] = {
        "MY{FILE}.prg",       // braces that open no escape
        "{C1}{C2}",           // the drive writes one group: {C1C2}
        "{C1",                // a group that is never closed
        "A~B",                // a byte the drive would escape
        "my_file~1.prg",
        "{00}A",              // a zero byte
        ".prg",               // no name before the type
        "{}",
        "",
        "ABCDEFGHIJKLMNOPQ.prg", // 17 characters: the drive lists the first 16
        "{C1C2C3C4C5C6C7C8C9CACBCCCDCECFD0D1}",
    };
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        if (listed_as(refused[i]) != REFUSED) {
            fprintf(stderr, "'%s' listed as '%s'\n", refused[i], listed_as(refused[i]).c_str());
            EXPECT_TRUE(false);
        }
    }
    EXPECT_EQ(listed_as("ABCDEFGHIJKLMNOP.prg"), std::string("abcdefghijklmnop"));
}

TEST(CbmNameOfEntry, GeosFileKeepsItsHostName)
{
    // CbmFileName reads .cvt as a type, but the drive lists such a file by its host name.
    EXPECT_EQ(listed_as("GEOS.cvt"), std::string(REFUSED));
    EXPECT_EQ(listed_as("{C7}EOS.CVT"), std::string(REFUSED));
}

TEST(CbmNameOfEntry, CbmFileSystemEntryIsListedUnderItsOwnName)
{
    FileInfo info("\xC6\x4F\x4F");
    info.name_format = NAME_FORMAT_CBM;
    char pet[17];
    EXPECT_TRUE(info.get_cbm_name(pet));
    EXPECT_EQ(text_of(pet), std::string("Foo"));

    FileInfo braces("A{B}C");
    braces.name_format = NAME_FORMAT_CBM;
    EXPECT_TRUE(braces.get_cbm_name(pet));
    EXPECT_EQ(text_of(pet), std::string("a{7B}b{7D}c"));

    FileInfo empty("");
    empty.name_format = NAME_FORMAT_CBM;
    EXPECT_FALSE(empty.get_cbm_name(pet));
}

// What a rename in the CBM names view relies on: every name the drive can store is listed
// under the same name again, and its text, edited back unchanged, stores the same host name.
TEST(CbmNameOfEntry, EveryStoredNameIsListedAndRenamedBackUnchanged)
{
    static const struct { const char *suffix; bool dir; } kinds[] = {
        { ".prg", false }, { ".SEQ", false }, { ".usr", false }, { ".rel", false },
        { "", false }, { "", true },
    };
    int lost = 0;
    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        for (int a = 1; a < 256; a++) {
            for (int b = 0; b < 256; b++) {
                std::string pet = b ? std::string(1, (char)a) + (char)b : std::string(1, (char)a);
                std::string host = host_of(pet, kinds[k].suffix);
                std::string text = listed_as(host.c_str(), kinds[k].dir);
                // A trailing shifted space is the padding of a directory entry, which the
                // drive drops when it stores the name, so what is listed is the rest.
                std::string kept = pet;
                while (!kept.empty() && ((uint8_t)kept[kept.size() - 1] == 0xA0)) {
                    kept.erase(kept.size() - 1);
                }
                bool ok;
                if (kept.empty()) {
                    ok = (text == REFUSED);
                } else {
                    std::string back = parsed(text.c_str());
                    ok = (text != REFUSED) && (back == kept) && (host_of(back, kinds[k].suffix) == host);
                }
                if (!ok && (lost++ < 5)) {
                    fprintf(stderr, "host '%s' (%s) listed as '%s'\n", host.c_str(),
                            kinds[k].dir ? "dir" : "file", text.c_str());
                }
            }
        }
    }
    EXPECT_EQ(lost, 0);
}
