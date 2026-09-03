#include "routes.h"
#include "filemanager.h"
#include "json.h"
#include "c1541.h"
#include "iec_interface.h"
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

// List all the available drives
API_DOC(GET, drives, none,
    TAG("Drives")
    SUMMARY("List the drives")
    DESCRIPTION("Reports every drive this device has: the emulated drives `a` and `b`, and "
                "`softiec`, the IEC file system that serves files straight from storage. For each "
                "one it gives whether it is powered, its bus id, the model it is emulating, the "
                "ROM it is running and the image it has mounted. Which drives are present depends "
                "on the product and on the configuration, so this is the call to make before "
                "assuming a drive exists.")
    PATH("/v1/drives", "listDrives", "")
    RESPONSE("200", "application/json", "DrivesResponse", "The drives and what is in them.", "")
    RESPONSE_EXAMPLE("200", "One drive with a disk", "{\n  \"drives\" : [\n    { \"a\" : {\n        \"enabled\" : true,\n        \"bus_id\" : 8,\n        \"type\" : \"1541\",\n        \"rom\" : \"1541.rom\",\n        \"image_file\" : \"disk.d64\",\n        \"image_path\" : \"/Usb0/games\"\n    } }\n  ],\n  \"errors\" : []\n}", "")
)
API_CALL(GET, drives, none, NULL, ARRAY({ }))
{
    JSON_List *drives = JSON::List();
    resp->json->add("drives", drives);
    if (c1541_A) drive_info(drives, c1541_A, "a");
    if (c1541_B) drive_info(drives, c1541_B, "b");
    IecInterface :: info(drives);
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
    if (retval.status != SSRET_OK) {
        resp->error(SubsysCommand::error_string(retval.status));
    }
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_DOC(PUT, drives, mount,
    TAG("Drives")
    SUMMARY("Mount a disk image")
    CAUTION("destructive", "Replaces whatever was in the drive, with the same loss a remove would cause.")
    DESCRIPTION("Mounts an image that is already on the device.\n"
                "\n"
                "`type` decides how the image is read and defaults to the extension of the file "
                "name, so it only has to be given when the extension is missing or wrong. `mode` "
                "decides what happens to writes: `readwrite` puts them in the file, `readonly` "
                "write protects the disk so the drive refuses them, and `unlinked` accepts them "
                "in memory but never writes them back.")
    PATH("/v1/drives/{drive}:mount", "mountImage", "")
    PATH_PARAM("drive", "string", "Which drive to act on. `a` and `b` are the emulated drives, `softiec` is the IEC file system.", "a")
    PATH_PARAM_ENUM("drive", "a,b,softiec")
    PARAM("image", "string", "Path of the image on the device.", "", "/Usb0/games/disk.d64")
    PARAM("type", "string", "How to read the image. Defaults to the file extension.", "", "d64")
    PARAM_ENUM("type", "d64,g64,d71,g71,d81")
    PARAM("mode", "string", "What happens to writes.", "readwrite", "readonly")
    PARAM_ENUM("mode", "readwrite,readonly,unlinked")
    RESPONSE("200", "application/json", "DriveMountResponse", "The image was mounted.", "")
    RESPONSE_ERROR("400", "Invalid Type 'xyz'", "")
    RESPONSE_ERROR("404", "Cannot open file", "")
    RESPONSE_ERROR("415", "Error detected in file format", "")
    RESPONSE_ERROR("400", "Invalid Drive 'c'", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
)
API_CALL(PUT, drives, mount, NULL, ARRAY({{ "image", P_REQUIRED }, { "type", P_OPTIONAL }, { "mode", P_OPTIONAL } }))
{
    printf("Mount disk from path '%s' on drive '%s'\n", args["image"], args.get_path(0));
    char ext[4];
    get_extension(args["image"], ext);
    api_mount(resp, args["image"], args.get_path(0), args.get_or("type", ext), args["mode"]);
}

API_DOC(POST, drives, mount,
    TAG("Drives")
    SUMMARY("Upload and mount a disk image")
    CAUTION("destructive", "Replaces whatever was in the drive, with the same loss a remove would cause.")
    DESCRIPTION("The same as the PUT form, with the image in the request body. The upload is "
                "written to a temporary file on the device first, and the name it is given comes "
                "from the multipart part or from the `Content-Disposition` header. That name is "
                "also where the default `type` comes from, so an upload whose name has no useful "
                "extension needs `type` to be given.")
    PATH("/v1/drives/{drive}:mount", "uploadAndMountImage", "")
    PATH_PARAM("drive", "string", "Which drive to act on. `a` and `b` are the emulated drives, `softiec` is the IEC file system.", "a")
    PATH_PARAM_ENUM("drive", "a,b,softiec")
    PARAM("type", "string", "How to read the image. Defaults to the extension of the uploaded name.", "", "d64")
    PARAM_ENUM("type", "d64,g64,d71,g71,d81")
    PARAM("mode", "string", "What happens to writes.", "readwrite", "unlinked")
    PARAM_ENUM("mode", "readwrite,readonly,unlinked")
    BODY("multipart/form-data", "FileUpload", "The image to mount.")
    BODY("application/octet-stream", "", "The image to mount, sent raw.")
    RESPONSE("200", "application/json", "DriveMountResponse", "The image was mounted.", "")
    RESPONSE_ERROR("400", "Upload of file failed.", "")
    RESPONSE_ERROR("400", "Invalid Drive 'c'", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
)
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
    if (retval.status != SSRET_OK) {
        resp->error(SubsysCommand::error_string(retval.status));
    }
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}

API_DOC(PUT, drives, reset,
    TAG("Drives")
    SUMMARY("Reset the drive")
    DESCRIPTION("Resets the drive processor, as turning the drive off and on again would. "
                "Whatever is mounted stays mounted.")
    PATH("/v1/drives/{drive}:reset", "resetDrive", "")
    PATH_PARAM("drive", "string", "Which drive to act on. `a` and `b` are the emulated drives, `softiec` is the IEC file system.", "a")
    PATH_PARAM_ENUM("drive", "a,b,softiec")
    RESPONSE("200", "application/json", "ErrorResponse", "The drive was reset.", "")
    RESPONSE_ERROR("400", "Invalid Drive 'c'", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
)
API_CALL(PUT, drives, reset, NULL, ARRAY({ }))
{
    return simple_drive_command(args, resp, MENU_1541_RESET);
}

API_DOC(PUT, drives, remove,
    TAG("Drives")
    SUMMARY("Remove the mounted disk")
    CAUTION("destructive", "Changes made in unlinked mode were never written to the file and are lost with the disk.")
    DESCRIPTION("Ejects the disk. Changes that were made in `unlinked` mode are lost, because "
                "they were never written to the file.")
    PATH("/v1/drives/{drive}:remove", "removeImage", "")
    PATH_PARAM("drive", "string", "Which drive to act on. `a` and `b` are the emulated drives, `softiec` is the IEC file system.", "a")
    PATH_PARAM_ENUM("drive", "a,b,softiec")
    RESPONSE("200", "application/json", "ErrorResponse", "The disk was removed.", "")
    RESPONSE_ERROR("403", "Disk has been modified, save first", "")
    RESPONSE_ERROR("400", "Invalid Drive 'c'", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
)
API_CALL(PUT, drives, remove, NULL, ARRAY({ }))
{
    return simple_drive_command(args, resp, MENU_1541_REMOVE);
}

API_DOC(PUT, drives, on,
    TAG("Drives")
    SUMMARY("Turn the drive on")
    DESCRIPTION("Powers the drive up so that it answers on the serial bus. A drive that was "
                "already on is reset instead.")
    PATH("/v1/drives/{drive}:on", "turnDriveOn", "")
    PATH_PARAM("drive", "string", "Which drive to act on. `a` and `b` are the emulated drives, `softiec` is the IEC file system.", "a")
    PATH_PARAM_ENUM("drive", "a,b,softiec")
    RESPONSE("200", "application/json", "ErrorResponse", "The drive is on.", "")
    RESPONSE_ERROR("400", "Invalid Drive 'c'", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
)
API_CALL(PUT, drives, on, NULL, ARRAY({ }))
{
    return simple_drive_command(args, resp, MENU_1541_TURNON);
}

API_DOC(PUT, drives, off,
    TAG("Drives")
    SUMMARY("Turn the drive off")
    DESCRIPTION("Powers the drive down. It stops answering on the serial bus, which is how a "
                "device number is freed for a real drive on the same bus.")
    PATH("/v1/drives/{drive}:off", "turnDriveOff", "")
    PATH_PARAM("drive", "string", "Which drive to act on. `a` and `b` are the emulated drives, `softiec` is the IEC file system.", "a")
    PATH_PARAM_ENUM("drive", "a,b,softiec")
    RESPONSE("200", "application/json", "ErrorResponse", "The drive is off.", "")
    RESPONSE_ERROR("400", "Invalid Drive 'c'", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
)
API_CALL(PUT, drives, off, NULL, ARRAY({ }))
{
    return simple_drive_command(args, resp, MENU_1541_TURNOFF);
}

API_DOC(PUT, drives, unlink,
    TAG("Drives")
    SUMMARY("Unlink the mounted disk from its file")
    CAUTION("destructive", "Everything written from now on is discarded when the disk is removed.")
    DESCRIPTION("Leaves the disk in the drive but breaks the connection to the file it came from. "
                "The drive keeps accepting writes and they stay in memory, but nothing more is "
                "written back to the image file. This is `mode=unlinked` applied after the fact, "
                "and it is the way to try something destructive on a disk without risking the "
                "file.")
    PATH("/v1/drives/{drive}:unlink", "unlinkImage", "")
    PATH_PARAM("drive", "string", "Which drive to act on. `a` and `b` are the emulated drives, `softiec` is the IEC file system.", "a")
    PATH_PARAM_ENUM("drive", "a,b,softiec")
    RESPONSE("200", "application/json", "ErrorResponse", "The disk is no longer linked to its file.", "")
    RESPONSE_ERROR("400", "Invalid Drive 'c'", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
)
API_CALL(PUT, drives, unlink, NULL, ARRAY({ }))
{
    return simple_drive_command(args, resp, MENU_1541_UNLINK);
}

API_DOC(PUT, drives, load_rom,
    TAG("Drives")
    SUMMARY("Load a drive ROM")
    DESCRIPTION("Replaces the ROM the drive runs with an image from the device and resets the "
                "drive into it. 16K and 32K images are accepted; a 32K image is what a drive with "
                "a patched or extended DOS needs.")
    PATH("/v1/drives/{drive}:load_rom", "loadDriveRom", "")
    PATH_PARAM("drive", "string", "Which drive to act on. `a` and `b` are the emulated drives, `softiec` is the IEC file system.", "a")
    PATH_PARAM_ENUM("drive", "a,b,softiec")
    PARAM("file", "string", "Path of the ROM image on the device.", "", "/Usb0/roms/1541-ii.rom")
    RESPONSE("200", "application/json", "ErrorResponse", "The drive is running the new ROM.", "")
    RESPONSE_ERROR("404", "Cannot open file", "")
    RESPONSE_ERROR("412", "Drive ROM not found", "")
    RESPONSE_ERROR("412", "Drive ROM is invalid", "")
    RESPONSE_ERROR("412", "ROM image is too large", "")
    RESPONSE_ERROR("400", "Invalid Drive 'c'", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
)
API_CALL(PUT, drives, load_rom, NULL, ARRAY({{ "file", P_REQUIRED }}))
{
    return simple_drive_command(args, resp, FLOPPY_LOAD_DOS);
}

API_DOC(POST, drives, load_rom,
    TAG("Drives")
    SUMMARY("Upload a drive ROM")
    DESCRIPTION("The same as the PUT form, with the ROM image in the request body rather than on "
                "the device.")
    PATH("/v1/drives/{drive}:load_rom", "uploadDriveRom", "")
    PATH_PARAM("drive", "string", "Which drive to act on. `a` and `b` are the emulated drives, `softiec` is the IEC file system.", "a")
    PATH_PARAM_ENUM("drive", "a,b,softiec")
    BODY("multipart/form-data", "FileUpload", "The ROM image to load.")
    BODY("application/octet-stream", "", "The ROM image to load, sent raw.")
    RESPONSE("200", "application/json", "ErrorResponse", "The drive is running the new ROM.", "")
    RESPONSE_ERROR("400", "Upload of file failed.", "")
    RESPONSE_ERROR("404", "Cannot open file", "")
    RESPONSE_ERROR("412", "Drive ROM is invalid", "")
    RESPONSE_ERROR("400", "Invalid Drive 'c'", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
)
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

API_DOC(PUT, drives, set_mode,
    TAG("Drives")
    SUMMARY("Change the emulated drive model")
    DESCRIPTION("Switches the drive between the models the hardware can emulate. Not every "
                "product can do all three: hardware that only has the 1541 answers 405, and a "
                "drive that is in the wrong state for the change answers 415.")
    PATH("/v1/drives/{drive}:set_mode", "setDriveMode", "")
    PATH_PARAM("drive", "string", "Which drive to act on. `a` and `b` are the emulated drives, `softiec` is the IEC file system.", "a")
    PATH_PARAM_ENUM("drive", "a,b,softiec")
    PARAM("mode", "string", "Drive model to emulate.", "", "1571")
    PARAM_ENUM("mode", "1541,1571,1581")
    RESPONSE("200", "application/json", "DriveModeResponse", "The drive is emulating the new model.", "")
    RESPONSE_ERROR("400", "Invalid Drive Type '1581'", "")
    RESPONSE_ERROR("405", "This hardware only supports 1541", "")
    RESPONSE_ERROR("415", "Drive is in the wrong mode", "")
    RESPONSE_ERROR("400", "Invalid Drive 'c'", "")
    RESPONSE_ERROR("423", "Could not obtain lock of subsystem", "")
)
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
    if (retval.status != SSRET_OK) {
        resp->error(SubsysCommand::error_string(retval.status));
    }
    resp->json_response(SubsysCommand::http_response_map(retval.status));
}
