#include "routes.h"
#include "filemanager.h"
#include "json.h"
#include "c1541.h"
#include "iec.h"
#include "iec_channel.h"
#include "attachment_writer.h"
#include "route_drives.h"

extern C1541 *c1541_A;
extern C1541 *c1541_B;
//extern IecInterface *iec_if;

DriveToSubsys driveToSubsys;
ImageTypeToInt imageTypeToInt;
ImageTypeToCommand imageTypeToCommand;
ModeToInt modeToInt;
DriveTypeToInt driveTypeToInt;

static void drive_info(JSON_List *obj, C1541 *drive, const char *letter)
{
    mstring path, name;
    drive->get_last_mounted_file(path, name);
    obj->add(JSON::Obj()->add(letter, JSON::Obj()
        ->add("enabled", drive->get_drive_power())
        ->add("bus_id", drive->get_current_iec_address())
        ->add("type", drive->get_drive_type_string())
        ->add("rom", drive->get_drive_rom_file())
        ->add("image_file", name.c_str())
        ->add("image_path", path.c_str())));
}

void iec_info(JSON_List *obj) __attribute__((weak));
void iec_info(JSON_List *obj)
{
}

// List all the available drives
API_CALL(GET, drives, none, NULL, ARRAY({ }))
{
    JSON_List *drives = JSON::List();
    resp->json->add("drives", drives);
    if (c1541_A) drive_info(drives, c1541_A, "a");
    if (c1541_B) drive_info(drives, c1541_B, "b");
    if (iec_if) iec_info(drives);
    resp->json_response(HTTP_OK);
}

void api_mount(ResponseWrapper *resp, const char *fn, const char *drive, const char *type, const char *mode)
{
    int subsys_id = driveToSubsys[drive];
    if (subsys_id < 0) {
        resp->error("Invalid Drive '%s'", drive);
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    int ftype = imageTypeToInt[type];
    int command = imageTypeToCommand[type] | modeToInt[mode];

    if (command < 0) {
        resp->error("Invalid Type '%s'", type);
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    resp->json->add("Subsys", subsys_id)->add("Ftype", ftype)->add("command", command)->add("file", fn);
    SubsysCommand *cmd = new SubsysCommand(NULL, subsys_id, command, ftype, "", fn);
    SubsysResultCode_t retval = cmd->execute();
    if (retval != SSRET_OK) {
        resp->error(SubsysCommand::error_string(retval));
    }
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(PUT, drives, mount, NULL, ARRAY({{ "image", P_REQUIRED }, { "type", P_OPTIONAL }, { "mode", P_OPTIONAL } }))
{
    printf("Mount disk from path '%s' on drive '%s'\n", args["image"], args.get_path(0));
    char ext[4];
    get_extension(args["image"], ext);
    api_mount(resp, args["image"], args.get_path(0), args.get_or("type", ext), args["mode"]);
}

API_CALL(POST, drives, mount, &attachment_writer, ARRAY({ { "type", P_OPTIONAL }, { "mode", P_OPTIONAL } }))
{
    TempfileWriter *handler = (TempfileWriter *)body;
    const char *fn = handler->get_filename(0);
    if (!fn) {
        resp->error("Upload of file failed.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    printf("Mount disk from upload: '%s'\n", fn);
    char ext[4];
    get_extension(fn, ext);
    api_mount(resp, fn, args.get_path(0), args.get_or("type", ext), args["mode"]);

    //auto lamb = [] () { printf("Hello!\n"); };
    //lamb();
}

//#define MENU_1541_SAVED64   0x1503
//#define MENU_1541_SAVEG64   0x1504
//#define MENU_1541_BLANK     0x1505
//#define MENU_1541_SWAP      0x1514

static void simple_drive_command(ArgsURI& args, ResponseWrapper *resp, int command)
{
    const char *drive = args.get_path(0);
    int subsys_id = driveToSubsys[drive];
    if (subsys_id < 0) {
        resp->error("Invalid Drive '%s'", drive);
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    SubsysCommand *cmd = new SubsysCommand(NULL, subsys_id, command, 0, "", args.get_or("file", ""));
    SubsysResultCode_t retval = cmd->execute();
    if (retval != SSRET_OK) {
        resp->error(SubsysCommand::error_string(retval));
    }
    resp->json_response(SubsysCommand::http_response_map(retval));
}

API_CALL(PUT, drives, reset, NULL, ARRAY({ }))
{
    return simple_drive_command(args, resp, MENU_1541_RESET);
}

API_CALL(PUT, drives, remove, NULL, ARRAY({ }))
{
    return simple_drive_command(args, resp, MENU_1541_REMOVE);
}

API_CALL(PUT, drives, on, NULL, ARRAY({ }))
{
    return simple_drive_command(args, resp, MENU_1541_TURNON);
}

API_CALL(PUT, drives, off, NULL, ARRAY({ }))
{
    return simple_drive_command(args, resp, MENU_1541_TURNOFF);
}

API_CALL(PUT, drives, unlink, NULL, ARRAY({ }))
{
    return simple_drive_command(args, resp, MENU_1541_UNLINK);
}

API_CALL(PUT, drives, load_rom, NULL, ARRAY({{ "file", P_REQUIRED }}))
{
    return simple_drive_command(args, resp, FLOPPY_LOAD_DOS);
}

API_CALL(POST, drives, load_rom, &attachment_writer, ARRAY({ }))
{
    TempfileWriter *handler = (TempfileWriter *)body;
    const char *fn = handler->get_filename(0);
    if (!fn) {
        resp->error("Upload of file failed.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    args.set("file", fn);
    return simple_drive_command(args, resp, FLOPPY_LOAD_DOS);
}

API_CALL(PUT, drives, set_mode, NULL, ARRAY({{ "mode", P_REQUIRED }}))
{
    const char *drive = args.get_path(0);
    int subsys_id = driveToSubsys[drive];
    if (subsys_id < 0) {
        resp->error("Invalid Drive '%s'", drive);
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    int mode = driveTypeToInt[args["mode"]];
    if (mode < 0) {
        resp->error("Invalid Drive Type '%s'", args["mode"]);
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    resp->json->add("mode", args["mode"]);
    SubsysCommand *cmd = new SubsysCommand(NULL, subsys_id, MENU_1541_SET_MODE, mode, "", "");
    SubsysResultCode_t retval = cmd->execute();
    if (retval != SSRET_OK) {
        resp->error(SubsysCommand::error_string(retval));
    }
    resp->json_response(SubsysCommand::http_response_map(retval));
}
