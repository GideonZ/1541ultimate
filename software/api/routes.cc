#include "routes.h"
#include "attachment_writer.h"
#include "attachment_reu.h"
#include "stream_uart.h"
#include "dump_hex.h"
#include "network_config.h"
#include "product.h"
#include "versions.h"
#include "u64.h"
#include <string.h>
#include <strings.h>

Dict<const char *, IndexedList<const ApiCall_t *> *> *getRoutesList(void)
{
    static Dict<const char *, IndexedList<const ApiCall_t *> *> HttpRoutes(10, 0, 0, strcmp);
    return &HttpRoutes;
}

/* File Writer */
void writer_complete(TempfileWriter *writer, const void *context1, void *context2)
{
    const ApiCall_t *func = (const ApiCall_t *)context1;
    ArgsURI *args = (ArgsURI *)context2;
    if (func) {
        // On an aborted (interrupted) upload, free the args without running the
        // incomplete API call.
        if (!writer->is_aborted()) {
            ResponseWrapper respw(writer->get_response());
            if (args->Validate(*func, &respw) != 0) {
                respw.json_response(HTTP_BAD_REQUEST);
            } else {
                func->proc(*args, writer->get_request(), &respw, writer);
            }
        }
        delete args;
    }
    delete writer;
}

TempfileWriter *attachment_writer(HTTPReqMessage *req, HTTPRespMessage *resp, const ApiCall_t *func, ArgsURI *args)
{
    if (req->bodyType != eNoBody) {
        if ((req->bodyType == eTotalSize) && (req->bodySize == 0)) {
            req->bodyType = eNoBody;
            return NULL;
        }
        TempfileWriter *writer = new TempfileWriter(req, resp, writer_complete, func, args);
        setup_multipart(req, &TempfileWriter::collect_wrapper, writer);
        if (!req->BodyCB) {
            // setup_multipart ran out of memory and installed no body callback, so
            // no terminate/abort will ever run: free the writer here and report
            // "no body" so execute_api_v1 also frees args (no leak).
            delete writer;
            return NULL;
        }
        return writer;
    }
    return NULL;
}

/* REU Writer */
REUWriter *attachment_reu(HTTPReqMessage *req, HTTPRespMessage *resp, const ApiCall_t *func, ArgsURI *args)
{
    if (req->bodyType != eNoBody) {
        if ((req->bodyType == eTotalSize) && (req->bodySize == 0)) {
            req->bodyType = eNoBody;
            return NULL;
        }
        REUWriter *writer = new REUWriter();
        writer->create_callback(req, resp, args, (const ApiCall_t *)func);
        setup_multipart(req, &REUWriter::collect_wrapper, writer);
        if (!req->BodyCB) {
            // setup_multipart out of memory: no body callback installed, so free
            // the writer here and report "no body" (execute_api_v1 frees args).
            delete writer;
            return NULL;
        }
        return writer;
    }
    return NULL;
}

API_DOC(GET, help, none,
    TAG("About")
    SUMMARY("Reserved help endpoint")
    DESCRIPTION("Registered since the REST interface was introduced, but never implemented. It "
                "requires a `command` argument, does nothing with the value, and answers with a "
                "fixed HTML page rather than JSON. The header it sends spells the content type "
                "`text_html`, which is not a media type, so most clients treat the body as plain "
                "text. It is documented here because the firmware serves it, not because there is "
                "anything useful to read from it.")
    PATH("/v1/help", "getHelp", "")
    PARAM("command", "string", "Name of the call to describe. The value is not used.", "", "readmem")
    RESPONSE("200", "text/html", "", "A fixed HTML page.", "")
    RESPONSE("400", "text/html", "", "An HTML page listing the missing argument.", "")
)
API_CALL(GET, help, none, NULL, ARRAY({{"command", P_REQUIRED}}))
{
    if (args.Validate(http_GET_help_none, resp) != 0) {
        resp->html_response(400, "Illegal Arguments", "Please note the following errors:<br>");
        return;
    }

    resp->html_response(200, "This function provides some help!", "Help text.");
}

extern "C" {
int execute_api_v1(HTTPReqMessage *req, HTTPRespMessage *resp)
{
    ArgsURI *args = new ArgsURI();

    const ApiCall_t *func = args->ParseReqHeader(&req->Header);

    if (func) {
        if (func == (ApiCall_t *)-1) {  // Incorrect password
            ResponseWrapper respw(resp);
            respw.error("Forbidden.");
            respw.json_response(HTTP_FORBIDDEN);
            delete args;
        }
        else if (func->body_handler) {
            void *body = func->body_handler(req, resp, func, args);
            if (!body) {
                ResponseWrapper respw(resp);
                respw.error("Expected Body, but got none.");
                respw.json_response(HTTP_PRECONDITION_FAILED);
                delete args;
            } else {
                // body (-handler) successfully attached to request
                // Do not delete args, the body handler will do so after calling the function
            }
        } else {
            // No body required
            ResponseWrapper respw(resp);
            // Check arguments against function prototype
            if (args->Validate(*func, &respw) != 0) {
                respw.json_response(HTTP_BAD_REQUEST);
            } else {
                func->proc(*args, req, &respw, NULL); // NULL = no body
            }
            delete args;
        }
        return 0;
    } else {
        delete args;
        return -1;
    }
}
}

API_DOC(GET, version, none,
    TAG("About")
    SUMMARY("Version of the REST interface")
    DESCRIPTION("Returns the version of the HTTP interface itself. This is not the firmware "
                "version, which `GET /v1/info` reports.")
    PATH("/v1/version", "getVersion", "")
    RESPONSE("200", "application/json", "VersionResponse", "The interface version.", "")
    RESPONSE_EXAMPLE("200", "Version", "{\n  \"version\" : \"0.1\",\n  \"errors\" : []\n}", "")
)
API_CALL(GET, version, none, NULL, ARRAY( { }))
{
    resp->json->add("version", "0.1");
    resp->json_response(HTTP_OK);
}

API_DOC(GET, info, none,
    TAG("About")
    SUMMARY("Product and version information")
    DESCRIPTION("Identifies the device: product name, firmware version, FPGA build number, host "
                "name and unique id. `core_version` is the version of the C64 core and is present "
                "on Ultimate 64 hardware only. `unique_id` is left out when the device has none "
                "configured.\n"
                "\n"
                "The same information is also available without HTTP, from the Ultimate Ident "
                "Service on UDP port 64, which answers a broadcast and so can be used to find a "
                "device whose address is not known.")
    PATH("/v1/info", "getInfo", "")
    RESPONSE("200", "application/json", "InfoResponse", "What the device is and what it is running.", "")
    RESPONSE_EXAMPLE("200", "Ultimate 64 Elite", "{\n  \"product\" : \"Ultimate 64 Elite (V1.49) 3.14d\",\n  \"firmware_version\" : \"3.14d\",\n  \"fpga_version\" : \"122\",\n  \"core_version\" : \"1.49\",\n  \"hostname\" : \"Ultimate-64-Elite-C89085\",\n  \"unique_id\" : \"D09B96\",\n  \"errors\" : []\n}", "")
    RESPONSE_EXAMPLE("200", "Ultimate II+", "{\n  \"product\" : \"1541 Ultimate II+ 3.14d\",\n  \"firmware_version\" : \"3.14d\",\n  \"fpga_version\" : \"11f\",\n  \"hostname\" : \"Ultimate-II-Plus\",\n  \"errors\" : []\n}", "")
)
API_CALL(GET, info, none, NULL, ARRAY( { }))
{
    char fpga_version[8];
    sprintf(fpga_version, "1%02x", getFpgaVersion());
#ifdef U64
    char core_version[8];
    sprintf(core_version, "1.%02x", C64_CORE_VERSION);
#endif
    const char *hostname = networkConfig.cfg->get_string(CFG_NETWORK_HOSTNAME);

    resp->json->add("product", getProductString())
        ->add("firmware_version", APPL_VERSION_ASCII)
        ->add("fpga_version", fpga_version)
#ifdef U64
        ->add("core_version", core_version)
#endif
        ->add("hostname", hostname);

    const char *unique_id = networkConfig.cfg->get_string(CFG_NETWORK_UNIQUE_ID);
    if (unique_id && *unique_id) {
        if (strcmp(unique_id, "Default") == 0) {
            unique_id = getProductUniqueId();
        }
        resp->json->add("unique_id", unique_id);
    }

    resp->json_response(HTTP_OK);
}
