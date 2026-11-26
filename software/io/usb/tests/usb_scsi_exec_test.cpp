// Executes the production usb_scsi.cc (the Bulk-Only status, retry, Reset Recovery and read-only paths) against
// a scripted USB host layer (scsi_sim.cpp). What a device does is queued packet by packet; the assertions look at
// what the driver returned and at the order of what it sent.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scsi_sim.h"
#include "usb_scsi.h"

extern BlockDevice *sim_registered_unit;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

struct Rig { UsbDevice *dev; UsbInterface *intf; UsbScsiDriver *drv; UsbScsi *unit; };

// A driver installed on a device with a bulk IN endpoint 2 (0x82) and a bulk OUT endpoint 2, one LUN, ready.
static Rig make_rig()
{
    static UsbBase *host = (UsbBase *)calloc(1, sizeof(UsbBase));
    sim.reset();
    Rig r;
    r.dev = new UsbDevice(host, 2);
    t_interface_descriptor d;
    memset(&d, 0, sizeof d);
    d.interface_class = 8; d.protocol = 0x50; d.interface_number = 3;
    r.intf = new UsbInterface(r.dev, 3, &d);
    const uint8_t in_ep[7]  = { 7, 5, 0x82, 2, 0x00, 0x02, 0 };
    const uint8_t out_ep[7] = { 7, 5, 0x02, 2, 0x00, 0x02, 0 };
    r.intf->addEndpoint((uint8_t *)in_ep, 7);
    r.intf->addEndpoint((uint8_t *)out_ep, 7);
    sim.data(36, 0x20); sim.csw();   // INQUIRY
    sim.data(18, 0);    sim.csw();   // REQUEST SENSE: no sense, ready
    r.drv = (UsbScsiDriver *)UsbScsiDriver::test_driver(r.intf);
    r.drv->install(r.intf);
    r.unit = (UsbScsi *)sim_registered_unit;
    sim.log.clear(); sim.in.clear();
    sim.takes = sim.gives = 0;
    return r;
}

static uint8_t buf[16384];

static int cbws(uint8_t opcode)
{
    int n = 0;
    for (size_t i = 0; i < sim.log.size(); i++) n += (sim.log[i].kind == e_ev_cbw) && (sim.log[i].a == opcode);
    return n;
}

static bool is_steps(const std::vector<int> &v, int a, int b, int c)
{
    return v.size() == 3 && v[0] == a && v[1] == b && v[2] == c;
}

static void test_normal_read_and_write()
{
    Rig r = make_rig();
    sim.stream(8192); sim.csw();
    CHECK(r.unit->read(buf, 0, 16) == RES_OK);
    CHECK(cbws(0x28) == 1);
    CHECK(sim.count(e_ev_msr) == 0);
    CHECK(!r.drv->is_read_only());
    CHECK(sim.takes == sim.gives);
    sim.csw();
    CHECK(r.unit->write(buf, 0, 1) == RES_OK);
    CHECK(cbws(0x2A) == 1);
    CHECK(sim.count(e_ev_msr) == 0);
}

// An invalid status: Reset Recovery in the order of the specification, one more attempt, then read-only.
static void test_reset_recovery()
{
    { Rig r = make_rig();
      sim.stream(1024); sim.csw(0, 0, 1);    // a 1 byte CSW: three packets, the toggles are odd when the recovery starts
      sim.stream(1024); sim.csw();           // the second attempt
      CHECK(r.unit->read(buf, 0, 2) == RES_OK);
      CHECK(sim.lost == 0);                  // both data toggles restarted
      CHECK(sim.count(e_ev_msr) == 1);
      CHECK(is_steps(sim.recovery_steps(), 0, 0x82, 0x02));   // reset, clear IN (direction bit set), clear OUT
      CHECK(cbws(0x28) == 2);
      CHECK(r.drv->is_read_only());
      CHECK(r.unit->status() == STA_PROTECT);
      CHECK(r.unit->get_state() == e_device_ready);
      CHECK(sim.takes == sim.gives);
      // after the recovery the unit refuses a write before it touches the device
      sim.log.clear(); sim.csw();
      CHECK(r.unit->write(buf, 0, 1) == RES_WRPRT);
      CHECK(sim.log.empty());
      CHECK(r.unit->get_state() == e_device_ready);
      uint8_t cmd[10] = { 0x2A };
      CHECK(r.drv->exec_command(0, 10, true, cmd, 512, buf) == -8);   // a write that waited on the mutex
      CHECK(sim.log.empty());
      CHECK(sim.takes == sim.gives);
      // reads still work
      sim.stream(512); sim.csw();
      CHECK(r.unit->read(buf, 0, 1) == RES_OK);
      CHECK(sim.lost == 0);
      CHECK(sim.count(e_ev_msr) == 0); }   // the log was cleared above: no second recovery
    { Rig r = make_rig();   // the retry fails as well: one recovery only, the unit fails
      sim.stream(512); sim.csw(0, 0, 1);
      sim.stream(512); sim.csw(0, 0, 1);
      CHECK(r.unit->read(buf, 0, 1) == RES_ERROR);
      CHECK(sim.count(e_ev_msr) == 1);
      CHECK(cbws(0x28) == 2);
      CHECK(r.unit->get_state() == e_device_failed); }
    { Rig r = make_rig();   // a write is never reset or retried
      sim.csw(0, 0, 1);
      CHECK(r.unit->write(buf, 0, 1) == RES_ERROR);
      CHECK(sim.count(e_ev_msr) == 0);
      CHECK(!r.drv->is_read_only());
      CHECK(r.unit->get_state() == e_device_failed); }
    { Rig r = make_rig();   // a fault that came after a NAK timeout: no recovery, the same wait as before
      sim.stream(512); sim.nak(); sim.csw(0, 0, 1);
      CHECK(r.unit->read(buf, 0, 1) == RES_ERROR);
      CHECK(sim.count(e_ev_msr) == 0);
      CHECK(r.unit->get_state() == e_device_failed); }
}

// A CSW that arrives in place of the missing READ data
static void test_csw_in_data_phase()
{
    { Rig r = make_rig();   // status 0, no residue, data missing: the command is issued again, no recovery
      sim.data(512); sim.csw();
      sim.stream(8192); sim.csw();
      CHECK(r.unit->read(buf, 0, 16) == RES_OK);
      CHECK(cbws(0x28) == 2);
      CHECK(sim.count(e_ev_msr) == 0); }
    { Rig r = make_rig();   // the device does it again: the status is invalid, recovery
      sim.data(512); sim.csw();
      sim.data(512); sim.csw();
      sim.stream(8192); sim.csw();
      CHECK(r.unit->read(buf, 0, 16) == RES_OK);
      CHECK(sim.count(e_ev_msr) == 1); }
    { Rig r = make_rig();   // status 1 in place of data: the status of the command (request sense follows)
      sim.data(512); sim.csw(1, 7680);
      sim.data(18, 0x70); sim.csw();
      CHECK(r.unit->read(buf, 0, 16) == RES_ERROR);
      CHECK(cbws(0x03) == 1);
      CHECK(sim.count(e_ev_msr) == 0); }
}

// The stall paths
static void test_stalls()
{
    { Rig r = make_rig();   // stall in the IN data phase: the IN endpoint address, with bit 7
      sim.stall();
      CHECK(r.unit->read(buf, 0, 1) == RES_ERROR);
      std::vector<int> u = sim.unstalls();
      CHECK(u.size() == 1 && u[0] == 0x82);
      CHECK(sim.takes == sim.gives); }
    { Rig r = make_rig();   // stall in the OUT data phase
      sim.out_results.push_back(31); sim.out_results.push_back(-4);
      CHECK(r.unit->write(buf, 0, 1) == RES_ERROR);
      std::vector<int> u = sim.unstalls();
      CHECK(u.size() == 1 && u[0] == 0x02); }
    { Rig r = make_rig();   // stall in the command phase
      sim.out_results.push_back(-4);
      CHECK(r.unit->write(buf, 0, 1) == RES_ERROR);
      std::vector<int> u = sim.unstalls();
      CHECK(u.size() == 1 && u[0] == 0x02); }
}

// TEST UNIT READY while another command holds the driver: busy, not failed
static void test_unit_ready()
{
    { Rig r = make_rig();
      sim.mutex_busy = true;
      CHECK(!r.unit->test_unit_ready());
      CHECK(r.unit->get_state() == e_device_ready);
      CHECK(sim.log.empty()); }
    { Rig r = make_rig();
      sim.csw();
      CHECK(r.unit->test_unit_ready());
      CHECK(r.unit->get_state() == e_device_ready); }
    { Rig r = make_rig();   // a failure without an answer still fails the unit
      CHECK(!r.unit->test_unit_ready());
      CHECK(r.unit->get_state() == e_device_failed); }
    { Rig r = make_rig();   // an invalid status: recovery, one more attempt, ready
      sim.csw(0, 0, 1); sim.csw();
      CHECK(r.unit->test_unit_ready());
      CHECK(sim.count(e_ev_msr) == 1);
      CHECK(cbws(0x00) == 2); }
}

// READ CAPACITY
static void test_read_capacity()
{
    const uint32_t sentinel = 0xDEADBEEF;
    uint32_t blocks = sentinel, bs = sentinel;
    { Rig r = make_rig();
      sim.data(8, 0); sim.csw();
      CHECK(r.unit->read_capacity(&blocks, &bs) == RES_OK);
      CHECK(bs == 0 && blocks == 1); }
}

// INQUIRY: a reply that is not 36 bytes is a failure, and an INQUIRY is never recovered
static void test_inquiry()
{
    { Rig r = make_rig();
      uint8_t cmd[6] = { 0x12, 0, 0, 0, 36, 0 };
      sim.csw();   // a CSW in place of the data, twice (the second time it is the status)
      sim.csw();
      CHECK(r.drv->exec_command(0, 6, false, cmd, 36, buf) < 0);
      CHECK(sim.count(e_ev_msr) == 0);
      CHECK(!r.drv->is_read_only());
      CHECK(sim.takes == sim.gives); }
    { Rig r = make_rig();
      uint8_t cmd[6] = { 0x12, 0, 0, 0, 36, 0 };
      sim.data(36, 0x41); sim.csw();
      CHECK(r.drv->exec_command(0, 6, false, cmd, 36, buf) == 36); }
}

int main()
{
    test_normal_read_and_write();
    test_reset_recovery();
    test_csw_in_data_phase();
    test_stalls();
    test_unit_ready();
    test_read_capacity();
    test_inquiry();
    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("usb_scsi_exec_test OK\n");
    return 0;
}
