#ifndef SCSI_SIM_H
#define SCSI_SIM_H

#include <stdint.h>
#include <deque>
#include <vector>
#include <string>

// A scripted stand-in for the USB host layer under the production usb_scsi.cc (see usb_scsi_exec_test.cpp).
// The test queues what the device does on the Bulk IN pipe, packet by packet; the fake UsbBase::bulk_in plays
// it back the way one nano command does (usb_base.cc: packets until one is not full), and the log records what
// the driver did, in order.

enum SimKind { e_sim_data, e_sim_csw, e_sim_nak, e_sim_stall, e_sim_discard };

struct SimPacket
{
    SimKind kind;
    std::vector<uint8_t> bytes; // e_sim_data: the payload (empty is a zero length packet)
    uint8_t status;             // e_sim_csw
    uint32_t residue;           // e_sim_csw
    int csw_len;                // e_sim_csw: bytes of the 13 that arrive
};

enum SimEventKind { e_ev_cbw, e_ev_in, e_ev_out_data, e_ev_msr, e_ev_unstall, e_ev_control };

struct SimEvent
{
    SimEventKind kind;
    int a;  // cbw: opcode; in: requested length; out_data: length; unstall: endpoint address; msr: interface
    int b;  // in: stop_on_short; cbw: data length
    int r;  // in/out: what was returned
};

struct Sim
{
    std::deque<SimPacket> in;     // what the device sends on Bulk IN
    std::deque<int> out_results;  // result of the next bulk_out calls (default: the length)
    std::vector<SimEvent> log;
    uint8_t last_cbw[31];
    uint16_t now;                 // getMsTimer
    bool mutex_busy;              // xSemaphoreTake times out
    int takes, gives;
    int mps;                      // Bulk IN max packet size
    uint16_t dev_in_toggle, dev_out_toggle; // the PID the device sends / expects next (URES_TOGGLE or 0)
    int lost;                     // packets that carried the wrong toggle: the receiver drops them

    void reset();
    // building the device side
    void data(int n, uint8_t fill = 0x5A);          // one packet of n bytes (n == 0: a zero length packet)
    void stream(int n, uint8_t fill = 0x5A);        // n bytes as full packets and one short one
    void zlp()                           { data(0); }
    void csw(uint8_t status = 0, uint32_t residue = 0, int len = 13);
    void nak()                           { SimPacket p; p.kind = e_sim_nak; in.push_back(p); }
    void stall()                         { SimPacket p; p.kind = e_sim_stall; in.push_back(p); }
    void discard()                       { SimPacket p; p.kind = e_sim_discard; in.push_back(p); }
    // reading the log
    int count(SimEventKind k) const;
    std::vector<int> unstalls() const;   // endpoint addresses, in order
    std::vector<int> recovery_steps() const; // 0 = reset, else the endpoint address cleared, in order
};

extern Sim sim;

#endif
