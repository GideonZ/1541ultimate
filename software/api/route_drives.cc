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

DriveToSubsys driveToSubsys;
ImageTypeToInt imageTypeToInt;
ImageTypeToCommand imageTypeToCommand;
ModeToInt modeToInt;

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

static void iec_info(JSON_List *obj)
{
    char buffer[64];
    iec_if.get_error_string(buffer);
    
    JSON_List *partitions = JSON::List();
    for (int i=0; i < MAX_PARTITIONS; i++) {
        const char *p = iec_if.get_partition_dir(i);
        if (p) {
            partitions->add(JSON::Obj()->add("id", i)->add("path", p));
        }
    }

    obj->add(JSON::Obj()->add("softiec", JSON::Obj()
        ->add("enabled", iec_if.iec_enable))
        ->add("bus_id", iec_if.get_current_iec_address())
        ->add("type", "DOS emulation")
        ->add("last_error", buffer)
        ->add("partitions", partitions));
}

// List all the available drives
API_CALL(GET, drives, none, NULL, ARRAY({P_END}))
{
    JSON_List *drives = JSON::List();
    resp->json->add("drives", drives);
    if (c1541_A) drive_info(drives, c1541_A, "a");
    if (c1541_B) drive_info(drives, c1541_B, "b");
    // iec_info(obj);
    resp->json_response(200);
}

void api_mount(ResponseWrapper *resp, const char *fn, const char *drive, const char *type, const char *mode)
{
    int subsys_id = driveToSubsys[drive];
    if (subsys_id < 0) {
        resp->error("Invalid Drive '%s'", drive);
        resp->json_response(403);
        return;
    }

    int ftype = imageTypeToInt[type] | modeToInt[mode];
    int command = imageTypeToCommand[type];

    if (!command) {
        resp->error("Invalid Type '%s'", type);
        resp->json_response(403);
        return;
    }

    resp->json->add("Subsys", subsys_id)->add("Ftype", ftype);
    resp->json_response(200);
}

API_CALL(PUT, drives, mount, NULL, ARRAY({{ "image", P_REQUIRED }, { "type", P_OPTIONAL }, { "mode", P_OPTIONAL }, P_END }))
{
    if (args.Validate(http_PUT_drives_mount, resp) != 0) {
        resp->json_response(400);
        return;
    }
    printf("Mount disk from path '%s' on drive '%s'\n", args["image"], args.get_path());
    char ext[4];
    get_extension(args["image"], ext);
    api_mount(resp, args["image"], args.get_path(), args.get_or("type", ext), args["mode"]);
}

API_CALL(POST, drives, mount, &attachment_writer, ARRAY({ { "type", P_OPTIONAL }, { "mode", P_OPTIONAL }, P_END }))
{
    if (args.Validate(http_POST_drives_mount, resp) != 0) {
        resp->json_response(400);
        return;
    }
    TempfileWriter *handler = (TempfileWriter *)body;
    const char *fn = handler->get_filename(0);
    if (!fn) {
        resp->error("Upload of file failed.");
        resp->json_response(400);
    }
    printf("Mount disk from upload: '%s'\n", fn);
    char ext[4];
    get_extension(fn, ext);
    api_mount(resp, fn, args.get_path(), args.get_or("type", ext), args["mode"]);
}
