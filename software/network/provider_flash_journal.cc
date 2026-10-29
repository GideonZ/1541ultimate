#include "provider_flash_journal.h"
#include "flash.h"
#include <string.h>
namespace provider_control {
namespace {
const uint32_t Magic = 0x4a534c54; // TLSJ, reserved configuration page identifier.
uint32_t crc(const uint8_t *p, unsigned n) {
    uint32_t v=UINT32_MAX;
    for (unsigned i=0;i<n;++i) {
        v^=p[i];
        for (unsigned b=0;b<8;++b) v=(v>>1) ^ (0xedb88320U & (0U-(v&1)));
    }
    return ~v;
}
void put(uint8_t *p,uint64_t v,unsigned n) { for (unsigned i=0;i<n;++i) p[i]=uint8_t(v>>(8*i)); }
uint64_t get(const uint8_t *p,unsigned n) { uint64_t v=0; for (unsigned i=0;i<n;++i) v|=uint64_t(p[i])<<(8*i); return v; }
}
FlashIncarnationJournal::FlashIncarnationJournal(Flash& flash,unsigned a,unsigned b,uint64_t domain) :
    flash_(flash),slots_{a,b},domain_(domain),failed_(!domain || a==b) {}
bool FlashIncarnationJournal::geometry(unsigned pages[2]) {
    t_flash_address address={}; flash_.get_image_addresses(FLASH_ID_CONFIG,&address);
    if (failed_ || address.id!=FLASH_ID_CONFIG || address.device_addr<0 || address.start!=address.device_addr ||
        flash_.get_page_size()!=256 || address.device_addr%4096 || address.max_length<=0 ||
        flash_.get_number_of_pages()<=0) return false;
    const uint64_t capacity=uint64_t(flash_.get_number_of_pages())*256;
    const int slots=flash_.get_number_of_config_pages();
    const uint64_t end=uint64_t(address.device_addr)+unsigned(address.max_length);
    if (slots<=0 || uint64_t(slots)*4096>capacity || end>capacity) return false;
    // W25Q/S25FL configuration slots are counted backwards from flash end.
    // The image can reserve more space than the driver exposes as slots.
    const uint64_t config_start=capacity-uint64_t(slots)*4096;
    if (config_start<unsigned(address.device_addr) || capacity>end) return false;
    for (unsigned i=0;i<2;++i) {
        uint64_t start=config_start+uint64_t(slots_[i])*4096;
        if (slots_[i]>=unsigned(slots) || start+4096>end ||
            flash_.get_sector_size(int(start))!=4096) return false;
        pages[i]=unsigned(start/256);
        if (flash_.page_to_sector(pages[i])!=int(start/4096) ||
            flash_.page_to_sector(pages[i]+15)!=int(start/4096)) return false;
    }
    return true;
}
bool FlashIncarnationJournal::read(unsigned page,uint64_t& epoch) {
    uint8_t bytes[256]; memset(bytes,0,sizeof(bytes));
    if (!flash_.read_page(page,bytes) || get(bytes,4)!=Magic || get(bytes+4,4)!=1 ||
        get(bytes+8,8)!=domain_ || get(bytes+24,4)!=crc(bytes,24) || get(bytes+28,4)!=~uint32_t(get(bytes+24,4))) return false;
    for (unsigned i=32;i<256;++i) if (bytes[i]!=0xff) return false;
    epoch=get(bytes+16,8); return true;
}
bool FlashIncarnationJournal::write(unsigned page,uint64_t epoch,bool erase) {
    uint8_t bytes[256]; memset(bytes,0xff,sizeof(bytes));
    put(bytes,Magic,4); put(bytes+4,1,4); put(bytes+8,domain_,8); put(bytes+16,epoch,8);
    put(bytes+24,crc(bytes,24),4); put(bytes+28,~uint32_t(get(bytes+24,4)),4);
    if ((erase && !flash_.erase_sector(flash_.page_to_sector(page))) || !flash_.write_page(page,bytes)) return false;
    uint64_t verify=0;
    return read(page,verify) && verify==epoch;
}
bool FlashIncarnationJournal::provision() {
    unsigned pages[2]; bool ok=geometry(pages);
    uint8_t bytes[256];
    for (unsigned s=0;ok && s<2;++s) for (unsigned p=0;ok && p<16;++p) {
        memset(bytes,0,sizeof(bytes)); ok=flash_.read_page(pages[s]+p,bytes);
        for (unsigned b=0;ok && b<256;++b) ok=bytes[b]==0xff;
    }
    if (ok) ok=write(pages[0],0,false) && write(pages[1],1,false);
    if (!ok) failed_=true;
    return ok;
}
bool FlashIncarnationJournal::reserve(void *context,uint64_t& result) {
    auto& self=*static_cast<FlashIncarnationJournal*>(context);
    result=0; unsigned pages[2]; uint64_t epochs[2];
    bool ok=self.geometry(pages) && self.read(pages[0],epochs[0]) && self.read(pages[1],epochs[1]);
    if (ok) {
        unsigned newer=epochs[1]>epochs[0] ? 1 : 0;
        uint64_t high=epochs[newer],low=epochs[1-newer];
        // An erased/torn/corrupt sector never falls back to an older identity.
        ok=high!=UINT64_MAX && high>low && high-low==1;
        if (ok) {
            ok=self.write(pages[1-newer],high+1,true);
            if (ok) result=high+1;
        }
    }
    if (!ok) self.failed_=true;
    return ok;
}
}
