#include "u2_memory_backend.h"

#include "c64.h"

uint8_t U2MemoryBackend :: read(uint16_t address)
{
    if (!machine || !machine->exists()) {
        return 0;
    }
    return machine->peek(address);
}

void U2MemoryBackend :: write(uint16_t address, uint8_t value)
{
    if (!machine || !machine->exists()) {
        return;
    }
    if (machine->is_accessible()) {
        machine->dma_transfer_frozen(address, &value, 1, 0);
        return;
    }
    machine->poke(address, value);
}

void U2MemoryBackend :: read_block(uint16_t address, uint8_t *dst, uint16_t len)
{
    if (!machine || !machine->exists()) {
        while (len) {
            *dst++ = 0;
            len--;
        }
        return;
    }
    bool stopped_it = machine->begin_stopped_session();
    while (len) {
        *dst++ = machine->peek(address++);
        len--;
    }
    machine->end_stopped_session(stopped_it);
}

void U2MemoryBackend :: write_block(uint16_t address, const uint8_t *src, uint16_t len)
{
    if (!machine || !machine->exists()) {
        return;
    }
    if (machine->is_accessible()) {
        machine->dma_transfer_frozen(address, const_cast<uint8_t *>(src), len, 0);
        return;
    }
    bool stopped_it = machine->begin_stopped_session();
    while (len) {
        machine->poke(address++, *src++);
        len--;
    }
    machine->end_stopped_session(stopped_it);
}

void U2MemoryBackend :: begin_session(void)
{
    if (!machine || !machine->exists() || machine->is_accessible()) {
        return;
    }
    bool stopped_it = machine->begin_stopped_session();
    sampled_cia2_porta = machine->peek(0xDD00);
    machine->end_stopped_session(stopped_it);
}

uint8_t U2MemoryBackend :: read_cia2_porta(void)
{
    if (!machine || !machine->exists()) {
        return 0x03;
    }
    if (machine->is_accessible()) {
        sampled_cia2_porta = machine->get_frozen_cia2_porta();
    } else if (machine->is_stopped()) {
        sampled_cia2_porta = machine->peek(0xDD00);
    }
    return sampled_cia2_porta;
}

uint8_t U2MemoryBackend :: get_live_vic_bank(void)
{
    return (uint8_t)(3 - (read_cia2_porta() & 0x03));
}

void U2MemoryBackend :: set_live_vic_bank(uint8_t vic_bank)
{
    if (!machine || !machine->exists()) {
        return;
    }
    if (machine->is_accessible()) {
        uint8_t porta = read_cia2_porta();
        porta = (uint8_t)((porta & 0xFC) | (uint8_t)(3 - (vic_bank & 0x03)));
        machine->set_frozen_cia2_porta(porta);
        sampled_cia2_porta = porta;
        return;
    }
    uint8_t porta;
    if (machine->is_stopped()) {
        porta = read_cia2_porta();
    } else {
        bool stopped_it = machine->begin_stopped_session();
        porta = machine->peek(0xDD00);
        machine->end_stopped_session(stopped_it);
    }
    porta = (uint8_t)((porta & 0xFC) | (uint8_t)(3 - (vic_bank & 0x03)));
    machine->poke(0xDD00, porta);
    sampled_cia2_porta = porta;
}

const char *U2MemoryBackend :: source_name(uint16_t) const
{
    // U2 reads the current CPU-visible aperture directly. Without ROM shadow
    // snapshots or monitor-selected banking, this is not guaranteed to be RAM.
    return "CPU";
}
