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
API_CALL(PUT, runners, sidplay, NULL, ARRAY( { { "file", P_REQUIRED }, { "songnr", P_OPTIONAL } }))
{
    int result = FileTypeSID :: play_file(args["file"], NULL, args.get_int("songnr", 0));
    if (result) {
        resp->error(FileTypeSID :: get_error(result));
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
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
    if (result) {
        resp->error(FileTypeSID :: get_error(result));
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    resp->json_response(HTTP_OK);
}

/*
 * PRG Loader
 */
API_CALL(PUT, runners, load_prg, NULL, ARRAY( { { "file", P_REQUIRED } }))
{
    SubsysResultCode_t retval = FileTypePRG :: start_prg(args["file"], false);
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(PUT, runners, run_prg, NULL, ARRAY( { { "file", P_REQUIRED } }))
{
    SubsysResultCode_t retval = FileTypePRG :: start_prg(args["file"], true);
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(POST, runners, load_prg, &attachment_writer, ARRAY( { }))
{
    TempfileWriter *handler = (TempfileWriter *)body;
    SubsysResultCode_t retval = FileTypePRG :: start_prg(handler->get_filename(0), false);
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(POST, runners, run_prg, &attachment_writer, ARRAY( { }))
{
    TempfileWriter *handler = (TempfileWriter *)body;
    SubsysResultCode_t retval = FileTypePRG :: start_prg(handler->get_filename(0), true);
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
}

/*
 * CRT Loader
 */
API_CALL(PUT, runners, run_crt, NULL, ARRAY( { { "file", P_REQUIRED } }))
{
    cart_def def;
    SubsysResultCode_t retval = C64_CRT :: load_crt("", args["file"], &def, C64 :: get_cartridge_rom_addr());
    if (retval == SSRET_OK) {
        SubsysCommand *c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_START_CART, (int)&def, "", "");
        retval = c64_command->execute();
    }
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(POST, runners, run_crt, &attachment_writer, ARRAY( { }))
{
    TempfileWriter *handler = (TempfileWriter *)body;
    cart_def def;
    SubsysResultCode_t retval = C64_CRT :: load_crt("", handler->get_filename(0), &def, C64 :: get_cartridge_rom_addr());
    if (retval == SSRET_OK) {
        SubsysCommand *c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_START_CART, (int)&def, "", "");
        retval = c64_command->execute();
    }
    resp->error(SubsysCommand::error_string(retval));
    resp->json_response(SubsysCommand::http_response_map(retval));
}

/*
 * MOD File Player
 */
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
