#ifndef ATTACHMENT_WRITER
#define ATTACHMENT_WRITER

extern "C" {
    #include "multipart.h"
}

#define FILE_PATH "/Temp/"

class TempfileWriter
{
    IndexedList<const char *> filenames;
    IndexedList<size_t> filesizes;
    IndexedList<uint8_t *>filebuffers;
    static int temp_count;
    FILE *fo;
    size_t file_size;    
    HTTPReqMessage *req;
    HTTPRespMessage *resp;
    ArgsURI *args;
    const ApiCall_t *func;
public:
    TempfileWriter() : filenames(4, NULL), filesizes(4, 0), filebuffers(4, NULL) {
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
        for (int i=0; i<filebuffers.get_elements(); i++) {
            free(filebuffers[i]);
        }
    }

    void create_callback(HTTPReqMessage *req, HTTPRespMessage *resp, ArgsURI *args, const ApiCall_t *func)
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
                fo = NULL;
                break;
            case eDataStart:
                sprintf(filename, FILE_PATH "temp%04x", temp_count++);
                filenames.append(strdup(filename));
                fo = fopen(filename, "wb");
                file_size = 0;
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
                file_size = 0;
                break;
            case eDataBlock:
                if (fo) {
                    fwrite(block->data, 1, block->length, fo);
                    file_size += block->length;
                }
                break;
            case eDataEnd:
                if (fo) {
                    filesizes.append(file_size);
                    fclose(fo);
                    fo = NULL;
                }
                break;
            case eTerminate:
                if (fo) {
                    fclose(fo);
                    fo = NULL;
                }
                printf("Uploaded files:\n");
                for (int i=0;i<filenames.get_elements();i++) {
                    printf("  '%s' (%d)\n", filenames[i], filesizes[i]);
                }
                // The actual function should now be called to do something with these files
                if (func) {
                    ResponseWrapper respw(resp);
                    if (args->Validate(*func, &respw) != 0) {
                        respw.json_response(HTTP_BAD_REQUEST);
                    } else {
                        func->proc(*args, req, &respw, this); // this = body (attachment writer object)
                    }
                    delete args;
                }
                break;
        }
    }

    const char *get_filename(int index)
    {
        return filenames[index];
    }
    size_t get_filesize(int index)
    {
        return filesizes[index];
    }
    uint8_t *get_buffer(int index)
    {
        return filebuffers[index];
    }

    int buffer_file(int index, size_t max_size)
    {
        size_t size = filesizes[index];
        if (size == 0) {
            return -1; // no data
        }
        if (size > max_size) {
            return -2; // too large
        }

        uint8_t *buffer = (uint8_t *)malloc(size);
        if (buffer) {
            FILE *f = fopen(get_filename(index), "rb");
            if (f) {
                fread(buffer, size, 1, f);
                fclose(f);
                filebuffers.append(buffer);
                return 0; // success
            }
            free(buffer);
            return -4; // can't open file
        }
        return -3; // no mem
    }
};

TempfileWriter *attachment_writer(HTTPReqMessage *req, HTTPRespMessage *resp, const ApiCall_t *func, ArgsURI *args);

#endif
