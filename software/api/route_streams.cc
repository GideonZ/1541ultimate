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

API_DOC(PUT, streams, start,
    TAG("Streams")
    SUMMARY("Start a data stream")
    DESCRIPTION("Sends the named stream to a listening host over UDP. `video` carries the VIC "
                "output, `audio` the sound, and `debug` a trace of the cartridge bus.\n"
                "\n"
                "`ip` may be an address or a host name, and may carry a port as `address:port`. "
                "Without a port the stream goes to 11000 for video, 11001 for audio and 11002 for "
                "debug. A multicast group is accepted in place of a single host, in which case "
                "every listener that joined the group receives the stream.\n"
                "\n"
                "Video and debug share hardware and cannot both run, so starting video stops "
                "debug first.")
    PATH("/v1/streams/{stream}:start", "startStream", "")
    PATH_PARAM("stream", "string", "Which stream to act on.", "video")
    PATH_PARAM_ENUM("stream", "video,audio,debug")
    PARAM("ip", "string", "Where to send the stream. An address, optionally followed by a port.", "", "192.168.1.10:11000")
    RESPONSE("200", "application/json", "ErrorResponse", "The stream is running.", "")
    RESPONSE_ERROR("404", "Unrecognized stream name 'screen'", "")
    RESPONSE_ERROR("500", "No Operational Network Interface", "")
)
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

API_DOC(PUT, streams, stop,
    TAG("Streams")
    SUMMARY("Stop a data stream")
    DESCRIPTION("Stops the named stream. Stopping a stream that is not running is not an error, "
                "so this is safe to call unconditionally when a client shuts down.")
    PATH("/v1/streams/{stream}:stop", "stopStream", "")
    PATH_PARAM("stream", "string", "Which stream to act on.", "video")
    PATH_PARAM_ENUM("stream", "video,audio,debug")
    RESPONSE("200", "application/json", "ErrorResponse", "The stream is stopped.", "")
    RESPONSE_ERROR("404", "Unrecognized stream name 'screen'", "")
)
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
