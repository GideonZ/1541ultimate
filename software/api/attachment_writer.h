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

// The below will show up as: LOAD"/TEMP/WEB-UPLOAD",8,1
// on the C64 screen. The truncation after 16 characters nicely
// hides the slightly cryptic "-0001" part of the filename.
#ifndef TEMP_FILE_PATH
#define TEMP_FILE_PATH "/Temp/"
#endif
#define TEMP_FILE_PREFIX "web-upload-"
#define TEMP_FILE_PATTERN "web-upload-????"


class TempfileWriter;

typedef void (*WriterCallback_t)(TempfileWriter *, const void *context1, void *context2);

class TempfileWriter
{
    IndexedList<const char *> filenames;
    IndexedList<size_t> filesizes;
    IndexedList<uint8_t *>filebuffers;
    static int temp_count;
#ifdef USE_FILEMANAGER
    File *fo;
#else
    FILE *fo;
#endif
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
        FileManager *fm = FileManager::getFileManager();
        switch(block->type) {
            case eStart:
                fo = NULL;
                break;
            case eDataStart:
                sprintf(filename, TEMP_FILE_PATH TEMP_FILE_PREFIX "%04x", ++temp_count);
                filenames.append(strdup(filename));
                fm->house_keeping_delete(TEMP_FILE_PATH, TEMP_FILE_PATTERN);
#ifdef USE_FILEMANAGER
                fm->fopen(filename, FA_WRITE | FA_CREATE_ALWAYS, &fo);
#else
                fo = fopen(filename, "wb");
#endif
                file_size = 0;
                break;
            case eSubHeader:
                sprintf(filename, TEMP_FILE_PATH TEMP_FILE_PREFIX "%04x", ++temp_count);
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
                            strncpy(filename + strlen(TEMP_FILE_PATH), sub, 127-strlen(TEMP_FILE_PATH) );
                            filename[127] = 0;
                        }
                    }
                }
                filenames.append(strdup(filename));
                fm->house_keeping_delete(TEMP_FILE_PATH, TEMP_FILE_PATTERN);
#ifdef USE_FILEMANAGER
                fm->fopen(filename, FA_WRITE | FA_CREATE_ALWAYS, &fo);
#else
                fo = fopen(filename, "wb");
#endif
                file_size = 0;
                break;
            case eDataBlock:
                if (fo) {
#ifdef USE_FILEMANAGER
                    fo->write(block->data, block->length, &dummy);
#else
                    fwrite(block->data, 1, block->length, fo);
#endif
                    file_size += block->length;
                }
                break;
            case eDataEnd:
                if (fo) {
                    filesizes.append(file_size);
#ifdef USE_FILEMANAGER
                    fm->fclose(fo);
#else
                    fclose(fo);
#endif
                    fo = NULL;
                }
                break;
            case eTerminate:
                if (fo) {
#ifdef USE_FILEMANAGER
                    fm->fclose(fo);
#else
                    fclose(fo);
#endif
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
