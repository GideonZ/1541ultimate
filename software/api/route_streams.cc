#include "routes.h"
#include "subsys.h"
#include "http_codes.h"
#include "data_streamer.h"

class StreamNameToInt : public Dict<const char *, int>
{
public:
    StreamNameToInt() : Dict(3, NULL, -1, &strcasecmp) {
        set("video", 0);
        set("audio", 1);
        set("debug", 2);
    }
};
static StreamNameToInt streamDict;

API_CALL(PUT, streams, start, NULL, ARRAY ( { { "ip", P_REQUIRED } }))
{
    const char *streamName = args.get_path(0);
    SubsysCommand *sys_command;

    if (!streamName) {
        resp->error("No stream name given in path");
        resp->json_response(HTTP_NOT_FOUND);
        return;
    }
    int streamIndex = streamDict[streamName];
    if (streamIndex < 0) {
        resp->error("Unrecognized stream name '%s'", streamName);
        resp->json_response(HTTP_NOT_FOUND);
        return;
    }

    if (streamIndex == 0) { // video streams require debug to be off
        sys_command = new SubsysCommand(NULL, -1, (int)dataStreamer, 2, "", "");
        sys_command->direct_call = DataStreamer :: S_stopStream;
        sys_command->execute();
    }

    sys_command = new SubsysCommand(NULL, -1, (int)dataStreamer, streamIndex, args["ip"], "");
    sys_command->direct_call = DataStreamer :: S_startStream;
    SubsysResultCode_t retval = sys_command->execute();
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_CALL(PUT, streams, stop, NULL, ARRAY ( { } ))
{
    SubsysCommand *sys_command;

    const char *streamName = args.get_path(0);
    if (!streamName) {
        resp->error("No stream name given in path");
        resp->json_response(HTTP_NOT_FOUND);
        return;
    }
    int streamIndex = streamDict[streamName];
    if (streamIndex < 0) {
        resp->error("Unrecognized stream name '%s'", streamName);
        resp->json_response(HTTP_NOT_FOUND);
        return;
    }

    sys_command = new SubsysCommand(NULL, -1, (int)dataStreamer, streamIndex, "", "");
    sys_command->direct_call = DataStreamer :: S_stopStream;
    sys_command->execute();
    resp->json_response(HTTP_OK);
}
