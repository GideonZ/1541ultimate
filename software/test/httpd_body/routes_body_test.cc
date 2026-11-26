// The response bodies the REST API hands to the HTTP server (api/routes.h
// ResponseWrapper: JSON, HTML and attachment replies all stream through
// stream_body) must be released when the server abandons the reply with
// BodyCB(ctx, NULL, -1), as it does when the client goes away. Run under
// AddressSanitizer with leak detection (make test).
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "routes.h"

extern "C" void outbyte(int c) { char ch = (char)c; (void)!write(1, &ch, 1); }

static int failures = 0;
static void check(bool ok, const char *name)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}

int main()
{
    {   // abandoned JSON reply
        HTTPRespMessage resp;
        memset(&resp, 0, sizeof resp);
        ResponseWrapper w(&resp);
        w.json->add("a", 1);
        w.json_response(200);
        bool armed = resp.BodyCB != NULL;
        resp.BodyCB(resp.BodyContext, NULL, -1);
        check(armed, "json_reply_has_a_body_to_abort");
    }
    {   // abandoned HTML reply
        HTTPRespMessage resp;
        memset(&resp, 0, sizeof resp);
        ResponseWrapper w(&resp);
        w.html_response(200, "t", "text %d", 1);
        resp.BodyCB(resp.BodyContext, NULL, -1);
        check(true, "html_reply_abandoned");
    }
    {   // abandoned attachment reply
        HTTPRespMessage resp;
        memset(&resp, 0, sizeof resp);
        ResponseWrapper w(&resp);
        StreamRamFile *att = w.add_attachment();
        att->format("0123456789");
        w.binary_response();
        resp.BodyCB(resp.BodyContext, NULL, -1);
        check(true, "attachment_reply_abandoned");
    }
    {   // a reply read to the end still works and frees itself at the end
        HTTPRespMessage resp;
        memset(&resp, 0, sizeof resp);
        ResponseWrapper w(&resp);
        w.json->add("a", 1);
        w.json_response(200);
        uint8_t buf[1024];
        int total = 0, n;
        while ((n = resp.BodyCB(resp.BodyContext, buf, sizeof buf)) > 0) total += n;
        check(total > 0 && n == 0, "complete_reply_streams_to_the_end");
    }
    printf("routes_body: %d failed\n", failures);
    return failures ? 1 : 0;
}
