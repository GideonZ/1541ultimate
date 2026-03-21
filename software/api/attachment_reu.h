#ifndef ATTACHMENT_REU
#define ATTACHMENT_REU

#include "routes.h"

// should come from linker file
#define REU_MEMORY_BASE 0x1000000
#define REU_MAX_SIZE    0x1000000

extern "C" {
    #include "multipart.h"
}

class REUWriter
{
    size_t file_size;    
    HTTPReqMessage *req;
    HTTPRespMessage *resp;
    ArgsURI *args;
    const ApiCall_t *func;
public:
    REUWriter() {
        req = NULL;
        resp = NULL;
        args = NULL;
        func = NULL;
        file_size = 0;
    }

    ~REUWriter()
    {
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
        REUWriter *writer = (REUWriter *)block->context;
        writer->collect(block);
    }

    void collect(BodyDataBlock_t *block)
    {
        char filename[128];
        HTTPHeaderField *f;
        int len;

        switch(block->type) {
            case eStart:
                break;
            case eDataStart:
                file_size = 0;
                break;
            case eSubHeader:
                file_size = 0;
                break;
            case eDataBlock:
                len = block->length;
                if (file_size + len > REU_MAX_SIZE) {
                    len = REU_MAX_SIZE - file_size;
                }
                if (len > 0) {
                    memcpy((uint8_t *)(REU_MEMORY_BASE) + file_size, block->data, len);
                    file_size += len;
                }
                break;
            case eDataEnd:
                break;
            case eTerminate:
                printf("Uploaded data to REU has size %d:\n", (int)file_size);

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
                delete this;
                break;
        }
    }
};

REUWriter *attachment_reu(HTTPReqMessage *req, HTTPRespMessage *resp, const ApiCall_t *func, ArgsURI *args);

#endif
