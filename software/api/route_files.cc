#include "routes.h"
#include "filemanager.h"
#include "json.h"
#include "user_file_interaction.h"
#include "blockdev_file.h"
#include "filesystem_d64.h"

API_DOC(GET, files, info,
    TAG("Files")
    SUMMARY("Read file information")
    DESCRIPTION("Reports the long file name, the size in bytes and the extension of one file. The "
                "path is everything between the route and the command, so a file in a "
                "subdirectory is written out in full.")
    PATH("/v1/files/{path}:info", "getFileInfo", "")
    PATH_PARAM("path", "string", "Path of the file on the device. It contains slashes and must be URL encoded.", "/Usb0/games/disk.d64")
    RESPONSE("200", "application/json", "FileInfoResponse", "What is known about the file.", "")
    RESPONSE_EXAMPLE("200", "A disk image", "{\n  \"files\" : {\n    \"path\" : \"Usb0/games/disk.d64\",\n    \"filename\" : \"disk.d64\",\n    \"size\" : 174848,\n    \"extension\" : \"D64\"\n  },\n  \"errors\" : []\n}", "")
    RESPONSE_ERROR("404", "FILE DOESN'T EXIST", "")
    RESPONSE_ERROR("404", "PATH DOESN'T EXIST", "")
)
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

API_DOC(PUT, files, create_d64,
    TAG("Files")
    SUMMARY("Create an empty D64 image")
    CAUTION("destructive", "A file that is already at that path is overwritten.")
    DESCRIPTION("Creates a file at the given path, fills it with zeros and formats it as a 1541 "
                "disk, so the result can be mounted straight away.\n"
                "\n"
                "`tracks` is 35 by default and may go up to 41; anything past 35 is the extended "
                "area that not every program can read. `diskname` is what goes in the directory "
                "header and defaults to the file name without its extension.")
    PATH("/v1/files/{path}:create_d64", "createD64", "")
    PATH_PARAM("path", "string", "Path of the file on the device. It contains slashes and must be URL encoded.", "/Usb0/games/disk.d64")
    PARAM("tracks", "integer(35..41)", "Number of tracks to format.", "35", "40")
    PARAM("diskname", "string", "Name in the directory header. Defaults to the file name without its extension.", "", "MY DISK")
    RESPONSE("200", "application/json", "DiskImageResponse", "The image was created and formatted.", "")
    RESPONSE_ERROR("400", "Track count should be between 35 and 41.", "")
    RESPONSE_ERROR("500", "FILE EXISTS", "")
)
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
        FRESULT fres;
        {
            // The file system stack borrows f, so it has to be gone before the
            // file is closed. Scoping it here also means there is one close for
            // both outcomes: the failure path used to return without closing,
            // leaking the File and its open handle.
            BlockDevice_File blk(f, 256);
            Partition prt(&blk, 0, 0, 0);
            FileSystemD64 fs(&prt, true);
            fres = fs.format(fn);
        }
        FileManager :: getFileManager()->fclose(f);
        if (fres != FR_OK) {
            resp->error(FileSystem::get_error_string(fres));
            resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        resp->json_response(HTTP_OK);
    } 
}

API_DOC(PUT, files, create_d71,
    TAG("Files")
    SUMMARY("Create an empty D71 image")
    CAUTION("destructive", "A file that is already at that path is overwritten.")
    DESCRIPTION("Creates a file at the given path and formats it as a 1571 disk, which is the "
                "double sided 70 track layout. The track count is fixed.")
    PATH("/v1/files/{path}:create_d71", "createD71", "")
    PATH_PARAM("path", "string", "Path of the file on the device. It contains slashes and must be URL encoded.", "/Usb0/games/disk.d64")
    PARAM("diskname", "string", "Name in the directory header. Defaults to the file name without its extension.", "", "MY DISK")
    RESPONSE("200", "application/json", "DiskImageResponse", "The image was created and formatted.", "")
    RESPONSE_ERROR("500", "FILE EXISTS", "")
)
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
        FRESULT fres;
        {
            // The file system stack borrows f, so it has to be gone before the
            // file is closed. Scoping it here also means there is one close for
            // both outcomes: the failure path used to return without closing,
            // leaking the File and its open handle.
            BlockDevice_File blk(f, 256);
            Partition prt(&blk, 0, 0, 0);
            FileSystemD71 fs(&prt, true);
            fres = fs.format(fn);
        }
        FileManager :: getFileManager()->fclose(f);
        if (fres != FR_OK) {
            resp->error(FileSystem::get_error_string(fres));
            resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        resp->json_response(HTTP_OK);
    } 
}

API_DOC(PUT, files, create_d81,
    TAG("Files")
    SUMMARY("Create an empty D81 image")
    CAUTION("destructive", "A file that is already at that path is overwritten.")
    DESCRIPTION("Creates a file at the given path and formats it as a 1581 disk, 3200 blocks of "
                "256 bytes. The size is fixed.")
    PATH("/v1/files/{path}:create_d81", "createD81", "")
    PATH_PARAM("path", "string", "Path of the file on the device. It contains slashes and must be URL encoded.", "/Usb0/games/disk.d64")
    PARAM("diskname", "string", "Name in the directory header. Defaults to the file name without its extension.", "", "MY DISK")
    RESPONSE("200", "application/json", "DiskImageResponse", "The image was created and formatted.", "")
    RESPONSE_ERROR("500", "FILE EXISTS", "")
)
API_CALL(PUT, files, create_d81, NULL, ARRAY( { { "diskname", P_OPTIONAL } } ))
{
    resp->json->add("path", args.get_full_path());

    enforce_diskname(args);
    const char *fn = args["diskname"];
    resp->json->add("diskname", fn);

    File *f = create_file_of_size(resp, args.get_full_path(), 256*3200);
    if (f) {
        FRESULT fres;
        {
            // The file system stack borrows f, so it has to be gone before the
            // file is closed. Scoping it here also means there is one close for
            // both outcomes: the failure path used to return without closing,
            // leaking the File and its open handle.
            BlockDevice_File blk(f, 256);
            Partition prt(&blk, 0, 0, 0);
            FileSystemD81 fs(&prt, true);
            fres = fs.format(fn);
        }
        FileManager :: getFileManager()->fclose(f);
        if (fres != FR_OK) {
            resp->error(FileSystem::get_error_string(fres));
            resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        resp->json_response(HTTP_OK);
    } 
}

API_DOC(PUT, files, create_dnp,
    TAG("Files")
    SUMMARY("Create an empty DNP image")
    CAUTION("destructive", "A file that is already at that path is overwritten.")
    DESCRIPTION("Creates a native partition image and formats it. Each track is 64 KB, so the "
                "largest image, 255 tracks, is just under 16 MB. Unlike the other three, `tracks` "
                "has to be given here because there is no conventional size.")
    PATH("/v1/files/{path}:create_dnp", "createDnp", "")
    PATH_PARAM("path", "string", "Path of the file on the device. It contains slashes and must be URL encoded.", "/Usb0/games/disk.d64")
    PARAM("tracks", "integer(1..255)", "Number of 64 KB tracks.", "", "64")
    PARAM("diskname", "string", "Name in the directory header. Defaults to the file name without its extension.", "", "MY DISK")
    RESPONSE("200", "application/json", "DiskImageResponse", "The image was created and formatted.", "")
    RESPONSE_ERROR("400", "Invalid number of tracks (1-255).", "")
    RESPONSE_ERROR("500", "FILE EXISTS", "")
)
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
        FRESULT fres;
        {
            // The file system stack borrows f, so it has to be gone before the
            // file is closed. Scoping it here also means there is one close for
            // both outcomes: the failure path used to return without closing,
            // leaking the File and its open handle.
            BlockDevice_File blk(f, 256);
            Partition prt(&blk, 0, 0, 0);
            FileSystemDNP fs(&prt, true);
            fres = fs.format(fn);
        }
        FileManager :: getFileManager()->fclose(f);
        if (fres != FR_OK) {
            resp->error(FileSystem::get_error_string(fres));
            resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        resp->json_response(HTTP_OK);
    } 
}
