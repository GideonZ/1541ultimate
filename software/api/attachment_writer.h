#ifndef ATTACHMENT_WRITER
#define ATTACHMENT_WRITER

extern "C" {
    #include "multipart.h"
}

#define FILE_PATH "/Temp/"

class TempfileWriter
{
    IndexedList<const char *> filenames;
    static int temp_count;
    FILE *fo;
    
    HTTPReqMessage *req;
    HTTPRespMessage *resp;
    ArgsURI *args;
    APIFUNC func;
public:
    TempfileWriter() : filenames(4, NULL) {
        fo = NULL;
        req = NULL;
        resp = NULL;
        args = NULL;
        func = NULL;
    }

    ~TempfileWriter()
    {
        // filenames were created with strdup, so we need to free them here
        for (int i=0; i<filenames.get_elements(); i++) {
            free((char *)filenames[i]);
        }
    }

    void create_callback(HTTPReqMessage *req, HTTPRespMessage *resp, ArgsURI *args, APIFUNC func)
    {
        this->req = req;
        this->resp = resp;
        this->args = args;
        this->func = func;
    }

    static void collect_wrapper(BodyDataBlock_t *block)
    {
        TempfileWriter *writer = (TempfileWriter *)block->context;
        writer->collect(block);
    }

    void collect(BodyDataBlock_t *block)
    {
        char filename[128];
        HTTPHeaderField *f;

        switch(block->type) {
            case eStart:
                break;
            case eDataStart:
                sprintf(filename, FILE_PATH "temp%04x", temp_count++);
                filenames.append(strdup(filename));
                fo = fopen(filename, "wb");
                break;
            case eSubHeader:
                sprintf(filename, FILE_PATH "temp%04x", temp_count++);
                f = (HTTPHeaderField *)block->data;
                for(int i=0; i < block->length; i++) {
                    if (strcasecmp(f[i].key, "Content-Disposition") == 0) {
                        // extract filename from value string, e.g. 'form-data; name="bestand"; filename="sample.html"'
                        char *sub = strstr(f[i].value, "filename=\"");
                        if (sub) {
                            strncpy(filename + strlen(FILE_PATH), sub + 10, 127-strlen(FILE_PATH) );
                            filename[127] = 0;
                            char *quote = strstr(filename, "\"");
                            if (quote) {
                                *quote = 0;
                            }
                        }
                    }
                }
                filenames.append(strdup(filename));
                fo = fopen(filename, "wb");
                break;
            case eDataBlock:
                if (fo) {
                    fwrite(block->data, 1, block->length, fo);
                }
                break;
            case eDataEnd:
                if (fo) {
                    fclose(fo);
                }
                break;
            case eTerminate:
                printf("Uploaded files:\n");
                for (int i=0;i<filenames.get_elements();i++) {
                    printf("  '%s'\n", filenames[i]);
                }
                // The actual function should now be called to do something with these files
                if (func) {
                    ResponseWrapper respw(resp);
                    func(*args, req, &respw, this);
                    delete args;
                }
                break;
        }
    }

    const char *get_filename(int index)
    {
        return filenames[index];
    }
};

TempfileWriter *attachment_writer(HTTPReqMessage *req, HTTPRespMessage *resp, APIFUNC func, ArgsURI *args);

#endif
