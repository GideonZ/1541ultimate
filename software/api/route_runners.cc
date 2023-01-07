#include "routes.h"
#include "attachment_writer.h"
#include "pattern.h"
#include "filetype_sid.h"
#include "filetype_prg.h"
/*
 * SID PLAYER
 */
API_CALL(PUT, runners, sidplay, NULL, ARRAY( { { "file", P_REQUIRED }, { "songnr", P_OPTIONAL } }))
{
    int result = FileTypeSID :: play_file(args["file"], NULL, args.get_int("songnr", 0));
    resp->json->add("result", result);
    resp->json_response(HTTP_OK);
}

API_CALL(POST, runners, sidplay, &attachment_writer, ARRAY( { { "songnr", P_OPTIONAL } }))
{
    TempfileWriter *handler = (TempfileWriter *)body;
    const char *fn1 = handler->get_filename(0);
    const char *fn2 = handler->get_filename(1);
    if (!fn1) {
        resp->error("Upload of file failed.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    int result = FileTypeSID :: play_file(fn1, fn2, args.get_int("songnr", 0));
    resp->json->add("result", result);
    resp->json_response(HTTP_OK);
}

/*
 * PRG Loader
 */
API_CALL(PUT, runners, load_prg, NULL, ARRAY( { { "file", P_REQUIRED } }))
{
    SubsysResultCode_t retval = FileTypePRG :: start_prg(args["file"], false);
    if (retval != SSRET_OK) {
        resp->error(SubsysCommand::error_string(retval));
    }
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(PUT, runners, run_prg, NULL, ARRAY( { { "file", P_REQUIRED } }))
{
    SubsysResultCode_t retval = FileTypePRG :: start_prg(args["file"], true);
    if (retval != SSRET_OK) {
        resp->error(SubsysCommand::error_string(retval));
    }
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(POST, runners, load_prg, &attachment_writer, ARRAY( { }))
{
    TempfileWriter *handler = (TempfileWriter *)body;
    SubsysResultCode_t retval = FileTypePRG :: start_prg(handler->get_filename(0), false);
    if (retval != SSRET_OK) {
        resp->error(SubsysCommand::error_string(retval));
    }
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(POST, runners, run_prg, &attachment_writer, ARRAY( { }))
{
    TempfileWriter *handler = (TempfileWriter *)body;
    SubsysResultCode_t retval = FileTypePRG :: start_prg(handler->get_filename(0), true);
    if (retval != SSRET_OK) {
        resp->error(SubsysCommand::error_string(retval));
    }
    resp->json_response(SubsysCommand::http_response_map(retval));
}
