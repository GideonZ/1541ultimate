// The fakes under the production usb_scsi.cc: the RTOS calls, the USB host layer and the file manager.
// bulk_in models usb_base.cc: one nano command moves packets until one is not full, flips the toggle once per
// accepted packet, and the caller's stop rule is bot_in_stop_on_short from usb_bot_policy.h. The loop itself is
// not compiled from usb_base.cc (that needs the controller registers).
#include <stdio.h>
#include <string.h>
#include "scsi_sim.h"
#include "usb_scsi.h"
#include "filemanager.h"

Sim sim;

void Sim::reset()
{
    in.clear(); out_results.clear(); log.clear();
    memset(last_cbw, 0, sizeof last_cbw);
    now = 0; mutex_busy = false; takes = gives = 0; mps = 512; dev_in_toggle = dev_out_toggle = 0; lost = 0;
}

void Sim::data(int n, uint8_t fill)
{
    SimPacket p; p.kind = e_sim_data; p.bytes.assign(n, fill); in.push_back(p);
}

void Sim::stream(int n, uint8_t fill)
{
    for (; n >= mps; n -= mps) data(mps, fill);
    if (n) data(n, fill);
}

void Sim::csw(uint8_t status, uint32_t residue, int len)
{
    SimPacket p; p.kind = e_sim_csw; p.status = status; p.residue = residue; p.csw_len = len; in.push_back(p);
}

int Sim::count(SimEventKind k) const
{
    int n = 0;
    for (size_t i = 0; i < log.size(); i++) n += (log[i].kind == k);
    return n;
}

std::vector<int> Sim::unstalls() const
{
    std::vector<int> v;
    for (size_t i = 0; i < log.size(); i++) if (log[i].kind == e_ev_unstall) v.push_back(log[i].a);
    return v;
}

std::vector<int> Sim::recovery_steps() const
{
    std::vector<int> v;
    for (size_t i = 0; i < log.size(); i++) {
        if (log[i].kind == e_ev_msr) v.push_back(0);
        if (log[i].kind == e_ev_unstall) v.push_back(log[i].a);
    }
    return v;
}

static void event(SimEventKind k, int a, int b, int r) { SimEvent e = { k, a, b, r }; sim.log.push_back(e); }

// RTOS
extern "C" uint16_t getMsTimer(void) { return sim.now; }
extern "C" void dump_hex_relative(void *, int) { }
void vTaskDelay(TickType_t) { }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { static int m; return &m; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { if (sim.mutex_busy) return pdFALSE; sim.takes++; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t) { sim.gives++; return pdTRUE; }

// file manager and file device: only the block device the driver registers matters
BlockDevice *sim_registered_unit = 0;
FileDevice::FileDevice(BlockDevice *b, const char *, const char *) : CachedTreeNode(NULL, "") { sim_registered_unit = b; }
void FileDevice::attach_disk(int) { }
void FileDevice::detach_disk() { }
void FileManager::add_root_entry(CachedTreeNode *) { }
void FileManager::remove_root_entry(CachedTreeNode *) { }
void FileManager::invalidate(CachedTreeNode *) { }

// USB device
UsbDevice::UsbDevice(UsbBase *u, int s) { memset((void *)this, 0, sizeof *this); host = u; current_address = 5; strcpy(product, "SIM"); }
UsbDevice::~UsbDevice() { }
char *UsbDevice::get_pathname(char *dest, int) { strcpy(dest, "usb1"); return dest; }
void UsbDevice::set_configuration(uint8_t) { }
void UsbDevice::set_interface(uint8_t, uint8_t) { }
struct t_device_configuration *UsbDevice::get_device_config() { static t_device_configuration c; c.config_value = 1; return &c; }
// CLEAR_FEATURE(ENDPOINT_HALT) restarts the data toggle of the device endpoint
int UsbDevice::unstall_pipe(uint8_t ep, int timeout)
{
    event(e_ev_unstall, ep, timeout, 0);
    if (ep == 0x82) sim.dev_in_toggle = 0;
    if (ep == 0x02) sim.dev_out_toggle = 0;
    return 0;
}

// USB host
void UsbBase::initialize_pipe(struct t_pipe *pipe, UsbDevice *dev, struct t_endpoint_descriptor *ep)
{
    memset(pipe, 0, sizeof *pipe);
    pipe->device = dev;
    pipe->DevEP = (dev->current_address << 8) | (ep->endpoint_address & 0x0F); // the direction bit is not kept
    pipe->MaxTrans = ep->max_packet_size;
}

int UsbBase::control_exchange(struct t_pipe *, void *out, int, void *in, int inlen, int timeout)
{
    const uint8_t *s = (const uint8_t *)out;
    if (s[1] == 0xFF) { event(e_ev_msr, s[4], timeout, 0); return 0; }
    event(e_ev_control, s[1], timeout, 0);
    if (in && inlen) { memset(in, 0, inlen); return 1; }  // GET MAX LUN: one LUN
    return 0;
}

int UsbBase::bulk_out(struct t_pipe *pipe, void *buf, int len, int timeout, int retries)
{
    int r = len;
    if (!sim.out_results.empty()) { r = sim.out_results.front(); sim.out_results.pop_front(); }
    if (r >= 0) { // the packets of this transfer: the toggle of the host and the device must agree
        for (int n = 0; n < ((len + 511) / 512 ? (len + 511) / 512 : 1); n++) {
            if ((pipe->Command & URES_TOGGLE) != sim.dev_out_toggle) sim.lost++;
            pipe->Command ^= URES_TOGGLE;
            sim.dev_out_toggle ^= URES_TOGGLE;
        }
    }
    if (len == 31) {
        memcpy(sim.last_cbw, buf, 31);
        uint32_t dl; memcpy(&dl, sim.last_cbw + 8, 4);
        event(e_ev_cbw, sim.last_cbw[15], dl, r);
    } else {
        event(e_ev_out_data, len, 0, r);
    }
    return r;
}

int UsbBase::bulk_in(struct t_pipe *pipe, void *buf, int len, int timeout, int retries, bool stop_on_short)
{
    const int requested = len, mps = (len > pipe->MaxTrans) ? pipe->MaxTrans : len; // descr->maxTrans
    uint8_t *dst = (uint8_t *)buf;
    int total = 0;
    do {
        uint16_t before = pipe->Command;
        const int cur = (len > 49152) ? 49152 : len;
        int transferred = 0;
        bool packet = false, stall = false;
        while (transferred < cur) { // one nano command
            if (sim.in.empty()) break; // nothing sent: a NAK timeout
            SimPacket p = sim.in.front(); sim.in.pop_front();
            if (p.kind == e_sim_nak) { sim.now += 3000; break; }
            if (p.kind == e_sim_stall) { stall = true; break; }
            if (p.kind == e_sim_discard) { packet = true; break; } // a duplicate: stored nothing, toggle unchanged
            std::vector<uint8_t> b = p.bytes;
            if (p.kind == e_sim_csw) {
                uint32_t tag; memcpy(&tag, sim.last_cbw + 4, 4);
                b.assign(13, 0); memcpy(&b[0], "USBS", 4); memcpy(&b[4], &tag, 4); memcpy(&b[8], &p.residue, 4); b[12] = p.status;
                b.resize(p.csw_len);
            }
            if ((pipe->Command & URES_TOGGLE) != sim.dev_in_toggle) { // wrong PID: the host drops it, the device moves on
                sim.lost++; sim.dev_in_toggle ^= URES_TOGGLE; packet = true; break;
            }
            sim.dev_in_toggle ^= URES_TOGGLE;
            int n = (int)b.size();
            if (n > cur - transferred) n = cur - transferred; // the model never babbles
            memcpy(dst + total + transferred, b.data(), n);
            transferred += n;
            packet = true;
            pipe->Command ^= URES_TOGGLE;
            if (n < mps) break;
        }
        if (stall) { total = -4; break; }
        if (!packet) {
            if (retries <= 0) { total = -1; break; }
            retries--;
            continue;
        }
        total += transferred;
        len -= transferred;
        if (stop_on_short && bot_in_stop_on_short(true, before & URES_TOGGLE, pipe->Command & URES_TOGGLE, transferred, cur, mps))
            break;
    } while (len > 0);
    event(e_ev_in, requested, stop_on_short, total);
    return total;
}

FileDevice::~FileDevice() { }
bool FileDevice::is_ready(void) { return true; }
int FileDevice::probe(void) { return 0; }
void FileDevice::get_display_string(char *, int) { }
