#ifndef PROVIDER_FLASH_TEST_PORT_H
#define PROVIDER_FLASH_TEST_PORT_H
#include "flash.h"
#include <string.h>
struct MemoryFlash : Flash {
    enum Fault { None, EraseBefore, EraseAfter, WriteBefore, Torn, WriteAfter, Verify } fault=None;
    unsigned reads=0,writes=0,erases=0;
    uint8_t memory[16384];
    bool verify=false;
    unsigned geometry_fault=0;
    MemoryFlash() { memset(memory,0xff,sizeof(memory)); }
    int get_page_size() override { return geometry_fault==1 ? 264 : 256; }
    int get_number_of_pages() override { return geometry_fault==2 ? 0 : 48; }
    int get_number_of_config_pages() override { return geometry_fault==3 ? -1 : 2; }
    int get_sector_size(int) override { return geometry_fault==4 ? 8192 : 4096; }
    int page_to_sector(int page) override { return page/16+(geometry_fault==5); }
    void get_image_addresses(int id,t_flash_address *a) override {
        *a={uint8_t(id),0,4096,4096,8192};
        if (geometry_fault==6) a->id=0;
        if (geometry_fault==7) a->start=0;
        if (geometry_fault==8) a->max_length=4096;
        if (geometry_fault==9) a->device_addr=4097;
        if (geometry_fault==10) a->max_length=16384;
        if (geometry_fault==11) { a->device_addr=0; a->start=0; a->max_length=12288; }
    }
    bool read_page(int page,void *out) override {
        ++reads;
        if (fault==Verify && verify) return false;
        if (page<0 || page>=64) return false;
        memcpy(out,memory+page*256,256); return true;
    }
    bool erase_sector(int sector) override {
        ++erases; if (fault==EraseBefore) return false;
        if (sector!=1 && sector!=2) return false;
        memset(memory+sector*4096,0xff,4096);
        return fault!=EraseAfter;
    }
    bool write_page(int page,const void *data) override {
        ++writes; if (fault==WriteBefore) return false;
        if (page!=16 && page!=32) return false;
        const uint8_t *in=static_cast<const uint8_t*>(data);
        for (unsigned i=0;i<(fault==Torn ? 19U : 256U);++i) memory[page*256+i]&=in[i];
        verify=true; return fault!=WriteAfter && fault!=Torn;
    }
};
#endif
