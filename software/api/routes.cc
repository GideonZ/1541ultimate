#include "routes.h"
#include "attachment_writer.h"
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
TempfileWriter *attachment_writer(HTTPReqMessage *req, HTTPRespMessage *resp, const ApiCall_t *func, ArgsURI *args)
{
    if (req->BodySize) {
        TempfileWriter *writer = new TempfileWriter();
        writer->create_callback(req, resp, args, (const ApiCall_t *)func);
        setup_multipart(req, &TempfileWriter::collect_wrapper, writer);
        return writer;
    }
    return NULL;
}
int TempfileWriter :: temp_count = 0;

API_CALL(GET, help, none, NULL, ARRAY({{"command", P_REQUIRED}}))
{
    if (args.Validate(http_GET_help_none, resp) != 0) {
        resp->html_response(400, "Illegal Arguments", "Please note the following errors:<br>");
        return;
    }

    resp->html_response(200, "This function provides some help!", "Help text.");
}

/*
API_CALL(PUT, files, createDiskImage, NULL, ARRAY({{"type", P_REQUIRED}, {"format", P_OPTIONAL}}))
{
    if (args.Validate(http_files_createDiskImage) != 0) {
        build_response(resp, 400, "During parsing, the following errors occurred:<br><br>%s", args.get_errortext());
        return;
    }
    printf("DiskIm");
    build_response(resp, 200, "Create Disk Image of type %s into file '%s'<br>", args["type"], args.get_path());
}

API_CALL(PUT, drives, mount, &attachment_writer, ARRAY({P_END}))
{
    if (args.Validate(http_PUT, drives_mount) != 0) {
        build_response(resp, 400, "During parsing, the following errors occurred:<br><br>%s", args.get_errortext());
        return;
    }
    if (!body) {
        printf("Mount disk from path '%s'\n", args.get_path());
        build_response(resp, 200, "Mount disk from path '%s'<br>", args.get_path());
    } else {
        TempfileWriter *handler = (TempfileWriter *)body;
        printf("Mount disk from upload: '%s'\n", handler->get_filename(0));
        build_response(resp, 200, "Mount disk from upload. Filename = '%s'<br>", handler->get_filename(0));
    }
}
*/

extern "C" {
int execute_api_v1(HTTPReqMessage *req, HTTPRespMessage *resp)
{
    ArgsURI *args = new ArgsURI();

    const ApiCall_t *func = args->ParseReqHeader(&req->Header);

    if (func) {
        if (func->body_handler) {
            void *body = func->body_handler(req, resp, func, args);
            if (!body) {
                ResponseWrapper respw(resp);
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
