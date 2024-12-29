#include "routes.h"
#include "attachment_writer.h"
#include "attachment_reu.h"
#include "stream_uart.h"
#include "dump_hex.h"
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
        ResponseWrapper respw(writer->get_response());
        if (args->Validate(*func, &respw) != 0) {
            respw.json_response(HTTP_BAD_REQUEST);
        } else {
            func->proc(*args, writer->get_request(), &respw, writer);
        }
        delete args;
    }
}

TempfileWriter *attachment_writer(HTTPReqMessage *req, HTTPRespMessage *resp, const ApiCall_t *func, ArgsURI *args)
{
    if (req->bodyType != eNoBody) {
        TempfileWriter *writer = new TempfileWriter(req, resp, writer_complete, func, args);
        setup_multipart(req, &TempfileWriter::collect_wrapper, writer);
        return writer;
    }
    return NULL;
}
int TempfileWriter :: temp_count = 0;

/* REU Writer */
REUWriter *attachment_reu(HTTPReqMessage *req, HTTPRespMessage *resp, const ApiCall_t *func, ArgsURI *args)
{
    if (req->bodyType != eNoBody) {
        REUWriter *writer = new REUWriter();
        writer->create_callback(req, resp, args, (const ApiCall_t *)func);
        setup_multipart(req, &REUWriter::collect_wrapper, writer);
        return writer;
    }
    return NULL;
}

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

API_CALL(GET, version, none, NULL, ARRAY( { }))
{
    resp->json->add("version", "0.1");
    resp->json_response(HTTP_OK);
}
