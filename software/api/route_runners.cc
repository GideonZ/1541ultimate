#include "routes.h"
#include "attachment_writer.h"
#include "attachment_reu.h"
#include "pattern.h"
#include "filetype_sid.h"
#include "filetype_prg.h"
#include "filetype_reu.h"
#include "c64_crt.h"

/*
 * SID PLAYER
 */
API_DOC(PUT, runners, sidplay,
    TAG("Runners")
    SUMMARY("Play a SID tune")
    DESCRIPTION("Loads a SID file from the device and starts it. The player takes over the "
                "machine, so whatever was running stops.\n"
                "\n"
                "`songnr` selects a subtune, counting from 1. Leaving it out, or passing 0, plays "
                "the start song the file itself names.")
    PATH("/v1/runners:sidplay", "playSid", "")
    PARAM("file", "string", "Path of the SID file on the device.", "", "/Usb0/music/tune.sid")
    PARAM("songnr", "integer", "Subtune to play, counting from 1. 0 uses the start song of the file.", "0", "2")
    RESPONSE("200", "application/json", "ErrorResponse", "The tune is playing.", "")
    RESPONSE_ERROR("400", "Invalid Song Number Requested", "")
    RESPONSE_ERROR("404", "Cannot open file", "")
    RESPONSE_ERROR("415", "SID File Memory Rollover", "")
)
API_CALL(PUT, runners, sidplay, NULL, ARRAY( { { "file", P_REQUIRED }, { "songnr", P_OPTIONAL } }))
{
    SubsysResultCode_e result = FileTypeSID :: play_file(args["file"], NULL, args.get_int("songnr", 0));
    if (result != SSRET_OK) {
        resp->error(SubsysCommand :: error_string(result));
        resp->json_response(SubsysCommand :: http_response_map(result));
        return;
    }
    resp->json_response(HTTP_OK);
}

API_DOC(POST, runners, sidplay,
    TAG("Runners")
    SUMMARY("Upload and play a SID tune")
    DESCRIPTION("The same as the PUT form, with the tune in the request body. A request that "
                "carries a second file part hands that one to the player as well, which is how a "
                "tune that needs its own player binary is started.")
    PATH("/v1/runners:sidplay", "uploadAndPlaySid", "")
    PARAM("songnr", "integer", "Subtune to play, counting from 1. 0 uses the start song of the file.", "0", "2")
    BODY("multipart/form-data", "FileUpload", "The tune to play, and optionally a second file for the player.")
    BODY("application/octet-stream", "", "The tune to play, sent raw.")
    RESPONSE("200", "application/json", "ErrorResponse", "The tune is playing.", "")
    RESPONSE_ERROR("400", "Upload of file failed.", "")
    RESPONSE_ERROR("400", "Invalid Song Number Requested", "")
)
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
    SubsysResultCode_e result = FileTypeSID :: play_file(fn1, fn2, args.get_int("songnr", 0));
    if (result != SSRET_OK) {
        resp->error(SubsysCommand :: error_string(result));
        resp->json_response(SubsysCommand :: http_response_map(result));
        return;
    }
    resp->json_response(HTTP_OK);
}

/*
 * PRG Loader
 */
API_DOC(PUT, runners, load_prg,
    TAG("Runners")
    SUMMARY("Load a program without starting it")
    DESCRIPTION("DMA loads a PRG file from the device to the address in its first two bytes and "
                "leaves it there. BASIC pointers are fixed up, so a BASIC program can be listed "
                "or started with RUN afterwards. Use `runners:run_prg` to start it as well.")
    PATH("/v1/runners:load_prg", "loadPrg", "")
    PARAM("file", "string", "Path of the PRG file on the device.", "", "/Usb0/games/game.prg")
    RESPONSE("200", "application/json", "ErrorResponse", "The program is in memory.", "")
    RESPONSE_ERROR("404", "Cannot open file", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
API_CALL(PUT, runners, load_prg, NULL, ARRAY( { { "file", P_REQUIRED } }))
{
    SubsysResultCode_t retval = FileTypePRG :: start_prg(args["file"], false);
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_DOC(PUT, runners, run_prg,
    TAG("Runners")
    SUMMARY("Load and start a program")
    DESCRIPTION("DMA loads a PRG file from the device and starts it, the way selecting it in the "
                "menu would. A BASIC program is run and a machine code program is called at its "
                "load address.")
    PATH("/v1/runners:run_prg", "runPrg", "")
    PARAM("file", "string", "Path of the PRG file on the device.", "", "/Usb0/games/game.prg")
    RESPONSE("200", "application/json", "ErrorResponse", "The program was started.", "")
    RESPONSE_ERROR("404", "Cannot open file", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
API_CALL(PUT, runners, run_prg, NULL, ARRAY( { { "file", P_REQUIRED } }))
{
    SubsysResultCode_t retval = FileTypePRG :: start_prg(args["file"], true);
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_DOC(POST, runners, load_prg,
    TAG("Runners")
    SUMMARY("Upload a program without starting it")
    DESCRIPTION("The same as the PUT form, with the program in the request body rather than on "
                "the device.")
    PATH("/v1/runners:load_prg", "uploadPrg", "")
    BODY("multipart/form-data", "FileUpload", "The program to load.")
    BODY("application/octet-stream", "", "The program to load, sent raw.")
    RESPONSE("200", "application/json", "ErrorResponse", "The program is in memory.", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
API_CALL(POST, runners, load_prg, &attachment_writer, ARRAY( { }))
{
    TempfileWriter *handler = (TempfileWriter *)body;
    SubsysResultCode_t retval = FileTypePRG :: start_prg(handler->get_filename(0), false);
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_DOC(POST, runners, run_prg,
    TAG("Runners")
    SUMMARY("Upload and start a program")
    DESCRIPTION("The same as the PUT form, with the program in the request body rather than on "
                "the device.")
    PATH("/v1/runners:run_prg", "uploadAndRunPrg", "")
    BODY("multipart/form-data", "FileUpload", "The program to run.")
    BODY("application/octet-stream", "", "The program to run, sent raw.")
    RESPONSE("200", "application/json", "ErrorResponse", "The program was started.", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
API_CALL(POST, runners, run_prg, &attachment_writer, ARRAY( { }))
{
    TempfileWriter *handler = (TempfileWriter *)body;
    SubsysResultCode_t retval = FileTypePRG :: start_prg(handler->get_filename(0), true);
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

/*
 * CRT Loader
 */
API_DOC(PUT, runners, run_crt,
    TAG("Runners")
    SUMMARY("Start a cartridge")
    DESCRIPTION("Loads a CRT image from the device into the cartridge memory and reboots the "
                "machine into it. The image has to be one of the cartridge types the firmware "
                "implements; an unsupported type is reported rather than started.")
    PATH("/v1/runners:run_crt", "runCrt", "")
    PARAM("file", "string", "Path of the CRT file on the device.", "", "/Usb0/carts/fc3.crt")
    RESPONSE("200", "application/json", "ErrorResponse", "The machine restarted with the cartridge.", "")
    RESPONSE_ERROR("404", "Cannot open file", "")
    RESPONSE_ERROR("415", "Error detected in file format", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
API_CALL(PUT, runners, run_crt, NULL, ARRAY( { { "file", P_REQUIRED } }))
{
    cart_def def;
    SubsysResultCode_t result = { C64_CRT :: load_crt("", args["file"], &def, C64 :: get_cartridge_rom_addr()) }; // eek
    if (result.status == SSRET_OK) {
        SubsysCommand *c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_START_CART, (int)&def, "", "");
        result = c64_command->execute();
    }
    resp->error(SubsysCommand::error_string(result.status));
    resp->json_response(SubsysCommand::http_response_map(result.status));
}

API_DOC(POST, runners, run_crt,
    TAG("Runners")
    SUMMARY("Upload and start a cartridge")
    DESCRIPTION("The same as the PUT form, with the CRT image in the request body rather than on "
                "the device.")
    PATH("/v1/runners:run_crt", "uploadAndRunCrt", "")
    BODY("multipart/form-data", "FileUpload", "The cartridge image to start.")
    BODY("application/octet-stream", "", "The cartridge image to start, sent raw.")
    RESPONSE("200", "application/json", "ErrorResponse", "The machine restarted with the cartridge.", "")
    RESPONSE_ERROR("415", "Error detected in file format", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
    RESPONSE_ERROR("503", "SubSystem does not exist", "")
)
API_CALL(POST, runners, run_crt, &attachment_writer, ARRAY( { }))
{
    TempfileWriter *handler = (TempfileWriter *)body;
    cart_def def;
    SubsysResultCode_t retval = { C64_CRT :: load_crt("", handler->get_filename(0), &def, C64 :: get_cartridge_rom_addr()) }; // eek
    if (retval.status == SSRET_OK) {
        SubsysCommand *c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_START_CART, (int)&def, "", "");
        retval = c64_command->execute();
    }
    resp->error(SubsysCommand::error_string(retval.status));
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

/*
 * MOD File Player
 */
API_DOC(PUT, runners, modplay,
    TAG("Runners")
    SUMMARY("Play an Amiga MOD file")
    DESCRIPTION("Loads a MOD file from the device into the REU memory and starts the module "
                "player, which plays it through the sampler rather than through the SID.\n"
                "\n"
                "The sampler is an optional part of the FPGA build. Where it is absent the call "
                "answers 501. Loading the module overwrites whatever the REU held.")
    PATH("/v1/runners:modplay", "playMod", "")
    PARAM("file", "string", "Path of the MOD file on the device.", "", "/Usb0/music/song.mod")
    RESPONSE("200", "application/json", "ErrorResponse", "The module is playing.", "")
    RESPONSE_ERROR("404", "FILE DOESN'T EXIST", "")
    RESPONSE_ERROR("501", "Sampler module not available", "")
)
API_CALL(PUT, runners, modplay, NULL, ARRAY( { { "file", P_REQUIRED } }))
{
    if (!(getFpgaCapabilities() & CAPAB_SAMPLER)) {
        resp->error("Sampler module not available");
        resp->json_response(HTTP_NOT_IMPLEMENTED);
        return;
    }
    uint32_t trans;
    FRESULT fres = FileManager :: getFileManager() -> load_file("", args["file"], (uint8_t *)REU_MEMORY_BASE, REU_MAX_SIZE, &trans);
    if (fres != FR_OK) {
        resp->error(FileSystem::get_error_string(fres));
        resp->json_response(HTTP_NOT_FOUND);
        return;
    }
    FileTypeREU :: start_modplayer();
    resp->json_response(HTTP_OK);
}

API_DOC(POST, runners, modplay,
    TAG("Runners")
    SUMMARY("Upload and play an Amiga MOD file")
    DESCRIPTION("The same as the PUT form, with the module in the request body. The upload is "
                "streamed straight into the REU memory rather than through a temporary file, so a "
                "module larger than the file system would hold can still be played.")
    PATH("/v1/runners:modplay", "uploadAndPlayMod", "")
    BODY("multipart/form-data", "FileUpload", "The module to play.")
    BODY("application/octet-stream", "", "The module to play, sent raw.")
    RESPONSE("200", "application/json", "ErrorResponse", "The module is playing.", "")
    RESPONSE_ERROR("501", "Sampler module not available", "")
)
API_CALL(POST, runners, modplay, &attachment_reu, ARRAY( { }))
{
    if (!(getFpgaCapabilities() & CAPAB_SAMPLER)) {
        resp->error("Sampler module not available");
        resp->json_response(HTTP_NOT_IMPLEMENTED);
        return;
    }
    FileTypeREU :: start_modplayer();
    resp->json_response(HTTP_OK);
}
