#ifndef ATTACHMENT_WRITER
#define ATTACHMENT_WRITER

#include <string.h>
#include <stdlib.h>
#include "indexed_list.h"
#include "pattern.h"
#include "filemanager.h"

extern "C" {
    #include "multipart.h"
}

class TempfileWriter;

typedef void (*WriterCallback_t)(TempfileWriter *, const void *context1, void *context2);

class TempfileWriter
{
    IndexedList<const char *> filenames;
    IndexedList<size_t> filesizes;
    IndexedList<uint8_t *>filebuffers;
    File *fo;
    size_t file_size;    
    HTTPReqMessage *req;
    HTTPRespMessage *resp;
    WriterCallback_t callback;
    const void *context1;
    void *context2;
public:
    TempfileWriter(HTTPReqMessage *req, HTTPRespMessage *resp, WriterCallback_t cb, const void *c1, void *c2)
        : filenames(4, NULL), filesizes(4, 0), filebuffers(4, NULL) {
        fo = NULL;
        this->req = req;
        this->resp = resp;
        callback = cb;
        context1 = c1;
        context2 = c2;
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

    HTTPReqMessage *get_request()
    {
        return req;
    }

    HTTPRespMessage *get_response()
    {
        return resp;
    }

    static void collect_wrapper(BodyDataBlock_t *block)
    {
        TempfileWriter *writer = (TempfileWriter *)block->context;
        writer->collect(block);
    }

    void collect(BodyDataBlock_t *block)
    {
        char filename[128];
        char tempstring[128];
        uint32_t dummy;

        HTTPHeaderField *f;

        switch(block->type) {
            case eStart:
                fo = NULL;
                break;
            case eDataStart:
            {
                mstring canonical_path;
                FRESULT fres = FileManager::getFileManager()->create_temp_file(TempUpload, NULL,
                        FA_WRITE | FA_CREATE_ALWAYS, &fo, &canonical_path);
                if (fres == FR_OK) {
                    filenames.append(strdup(canonical_path.c_str()));
                } else {
                    filenames.append(NULL);
                    fo = NULL;
                }
            }
                file_size = 0;
                break;
            case eSubHeader:
                filename[0] = 0;
                f = (HTTPHeaderField *)block->data;
                for(int i=0; i < block->length; i++) {
                    if (strcasecmp(f[i].key, "Content-Disposition") == 0) {
                        // extract filename from value string, e.g. 'form-data; name="bestand"; filename="sample.html"'
                        strncpy(tempstring, f[i].value, 127); tempstring[127] = 0;
                        char *sub = strstr(tempstring, "filename=\"");
                        if (sub) {
                            sub += 10; // remove the 'filename="' part
                            char *quote = strstr(sub, "\"");
                            if (quote) {
                                *quote = 0;
                            }
                            while((*sub == '.') || (*sub == '\\') || (*sub == '/')) {
                                sub++;
                            }
                            fix_filename(sub);
                            strncpy(filename, sub, sizeof(filename) - 1);
                            filename[127] = 0;
                        }
                    }
                }
            {
                mstring canonical_path;
                FRESULT fres = FileManager::getFileManager()->create_temp_file(TempUpload,
                        filename[0] ? filename : NULL, FA_WRITE | FA_CREATE_ALWAYS, &fo, &canonical_path);
                if (fres == FR_OK) {
                    filenames.append(strdup(canonical_path.c_str()));
                } else {
                    filenames.append(NULL);
                    fo = NULL;
                }
            }
                file_size = 0;
                break;
            case eDataBlock:
                if (fo) {
                    fo->write(block->data, block->length, &dummy);
                    file_size += block->length;
                }
                break;
            case eDataEnd:
                if (fo) {
                    filesizes.append(file_size);
                    FileManager::getFileManager()->fclose(fo);
                    fo = NULL;
                }
                break;
            case eTerminate:
                if (fo) {
                    FileManager::getFileManager()->fclose(fo);
                    fo = NULL;
                }
                printf("Uploaded files:\n");
                for (int i=0;i<filenames.get_elements();i++) {
                    printf("  '%s' (%d)\n", filenames[i], (int)filesizes[i]);
                }
                // The actual function should now be called to do something with these files
                // but since we don't know the context in which this writer is used,
                // we simply call the callback with the context.
                if (callback) {
                    callback(this, context1, context2);
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

#endif
