#include "routes.h"
#include "filemanager.h"
#include "json.h"
#include "user_file_interaction.h"
#include "blockdev_file.h"
#include "filesystem_d64.h"

API_CALL(GET, files, info, NULL, ARRAY({ }))
{
    FileManager *fm = FileManager::getFileManager();
    FileInfo info(128);
    FRESULT fres = fm->fstat(args.get_full_path(), info);

    if (fres == FR_OK) {
        resp->json->add("files", JSON::Obj()
            ->add("path", args.get_full_path())
            ->add("filename", info.lfname)
            ->add("size", (int)info.size)
            //->add("date", date_from_int(info.date))
            //->add("time", time_from_int(info.time))
            ->add("extension", info.extension));

        resp->json_response(HTTP_OK);
    } else {
        resp->error(FileSystem::get_error_string(fres));
        resp->json_response(HTTP_NOT_FOUND);
    }
}

static File *create_file_of_size(ResponseWrapper *resp, const char *fn, int size)
{
    FileManager *fm = FileManager::getFileManager();
    uint32_t written;
    File *f;

    FRESULT fres = fm->fopen(fn, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &f);
    if (fres != FR_OK) {
        resp->error(FileSystem::get_error_string(fres));
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return NULL;
    }
    fres = write_zeros(f, size, written);
    resp->json->add("bytes_written", (int)written);

    if (fres != FR_OK) {
        resp->error(FileSystem::get_error_string(fres));
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        fm->fclose(f);
        return NULL;
    }
    fres = f->seek(0);
    if (fres != FR_OK) {
        resp->error(FileSystem::get_error_string(fres));
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        fm->fclose(f);
        return NULL;
    }
    return f;
}

static void enforce_diskname(ArgsURI &args)
{
    const char *fn = args["diskname"];
    if (!fn) {
        char *dup = strdup(get_filename(args.get_full_path()));
        set_extension(dup, "", strlen(dup));
        args.set("diskname", dup);
        args.temporary(dup);
    }
}

API_CALL(PUT, files, create_d64, NULL, ARRAY( { { "tracks", P_OPTIONAL }, { "diskname", P_OPTIONAL } } ))
{
    int tracks = args.get_int("tracks", 35);
    resp->json->add("path", args.get_full_path());
    resp->json->add("tracks", tracks);

    enforce_diskname(args);
    const char *fn = args["diskname"];
    resp->json->add("diskname", fn);

    if ((tracks < 35) || (tracks > 41)) {
        resp->error("Track count should be between 35 and 41.", tracks);
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    int size = (17 * (tracks - 35) + 683) * 256;
    File *f = create_file_of_size(resp, args.get_full_path(), size);
    if (f) {
        BlockDevice_File blk(f, 256);
        Partition prt(&blk, 0, 0, 0);
        FileSystemD64 fs(&prt, true);
        FRESULT fres = fs.format(fn);
        if (fres != FR_OK) {
            resp->error(FileSystem::get_error_string(fres));
            resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        FileManager :: getFileManager()->fclose(f);
        resp->json_response(HTTP_OK);
    } 
}

API_CALL(PUT, files, create_d71, NULL, ARRAY( { { "diskname", P_OPTIONAL } } ))
{
    int tracks = 70;
    resp->json->add("path", args.get_full_path());
    resp->json->add("tracks", tracks);

    enforce_diskname(args);
    const char *fn = args["diskname"];
    resp->json->add("diskname", fn);

    int size = 683 * 2 * 256;
    File *f = create_file_of_size(resp, args.get_full_path(), size);
    if (f) {
        BlockDevice_File blk(f, 256);
        Partition prt(&blk, 0, 0, 0);
        FileSystemD71 fs(&prt, true);
        FRESULT fres = fs.format(fn);
        if (fres != FR_OK) {
            resp->error(FileSystem::get_error_string(fres));
            resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        FileManager :: getFileManager()->fclose(f);
        resp->json_response(HTTP_OK);
    } 
}

API_CALL(PUT, files, create_d81, NULL, ARRAY( { { "diskname", P_OPTIONAL } } ))
{
    resp->json->add("path", args.get_full_path());

    enforce_diskname(args);
    const char *fn = args["diskname"];
    resp->json->add("diskname", fn);

    File *f = create_file_of_size(resp, args.get_full_path(), 256*3200);
    if (f) {
        BlockDevice_File blk(f, 256);
        Partition prt(&blk, 0, 0, 0);
        FileSystemD81 fs(&prt, true);
        FRESULT fres = fs.format(fn);
        if (fres != FR_OK) {
            resp->error(FileSystem::get_error_string(fres));
            resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        FileManager :: getFileManager()->fclose(f);
        resp->json_response(HTTP_OK);
    } 
}

API_CALL(PUT, files, create_dnp, NULL, ARRAY( { { "tracks", P_REQUIRED }, { "diskname", P_OPTIONAL } } ))
{
    int tracks = args.get_int("tracks", 0);
    resp->json->add("path", args.get_full_path());
    resp->json->add("tracks", tracks);
    if ((tracks < 1) || (tracks > 255)) {
        resp->error("Invalid number of tracks (1-255).");
        resp->json_response(HTTP_BAD_REQUEST);
        return;        
    }

    enforce_diskname(args);
    const char *fn = args["diskname"];
    resp->json->add("diskname", fn);

    File *f = create_file_of_size(resp, args.get_full_path(), tracks * 65536);
    if (f) {
        BlockDevice_File blk(f, 256);
        Partition prt(&blk, 0, 0, 0);
        FileSystemDNP fs(&prt, true);
        FRESULT fres = fs.format(fn);
        if (fres != FR_OK) {
            resp->error(FileSystem::get_error_string(fres));
            resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        FileManager :: getFileManager()->fclose(f);
        resp->json_response(HTTP_OK);
    } 
}
