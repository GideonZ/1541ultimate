#include "routes.h"
#include "filemanager.h"
#include "json.h"

API_CALL(files, info, NULL, ARRAY({P_END}))
{
    FileManager *fm = FileManager::getFileManager();
    FileInfo info(128);
    FRESULT fres = fm->fstat(args.get_path(), info);
    JSON_Object *reply = JSON::Obj();

    if (fres == FR_OK) {
        reply->add("path", args.get_path())
            ->add("filename", info.lfname)
            ->add("size", info.size)
            //->add("date", date_from_int(info.date))
            //->add("time", time_from_int(info.time))
            ->add("extension", info.extension)
            ->add("errors", JSON::List());

        build_response(resp, 200, reply->render());
    } else {
        reply->add("errors", JSON::List()->add(FileSystem::get_error_string(fres)));
        build_response(resp, 404, reply->render());
    }
    delete reply;
}
