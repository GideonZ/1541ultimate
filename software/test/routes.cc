#define ENTER_SAFE_SECTION
#define LEAVE_SAFE_SECTION
#include "routes.h"
#include "stream_stdout.h"
extern "C" {
    #include "url.h"
}

Dict<const char *, IndexedList<const ApiCall_t *> *> *getRoutesList(void)
{
    static Dict<const char *, IndexedList<const ApiCall_t *> *> HttpRoutes(10, 0, 0, strcmp);
    return &HttpRoutes;
}

API_CALL(help, empty, "This function is supposed to help you.", ARRAY({{"command", P_REQUIRED}, P_END}))
{
    mprintf("Hah!\n");
    return 0;
}

API_CALL(files, createDiskImage, "Create a disk image", ARRAY({{"type", P_REQUIRED}, {"format", P_OPTIONAL}, P_END }))
{
    if (args.Validate(http_files_createDiskImage) != 0) {
        return -1;
    }
    mprintf("Okay. Should create a disk image of type %s. Path = %s.\n", args["type"], args.get_path());
    return 0;
}

/*
int main()
{
    Stream_StdOut out;
    ArgsURI args;

    HTTPReqHeader hdr;
    hdr.Method = HTTP_GET;
    hdr.URI = "/v1/files/some/path/to/disk.d64:createDiskImage?type=d64&format=json";
    hdr.Version = "1.1";

    const ApiCall_t *func = args.ParseReqHeader(&hdr);
    if (func) {
        func->proc(out, args);
    } else {
        printf("Unknown route or command!\n");
    }
}
*/

/*
    const ApiCall_t *call = ArgsURI::find_api_call("help", "empty");
    if (call) {
        call->proc(out, args);
    } else {
        printf("Unknown!\n");
    }



    return 0;
}
*/

// This is to make smallprintf work on PC.
void outbyte(int byte) { putc(byte, stdout); }
