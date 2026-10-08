/*
 * Host side tests for the SID player's per-UltiSID model rule.
 *
 *   make -C target/pc/linux/ultisidmodels test
 *
 * The real software/u64/ultisid_models.h is compiled here, so what runs is the
 * code that ships. Slots follow U64Config::MapSid(): 0/1 sockets, 2/3 UltiSIDs,
 * 4/5 the second SIDs of the sockets, 6/7 the second SIDs of the UltiSIDs.
 */
#include <stdio.h>
#include "ultisid_models.h"

static int failures;
static int checks;

static void check(bool condition, const char *what)
{
    checks++;
    if (!condition) {
        failures++;
        printf("  FAIL  %s\n", what);
    } else {
        printf("  ok    %s\n", what);
    }
}

enum { EITHER = 3, M6581 = 1, M8580 = 2 };

int main(void)
{
    UltiSidModels m;

    printf("Slots\n");
    check(UltiSidModels::ultisidOf(2) == 0 && UltiSidModels::ultisidOf(6) == 0, "slots 2 and 6 are UltiSID 1");
    check(UltiSidModels::ultisidOf(3) == 1 && UltiSidModels::ultisidOf(7) == 1, "slots 3 and 7 are UltiSID 2");
    check(UltiSidModels::ultisidOf(0) < 0 && UltiSidModels::ultisidOf(1) < 0, "the sockets are no UltiSID");
    check(UltiSidModels::ultisidOf(4) < 0 && UltiSidModels::ultisidOf(5) < 0, "the sockets' second SIDs are no UltiSID");

    printf("A mixed tune on one UltiSID\n");
    m.clear();
    check(m.fits(2, M6581), "a 6581 fits an unclaimed UltiSID 1");
    check(m.claim(2, M6581), "the first SID on UltiSID 1 sets its model");
    check(!m.fits(6, M8580), "an 8580 does not fit slot 6 once UltiSID 1 is a 6581");
    check(m.fits(6, M6581), "a second 6581 does fit slot 6");
    check(m.fits(7, M8580), "UltiSID 2 is still free for the 8580");
    check(!m.claim(6, M8580), "claiming slot 6 for an 8580 anyway leaves UltiSID 1 a 6581");
    check(!m.fits(2, M8580), "and UltiSID 1 still refuses an 8580 afterwards");

    printf("Either model\n");
    m.clear();
    check(!m.claim(2, EITHER), "a SID that takes either model claims nothing");
    check(m.fits(6, M8580), "so slot 6 stays open to an 8580");
    check(m.fits(6, EITHER) && m.fits(2, EITHER), "and a SID that takes either model fits anywhere");

    printf("Not an UltiSID\n");
    m.clear();
    check(!m.claim(4, M8580), "slot 4 sets no UltiSID model");
    check(m.fits(2, M6581) && m.fits(6, M6581), "and UltiSID 1 stays unclaimed after it");
    check(!m.claim(5, M6581), "slot 5 sets no UltiSID model");
    check(m.fits(3, M8580) && m.fits(7, M8580), "and UltiSID 2 stays unclaimed after it");

    printf("A new mapping pass\n");
    m.claim(2, M6581);
    m.claim(3, M8580);
    m.clear();
    check(m.fits(6, M8580) && m.fits(7, M6581), "clear() releases both UltiSIDs");

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
