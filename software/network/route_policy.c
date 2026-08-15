/*
 * route_policy.c
 *
 * The decision described in route_policy.h, and nothing else. See that header
 * for why it exists and why it is separate from the lwIP hook that calls it.
 */

#include "route_policy.h"

int route_choose(const RouteCandidate *candidates, int count,
                 uint32_t source, uint32_t destination)
{
    int index;
    int best = -1;

    if (candidates == 0 || count <= 0) {
        return -1;
    }

    if (source != 0) {
        /* The caller bound a source address, so the interface is already
         * decided: it is the one that holds that address. An address no
         * usable interface holds is not this function's to place, and
         * answering -1 leaves the stack to reject or route it as it would
         * have done. */
        for (index = 0; index < count; index++) {
            if (candidates[index].usable
                && candidates[index].address == source) {
                return index;
            }
        }
        return -1;
    }

    for (index = 0; index < count; index++) {
        const RouteCandidate *candidate = &candidates[index];
        if (!candidate->usable || candidate->address == 0) {
            continue;
        }
        if ((candidate->address & candidate->netmask)
            != (destination & candidate->netmask)) {
            /* This interface cannot reach the destination directly, so the
             * preference does not come into it. */
            continue;
        }
        if (best < 0 || candidate->preference > candidates[best].preference) {
            best = index;
        }
    }
    return best;
}
