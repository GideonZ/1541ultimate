#include "routes.h"
#include "attachment_writer.h"
#include "pattern.h"
#include "filetype_sid.h"

// DefaultMethod defaultMethod;

void api_run(ArgsURI &args, ResponseWrapper *resp, const char *filename1, const char *filename2)
{
    // const char *method = args.get_path();
    // if (strlen(method) == 0) {
    //     method = defaultMethod[get_extension(filename)];
    //     if (!method) {
    //         method = "(unspecified)";
    //     }
    // }
    // resp->json->add("method", method);

    int result = FileTypeSID :: play_file(filename1, filename2, 0);
    resp->json->add("result", result);
    resp->json_response(HTTP_OK);
}

API_CALL(PUT, runners, none, NULL, ARRAY( { { "file", P_REQUIRED } }))
{
    api_run(args, resp, args["file"], NULL);
}

API_CALL(POST, runners, none, &attachment_writer, ARRAY( {  }))
{
    TempfileWriter *handler = (TempfileWriter *)body;
    const char *fn1 = handler->get_filename(0);
    const char *fn2 = handler->get_filename(1);
    if (!fn1) {
        resp->error("Upload of file failed.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    api_run(args, resp, fn1, fn2);
}

