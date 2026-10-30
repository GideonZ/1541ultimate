"""Compile the updater's unchanged function bodies against recording hardware stubs.

Only #include lines are replaced; the generated source and compiler output are
retained by the caller. This is an orchestration test, not flash-driver emulation.
"""
from pathlib import Path
import re
import subprocess
import sys

root, out = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(exist_ok=True)
source = (root / 'software/application/update_u2p/update_https_u64ii.cc').read_text()
body = re.sub(r'^#include[^\n]*$', '', source, flags=re.M)
symbols = re.findall(r'extern (?:uint32_t|uint8_t) (\w+);', body)
arrays = re.findall(r'extern const char (\w+)\[[^]]*\];', body)
stub = r'''
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include <cstdio>
#include "controller_probe.h"
#ifndef UPDATE_FPGA_TYPE
#define UPDATE_FPGA_TYPE 0
#endif
#define SILENT 0
#define IDENT_MAJOR 1
#define IDENT_MINOR 22
#define pdMS_TO_TICKS(x) (x)
#define portENTER_CRITICAL() ((void)0)
#define portEXIT_CRITICAL() ((void)0)
#define BUTTON_OK 1
#define BUTTON_YES 2
#define BUTTON_NO 4
#define ROMS_DIRECTORY "/flash/roms"
#define CARTS_DIRECTORY "/flash/carts"
static std::vector<std::string> events;
static int board=2, minor=IDENT_MINOR-1, corrupt=0, checksum_calls=0, fail_native=0, native_calls=0, fail_esp=0, esp_calls=0;
static bool identified=true, accept=true, download=true;
struct Halt {};
struct Finished {};
static unsigned stop_delays=0;
static void vTaskDelay(int t) { if (t==200 && ++stop_delays==10) throw Halt{}; }
static void diagnostic_line(unsigned, const char*) {}
struct Screen {};
static Screen *screen;
struct UserInterface {
    void update_progress(void*,int) {}
    void show_progress(const char*,uint32_t) {}
    void hide_progress() {}
    int popup(const char *msg,int) {
        events.push_back(msg);
        return accept ? BUTTON_YES : BUTTON_NO;
    }
};
static UserInterface ui;
static UserInterface *user_interface=&ui;
struct UART { void EnableIRQ(bool) {} };
struct ESP {
    UART instance; UART *uart=&instance;
    void EnableRunMode() { events.push_back("controller-start"); }
    int Download() { events.push_back("download"); return download ? 0 : -1; }
    int Flash(uint8_t*,uint32_t,uint32_t,void(*)(void*),void*) {
        events.push_back("esp-write"); return ++esp_calls == fail_esp ? -1 : 0;
    }
} esp32;
struct ControllerProbeHardware {};
static ControllerProbeResult controller_probe(ControllerProbeHardware&,ControllerIdentity &id) {
    id=ControllerIdentity{}; id.major=1; id.minor=minor;
    events.push_back("identity");
    return identified ? ControllerProbeResult::Identified : ControllerProbeResult::Malformed;
}
static void wifi_command_init() {}
struct Flash {
    const char *get_type_string() { return "stub"; }
    void protect_disable() { events.push_back("unprotect"); }
    void protect_enable() { events.push_back("protect"); }
};
static Flash flash;
static Flash *get_flash() { return &flash; }
static uint8_t getFpgaType() { return board; }
static const char *getBoardRevision() { return "test"; }
static int calc_checksum(uint8_t*, uint8_t*) { return ++checksum_calls == corrupt ? -1 : 1; }
static void console_print(Screen*,const char*,...) {}
static void setup(const char*) { events.push_back("setup"); }
static void check_flash_disk() { events.push_back("disk-check"); }
static void clear_field() {}
static void create_dir(const char*) { events.push_back("file-write"); }
static void write_flash_file(const char*,uint8_t*,int) { events.push_back("file-write"); }
static void write_html_file(const char*,const char*,int) { events.push_back("file-write"); }
static void write_api_files(const char*,const char*) { events.push_back("file-write"); }
static bool flash_buffer_at(Flash*,Screen*,int,bool,void*,void*,const char*,const char*) {
    events.push_back("native-write"); return ++native_calls != fail_native;
}
static void write_protect(Flash*,int) { events.push_back("protect"); }
static void reset_config(Flash*) { events.push_back("reset-config"); }
static void turn_off() { events.push_back("finish"); throw Finished{}; }
struct USB { void initHardware() {} } usb2;
struct I2C { void enable_scan(bool,bool) {} } i2c_instance;
static I2C *i2c=&i2c_instance;
'''
stub += '\n'.join('#define '+x+' 1' for x in sorted(set(re.findall(r'CHK_\w+',body)))) + '\n'
stub += '\n'.join('uint32_t '+x+';' for x in symbols if re.search(r'extern uint32_t '+x+';',body)) + '\n'
stub += '\n'.join('uint8_t '+x+';' for x in symbols if re.search(r'extern uint8_t '+x+';',body)) + '\n'
stub += '\n'.join('extern const char '+x+'[1]={0};' for x in arrays) + '\n'
tail = r'''
static bool has(const char *s) { for(auto &e:events) if(e==s) return true; return false; }
static void reset() {
    events.clear(); stop_delays=0; board=2; minor=IDENT_MINOR-1; corrupt=checksum_calls=fail_native=native_calls=fail_esp=esp_calls=0;
    identified=accept=download=true;
}
static void run() { try { ultimate_main(0,nullptr); } catch(Halt&) {} catch(Finished&) {} }
static void blocked() {
    assert(!has("setup") && !has("disk-check") && !has("file-write") && !has("unprotect") && !has("native-write") && !has("esp-write") && !has("reset-config") && !has("finish"));
}
int main() {
    unsigned count=0;
#ifdef NO_ESP
    reset(); run(); blocked(); assert(!has("identity")); ++count;
#else
    for(int b : {0,1,4,255}) { reset(); board=b; run(); blocked(); assert(!has("identity")); ++count; }
    for(int n=1;n<=5;++n) { reset(); corrupt=n; run(); blocked(); assert(!has("identity")); ++count; }
    for(int v : {IDENT_MINOR,IDENT_MINOR+1,65534}) { reset(); minor=v; run(); blocked(); ++count; }
    reset(); identified=false; run(); blocked(); ++count;
    for(int b : {2,3}) {
        reset(); board=b; run();
        assert(native_calls==2 && esp_calls==3 && has("finish") && has("reset-config"));
        size_t identity=0, setup_index=0;
        for(size_t i=0;i<events.size();++i) { if(events[i]=="identity") identity=i; if(events[i]=="setup") setup_index=i; }
        assert(identity<setup_index); ++count;
    }
    reset(); accept=false; run(); assert(!has("file-write") && !has("native-write") && !has("esp-write") && !has("reset-config")); ++count;
    for(int n=1;n<=2;++n) {
        reset(); fail_native=n; run(); assert(native_calls==n && !has("download") && !has("reset-config") && !has("finish") && has("protect")); ++count;
    }
    for(int n=1;n<=3;++n) {
        reset(); fail_esp=n; run(); assert(esp_calls==n && !has("reset-config") && !has("finish")); ++count;
    }
    reset(); download=false; run(); assert(esp_calls==0 && !has("reset-config") && !has("finish")); ++count;
#endif
    printf("updater orchestration: %u scenarios passed\n",count);
}
'''
generated = out / 'updater-under-test.cc'
generated.write_text(stub + body + tail)
for name, flags in [('normal',[]),('no-controller',['-DNO_ESP'])]:
    binary = out / name
    command = ['g++','-std=c++11','-fpermissive','-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-I'+str(root/'software/io/wifi'),*flags,str(generated),'-o',str(binary)]
    p = subprocess.run(command,capture_output=True,text=True)
    (out/(name+'-build.log')).write_text(p.stdout+p.stderr)
    if p.returncode: print(p.stderr); raise SystemExit(p.returncode)
    p = subprocess.run([str(binary)],capture_output=True,text=True)
    (out/(name+'-test.log')).write_text(p.stdout+p.stderr)
    print(p.stdout+p.stderr)
    if p.returncode: raise SystemExit(p.returncode)
