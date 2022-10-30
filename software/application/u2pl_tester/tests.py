
from support import Tester, DeviceUnderTest, find_ones, find_zeros
import time
import math
import struct
from fft import calc_fft
import numpy as np

dut_fpga  = '../../../target/fpga/ecp5_dut/impl1/u2p_ecp5_dut_impl1.bit'
dut_appl  = '../../../target/software/riscv32_dut/result/dut.bin'
final_fpga = '../../../target/fpga/u2plus_ecp5/impl1/u2p_ecp5_impl1.bit'
final_appl = '../../../target/software/riscv32_ultimate/result/ultimate.app'
final_fat = '../../../target/software/riscv32_ultimate/fat.bin'

TEST_SEND_ETH = 5
TEST_RECV_ETH = 6
TEST_USB_PHY = 7
TEST_USB_HUB = 8
TEST_USB_PORTS = 9
TEST_BUTTONS = 10
TEST_USB_INIT = 11
TEST_RTC_ACCESS = 13
TEST_RTC_READ = 14
TEST_RTC_WRITE = 15
TEST_USB_SHOW = 17

# This startup function returns a tuple with two JtagClients.
class TestFail(Exception):
    pass

class UltimateIIPlusLatticeTests:
    def __init__(self):
        pass

    def startup(self, proto = False):
        self.tester = Tester()
        self.dut = DeviceUnderTest()    
        self.proto = proto

    def shutdown(self):
        # Turn off power
        self.tester.user_set_io(0)

    def test_000_boot_current(self):
        self.tester.user_set_io(0x30) # Turn on DUT from both 'sides'
        time.sleep(0.2) # Let power regulators come up
        self.dut.ecp_clear_fpga()
        time.sleep(0.2) # Check Power supplt after some time

        supply = self.tester.read_adc_channel('Supply', 5)
        if supply < 5.0:
            TestFail('Tester Supply Voltage Low')
        self.supply = supply # For later use

        curr = self.tester.read_adc_channel('Cartridge Current', 5) + self.tester.read_adc_channel('MicroUSB Current', 5)
        print(f"After CLEAR FPGA: {curr:.1f} mA")
        if curr > 250.:   # Unfortunately this is including USB sticks and network loopback attached!
            TestFail('Board Current too high')
        self.current = curr # For later use

    def test_001_regulators(self):
        v50 = self.tester.read_adc_channel('+5.0V', 5)
        v43 = self.tester.read_adc_channel('+4.3V', 5)
        v33 = self.tester.read_adc_channel('+3.3V', 5)
        v25 = self.tester.read_adc_channel('+2.5V', 5)
        v18 = self.tester.read_adc_channel('+1.8V', 5)
        v09 = self.tester.read_adc_channel('+0.9V', 5)

        ok = True
        if not (0.855 <= v09 <= 0.945) and not self.proto:
            ok = False
        if not (1.71 <= v18 <= 1.89):
            ok = False
        if not (2.375 <= v25 <= 2.625):
            ok = False
        if not (3.135 <= v33 <= 3.465) and not self.proto:
            ok = False
        if not (4.085 <= v43 <= 4.515):
            ok = False
        if not (4.5 <= v50 <= self.supply) and not self.proto:
            ok = False

        if not ok:
            self.tester.report_adcs()
            raise TestFail('One or more regulator voltages out of range')

    def test_002_power_switchover(self):
        # Switch to MicroUSB supply mode
        self.tester.user_set_io(0x20)
        v18 = self.tester.read_adc_channel('+1.8V', 5)
        if v18 < 1.7:
            raise TestFail('Power went off when switching to MicroUSB power')
        curr = self.tester.read_adc_channel('Cartridge Current', 5)
        if curr > 5.0:
            raise TestFail('Current flow from Cartridge Supply')
        self.tester.user_set_io(0x30)
        # Switch to Cartridge Power Mode
        self.tester.user_set_io(0x10)
        v18 = self.tester.read_adc_channel('+1.8V', 5)
        if v18 < 1.7:
            raise TestFail('Power went off when switching to Cartridge power')
        curr = self.tester.read_adc_channel('MicroUSB Current', 5)
        if curr > 5.0:
            raise TestFail('Current flow from MicroUSB Supply')
        self.tester.user_set_io(0x30)

    def test_003_test_fpga(self):
        if self.dut.ecp_read_id() != 0x41111043:
            raise TestFail("FPGA on DUT not recognized")

        self.dut.ecp_load_fpga(dut_fpga)

        if self.dut.user_read_id() != 0xdead1541:
            raise TestFail("DUT: User JTAG not working. (bad ID)")
        
    def test_004_ddr2_memory(self):
        # bootloader should have run by now
        time.sleep(0.5)
        text = self.dut.user_read_console(True)
        if "RAM OK!!" not in text:
            raise TestFail("Memory calibration failed.")

        random = {} # Map of random byte blocks
        for i in range(6,26):
            random[i] = np.random.bytes(64)

        for i in range(6,26):
            addr = 1 << i
            print(f"Writing Addr: {addr:x}")
            self.dut.user_write_memory(addr, random[i])

        for i in range(6,26):
            addr = 1 << i
            print(f"Reading Addr: {addr:x}")
            rb = self.dut.user_read_memory(addr, 64)
            if rb != random[i]:
                print (random[i])
                print (rb)
                raise TestFail('Verify error on DDR2 memory')

    def test_005_start_app(self):
        self.dut.user_upload(dut_appl, 0x100)
        self.dut.user_run_app(0x100)
        time.sleep(0.5)
        text = self.dut.user_read_console()
        if "DUT Main" not in text:
            raise TestFail('Running test application failed')

    def test_006_buttons(self):
        print("Press each button!")
        (result, console) = self.dut.perform_test(TEST_BUTTONS, max_time = 60)
        print(f"Console Output:\n{console}")
        if result != 0:
            raise TestFail(f'Fault in buttons. Err = {result}')

    def test_007_ethernet(self):
        (result, console) = self.dut.perform_test(TEST_SEND_ETH)
        print(f"Console Output:\n{console}")
        if result != 0:
            raise TestFail(f"Couldn't send Ethernet Packet. Err = {result}")
        (result, console) = self.dut.perform_test(TEST_RECV_ETH)
        print(f"Console Output:\n{console}")
        if result != 0:
            raise TestFail(f"Didn't receive Ethernet Packet. Err = {result}")

    def test_008_usb_phy(self):
        (result, console) = self.dut.perform_test(TEST_USB_PHY)
        print(f"Console Output:\n{console}")
        if result != 0:
            raise TestFail(f"Couldn't find USB PHY (USB3310) Err = {result}")

    def test_009_usb_hub(self):
        (result, console) = self.dut.perform_test(TEST_USB_INIT)
        print(f"Console Output:\n{console}")
        time.sleep(1)
        (result, console) = self.dut.perform_test(TEST_USB_HUB)
        print(f"Console Output:\n{console}")
        if result != 0:
            raise TestFail(f"Couldn't find USB HUB (USB2503) Err = {result}")

    def test_010_usb_sticks(self):
        time.sleep(3)
        (result, console) = self.dut.perform_test(TEST_USB_PORTS)
        print(f"Console Output:\n{console}")
        if result != 0:
            raise TestFail(f"Couldn't find (all) USB sticks Err = {result}")
        (_result, console) = self.dut.perform_test(TEST_USB_SHOW)
        print(f"Console Output:\n{console}")

    def test_011_rtc(self):
        vbatt = self.tester.read_adc_channel('VBatt', 4)
        if vbatt < 2.8:
            raise TestFail(f"Real Time Clock fail! Vbatt Low! {vbatt:.2f}V")

        (result, console) = self.dut.perform_test(TEST_RTC_ACCESS)
        print(f"Console Output:\n{console}")
        if result != 0:
            raise TestFail(f"Couldn't access Real Time Clock. Err = {result}")

        self.time_since_battery = self.dut.get_seconds_after_epoch()

        # Write current time in RTC
        self.dut.write_current_time()

    def test_012_audio(self):
        print("Generating sine wave")
        scale = math.pow(2.0, 31) * 0.9
        data = b''
        for i in range(192):
            phase = i * (2. * math.pi / 48.) # 1000 Hz
            sampleL = scale * math.sin(phase)
            phase = i * (2. * math.pi / 64.) # 750 Hz
            sampleR = scale * math.sin(phase)
            data += struct.pack("<ll", int(sampleL), int(sampleR))

        ## data should now have 192 samples
        print("Uploading sound.")
        self.dut.user_write_memory(0x1100000, data)

        # Now let's enable this sound on the output
        regs = struct.pack("<LLB", 0x1100000, 0x1100000 + 192*8, 3)
        self.dut.user_write_io(0x100210, regs)

        # Let's now also start recording (once ~4000 samples, which takes < 0.1 second to do
        regs = struct.pack("<LLB", 0x1000000, 0x1000000 + 4608*8, 1)
        self.dut.user_write_io(0x100200, regs)

        # Now download the data (assuming we will never be faster than writing the data)
        print("Downloading audio data...")
        data = self.dut.user_read_memory(0x1000000, 4608 * 2 * 4)
        with open("audio.bin", 'wb') as fo:
            fo.write(data)

        (left_ampl, left_peak), (right_ampl, right_peak) = calc_fft("audio.bin", False)
        if left_ampl < 0.36:
            raise TestFail("Amplitude on left audio channel low.")
        if left_peak != 750.0:
            raise TestFail("Peak in spectrum not at 750 Hz")
        if right_ampl < 0.36:
            raise TestFail("Amplitude on right audio channel low.")
        if right_peak != 1000.0:
            raise TestFail("Peak in spectrum not at 1000 Hz")

    def test_012_iec(self):
        # IEC is only available on the dut.
        # Can only toggle each output and observe the input
        # While the pins are active low, the signals in the
        # FPGA are active high.
        patterns = b'\x00\x01\x02\x04\x08\x10\x1F\x1E\x1D\x1B\x17\x0F'
        errors = 0
        for i in patterns:
            wr = bytearray(1)
            wr[0] = i
            self.dut.user_write_io(0x100306, wr)
            rb = self.dut.user_read_io(0x100306, 1)
            # print("{0:02x} {1:02x}".format(i, rb[0]))
            if i != rb[0]:
                errors += 1

        # TODO: Report more accurately *which signal* is stuck at 0, stuck at 1, or shorted to another
        if errors > 0:
            raise TestFail("IEC local loopback failure.")

    def test_013_cartio_in(self):
        # Set everything to input on dut
        # Bit 47 has to be set to 1, because that's BUFFER_EN
        zeros = bytearray(8)
        buf_en = bytearray(8)
        buf_en[5] = 0x80
        self.dut.user_write_io(0x100308, zeros)
        self.dut.user_write_io(0x100300, buf_en)
        print("Setting bits: ", find_ones(buf_en))
        rb = self.dut.user_read_io(0x100300, 8)
        print("Read: ", find_ones(rb))

        # Set everything to output on tester
        ones = b'\xff' * 8
        self.tester.user_write_io(0x100308, ones)

        slot_bits = [x for x in range(38)]
        cassette_bits = [x for x in range(40, 44)]
        test_bits = slot_bits + cassette_bits
        errors = 0

        # Walking one test
        for i in test_bits:
            pattern = bytearray(6)
            pattern[i//8] |= 1 << (i %8)
            print("Writing: ", pattern.hex())
            self.tester.user_write_io(0x100300, pattern)
            trb = self.tester.user_read_io(0x100300, 6)
            #print("Tester:  ", trb.hex(), find_ones(trb))
            if (trb != pattern):
                print(f"BIT {i} is stuck hard!")
            rb = bytearray(self.dut.user_read_io(0x100300, 6))
            # Turn off bit 39 (VCC)
            rb[4] &= 0x7F
            print("Read:    ", rb.hex(), find_ones(rb))
            if (rb != pattern):
                errors += 1

        # Walking zero test
        for i in test_bits:
            pattern = bytearray(b'\xFF\xFF\xFF\xFF\xFF\xFF')
            pattern[i//8] &= ~(1 << (i %8))
            print("Writing: ", pattern.hex())
            self.tester.user_write_io(0x100300, pattern)
            trb = bytearray(self.tester.user_read_io(0x100300, 6))
            trb[4] |= 0xC0 #0x3F
            trb[5] |= 0xF0 #0x0F
            print("Tester:  ", trb.hex(), find_zeros(trb))
            if (trb != pattern):
                print(f"BIT {i} is stuck hard!")
            rb = bytearray(self.dut.user_read_io(0x100300, 6))
            rb[4] |= 0xC0 #0x3F
            rb[5] |= 0xF0 #0x0F
            print("Read:    ", rb.hex(), find_zeros(rb))
            if (rb != pattern):
                errors += 1

        print(f"Errors: {errors}")
        if errors > 0:
            raise TestFail("Cartridge / Cassette I/O failure.")

    def test_014_cartio_out(self):
        # Set everything to input on tester
        zeros = bytearray(8)
        self.tester.user_write_io(0x100308, zeros)

        # Set everything to output on dut
        ones = b'\xff' * 8
        buf_en = bytearray(8)
        buf_en[5] = 0x80
        self.dut.user_write_io(0x100300, buf_en)
        self.dut.user_write_io(0x100308, ones)

        slot_bits = [x for x in range(24)]
        slot_bits += [x for x in range(25, 38)]
        cassette_bits = [x for x in range(40, 44)]
        test_bits = slot_bits + cassette_bits
        errors = 0

        # Walking one test; from dut to tester
        for i in test_bits:
            pattern = bytearray(6)
            pattern[5] |= 0x80 # Buffer enable
            pattern[i//8] |= 1 << (i %8)
            print("Writing: ", pattern.hex())
            self.dut.user_write_io(0x100300, pattern)
            pattern[5] &= 0x7F # Remove buffer enable again

            trb = bytearray(self.dut.user_read_io(0x100300, 6))
            trb[4] &= 0x7F  # VCC mask
            print("DUT RB:  ", trb.hex(), find_ones(trb))
            if (trb != pattern):
                print(f"BIT {i} is stuck hard!")
            rb = bytearray(self.tester.user_read_io(0x100300, 6))
            
            print("Read:    ", rb.hex(), find_ones(rb))
            if (rb != pattern):
                errors += 1

        print(f"Errors: {errors}")
        if errors > 0:
            raise TestFail("Cartridge / Cassette I/O failure.")

    def test_015_leds(self):
        self.dut.user_set_io(0x80)  # Put local CPU in reset
        self.tester.user_set_io(0x10) # Turn on DUT power only through Cartridge
        time.sleep(0.2)
        led0 = self.tester.read_adc_channel('Cartridge Current', 10)
        self.dut.user_set_io(0x81)
        led1 = self.tester.read_adc_channel('Cartridge Current', 10)
        self.dut.user_set_io(0x83)
        led2 = self.tester.read_adc_channel('Cartridge Current', 10)
        self.dut.user_set_io(0x87)
        led3 = self.tester.read_adc_channel('Cartridge Current', 10)
        self.dut.user_set_io(0x8F)
        led4 = self.tester.read_adc_channel('Cartridge Current', 10)
        if (led4 > led3) and (led3 > led2) and (led2 > led1) and (led1 > led0):
            print("LEDs OK!")
        else:
            raise TestFail("LEDs not properly detected.")

    def test_016_speaker(self):
        pass

    def test_017_program(self):
        # Program the flash in three steps: 1) FPGA, 2) Application, 3) FAT Filesystem
        #self.dut.ecp_prog_flash(final_fpga, 0)
        #self.dut.ecp_prog_flash(final_appl, 0xA0000)
        self.dut.ecp_prog_flash(final_fat, 0x200000)

if __name__ == '__main__':
    tests = UltimateIIPlusLatticeTests()
    tests.startup(proto = True)
    tests.test_000_boot_current()
    tests.test_001_regulators()
    tests.test_002_power_switchover()
    tests.test_003_test_fpga()
    tests.test_004_ddr2_memory()
    tests.test_005_start_app()
    #tests.test_006_buttons()
    #tests.test_007_ethernet()
    #tests.test_008_usb_phy()
    #tests.test_009_usb_hub()
    #tests.test_010_usb_sticks()
    #tests.test_011_rtc()
    #tests.test_012_audio()
    #tests.test_013_cartio_in()
    #tests.test_014_cartio_out()
    #tests.test_015_leds()
    tests.test_016_speaker()
    tests.test_017_program()
    tests.shutdown()
