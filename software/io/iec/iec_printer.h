#ifndef IEC_PRINTER_H
#define IEC_PRINTER_H

#include "integer.h"
#include "iec.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "filemanager.h"
#include "mystring.h"
#include "mps_printer.h"

#define IEC_PRINTER_BUFFERSIZE  256

//enum t_printer_retval {
//    IEC_OK=0,
//    IEC_BYTE_LOST=-6
//};

class IecPrinter
{
    IecInterface *interface;
    FileManager *fm;
    const char *filename;
    MpsPrinter *mps;

    uint8_t buffer[IEC_PRINTER_BUFFERSIZE];
    int  pointer;
    File *f;
    bool raw;
    bool init;

public:
    IecPrinter()
    {
        fm = FileManager :: getFileManager();

        filename = NULL;
        f = NULL;
        mps = MpsPrinter::getMpsPrinter();
        pointer = 0;
        raw = false;
        init = true;
    }

    virtual ~IecPrinter()
    {
        close_file();
    }

    virtual int push_data(uint8_t b)
    {
        buffer[pointer++] = b;
        if(pointer == IEC_PRINTER_BUFFERSIZE) {
            if (raw)
            {
                if(!f)
                    open_file();
                if (f) {
                    uint32_t bytes;
                    f->write(buffer, IEC_PRINTER_BUFFERSIZE, &bytes);
                    pointer = 0;
                } else {
                    pointer--;
                    return IEC_BYTE_LOST;
                }
            } else {
                mps->Interpreter(buffer,IEC_PRINTER_BUFFERSIZE);
                pointer = 0;
            }
        }

        return IEC_OK;
    }

    virtual int push_command(uint8_t b)
    {
        switch(b) {
            case 0xFE: // Received printer OPEN
            case 0x00: // CURSOR UP (graphics/upper case) mode (default)
                if (!raw) {
                    if (pointer)
                    {
                        mps->Interpreter(buffer,pointer);
                        pointer=0;
                    }
                    mps->setCharsetVariant(0);
                }
                break;

            case 0x07: // CURSOR DOWN (upper case/lower case) mode
                if (!raw) {
                    if (pointer)
                    {
                        mps->Interpreter(buffer,pointer);
                        pointer=0;
                    }
                    mps->setCharsetVariant(1);
                }
                break;

            case 0xFF: // Received EOI (Close file)
                if (raw) close_file();
                break;
        }
        return IEC_OK;
    }

    virtual int flush(void)
    {
        if (pointer && raw && !f)
            open_file();

        close_file();
        return IEC_OK;
    }

    virtual int init_done(void)
    {
        init = false;
        return IEC_OK;
    }

    virtual int reset(void)
    {
        pointer = 0;
        mps->Reset();
        return IEC_OK;
    }

    virtual int set_filename(const char *file)
    {
        filename = file;
        mps->setFilename((char *)filename);
        return IEC_OK;
    }

    virtual int set_emulation(int d)
    {
        if (!raw && pointer) {
            mps->Interpreter(buffer,pointer);
            pointer=0;
        }
        switch (d)
        {
            case 0: mps->setInterpreter(MPS_PRINTER_INTERPRETER_CBM); break;
            case 1: mps->setInterpreter(MPS_PRINTER_INTERPRETER_EPSONFX80); break;
            case 2: mps->setInterpreter(MPS_PRINTER_INTERPRETER_IBMGP); break;
            case 3: mps->setInterpreter(MPS_PRINTER_INTERPRETER_IBMPP); break;
        }
        return IEC_OK;
    }

    virtual int set_ink_density(int d)
    {
        if (!raw && pointer) {
            mps->Interpreter(buffer,pointer);
            pointer=0;
        }
        mps->setDotSize(d);
        return IEC_OK;
    }

    virtual int set_cbm_charset(int d)
    {
        if (!raw && pointer) {
            mps->Interpreter(buffer,pointer);
            pointer=0;
        }
        mps->setCBMCharset(d);
        return IEC_OK;
    }

    virtual int set_epson_charset(int d)
    {
        if (!raw && pointer) {
            mps->Interpreter(buffer,pointer);
            pointer=0;
        }
        mps->setEpsonCharset(d);
        return IEC_OK;
    }

    virtual int set_ibm_charset(int d)
    {
        if (!raw && pointer) {
            mps->Interpreter(buffer,pointer);
            pointer=0;
        }
        mps->setIBMCharset(d);
        return IEC_OK;
    }

    virtual int set_output_type(int t)
    {
        bool new_raw = raw;
        switch (t)
        {
            case 0: // RAW format output
                new_raw = true;
                break;

            case 1: // PNG format output
                new_raw = false;
                break;
        }

        if (!init && new_raw != raw)
            close_file();

        raw = new_raw;
        return IEC_OK;
    }

private:

    int open_file(void)
    {
        FRESULT fres = fm->fopen((const char *) NULL, filename, FA_WRITE|FA_OPEN_ALWAYS, &f);
        if(f) {
            printf("Successfully opened printer file %s\n", filename);
            f->seek(f->get_size());
        } else {
            FRESULT fres = fm->fopen((const char *) filename, FA_WRITE|FA_CREATE_NEW, &f);
            if(f) {
                printf("Successfully created printer file %s\n", filename);
            } else {
                printf("Can't open printer file %s: %s\n", filename, FileSystem :: get_error_string(fres));
                return 1;
            }
        }
        return 0;
    }

    int close_file(void) // file should be open
    {
        if (raw) {
            if(f) {
                if (pointer > 0) {
                    uint32_t bytes;

                    f->write(buffer, pointer, &bytes);
                    pointer = 0;
                }
                fm->fclose(f);
                f = NULL;
            }
        } else {
            if (pointer > 0)
            {
                mps->Interpreter(buffer, pointer);
                pointer=0;
            }
            mps->FormFeed();
        }
        return 0;
    }
};

#endif /* IEC_PRINTER_H */
