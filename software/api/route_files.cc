#include "routes.h"
#include "filemanager.h"
#include "json.h"

API_CALL(GET, files, info, NULL, ARRAY({ }))
{
    FileManager *fm = FileManager::getFileManager();
    FileInfo info(128);
    FRESULT fres = fm->fstat(args.get_path(), info);

    if (fres == FR_OK) {
        resp->json->add("files", JSON::Obj()
            ->add("path", args.get_path())
            ->add("filename", info.lfname)
            ->add("size", (int)info.size)
            //->add("date", date_from_int(info.date))
            //->add("time", time_from_int(info.time))
            ->add("extension", info.extension));

        resp->json_response(200);
    } else {
        resp->error(FileSystem::get_error_string(fres));
        resp->json_response(404);
    }
}
