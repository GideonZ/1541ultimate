import time, struct, logging
from datetime import datetime
from jtag_functions import JtagClient, JtagClientException

tester_fpga = '../../../target/fpga/ecp5_tester/impl1/ecp5_tester_impl1.bit'
tester_app  = '../../../target/software/riscv32_tester/result/tester.bin'

DUT_TO_TESTER   = 0x0094
TESTER_TO_DUT   = 0x0098
TEST_STATUS     = 0x009C
RTC_DATA        = 0x00B0 # b0-bf
ADC_DATA        = 0x00C0

class TestFail(Exception):
    pass

class TestFailCritical(TestFail):
    pass

# create logger
logger = logging.getLogger('Support')
logger.setLevel(logging.INFO)
ch = logging.StreamHandler()
ch.setFormatter(logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s'))
logger.addHandler(ch)

class TesterADC:
    factors = [125, 2, 1, 1, 1, 2, 1, 1, 1, 125, 2, 4.03]
    units = ['mA', 'V', 'V', 'V', 'V', 'V', 'V', 'V', 'V', 'mA', 'V', 'V']
    names = ['Cartridge Current', '+4.3V', '+1.8V', '+2.5V', 'VBatt', '+5.0V', '+0.9V', '+1.1V', '+3.3V', 'MicroUSB Current', 'USB Vout', 'Supply']
    last_count = None

    @staticmethod
    def report_adcs(client):
        mem1 = client.user_read_memory(ADC_DATA, 28)

        ch = [0] * 12
        (ch[0], ch[1], ch[2], ch[3], ch[4], ch[5], ch[6], ch[7], ch[8], ch[9], ch[10], ch[11], cnt, _) = struct.unpack("<HHHHHHHHHHHHHH", mem1)
        for i, val in enumerate(ch):
            logger.info("{0:18s}{1:.3f}{2:s}".format(TesterADC.names[i], val * 0.004 * TesterADC.factors[i], TesterADC.units[i]))
#        print(f"Count: {cnt}")

    @staticmethod
    def read_adc_channel(client, idx, repeat):
        if isinstance(idx, str):
            idx = TesterADC.names.index(idx)
        total = 0.0
        for i in range(repeat):
            mem1 = client.user_read_memory(ADC_DATA, 28)
            ch = [0] * 12
            (ch[0], ch[1], ch[2], ch[3], ch[4], ch[5], ch[6], ch[7], ch[8], ch[9], ch[10], ch[11], cnt, _) = struct.unpack("<HHHHHHHHHHHHHH", mem1)
            total += ch[idx]

        if TesterADC.last_count == cnt:
            raise JtagClientException("ADC program seems to be stuck.")

        TesterADC.last_count = cnt
        return (total * 0.004 * TesterADC.factors[idx]) / repeat

    @staticmethod
    def add_log_handler(ch):
        global logger
        logger.addHandler(ch)

class Rtc:
    @staticmethod
    def bin2bcd(val):
        if val < 0:
            return 0
        if val > 99:
            return 99
        return ((val // 10) * 16) + val % 10

    @staticmethod
    def bcd2bin(val):
        return (val // 16) * 10 + val % 16

    @staticmethod
    def from_current_time():
        now = datetime.now()
        rtc = bytearray(12)
        rtc[0] = 0x21
        rtc[1] = 0x07
        rtc[4] = Rtc.bin2bcd(now.second)
        rtc[5] = Rtc.bin2bcd(now.minute)
        rtc[6] = Rtc.bin2bcd(now.hour)
        rtc[7] = Rtc.bin2bcd(now.day)
        rtc[8] = (now.weekday() + 1) % 7
        rtc[9] = Rtc.bin2bcd(now.month)
        rtc[10] = Rtc.bin2bcd(now.year - 1980)
        return rtc

    @staticmethod
    def from_rtc_bytes(rtc):
        sec = Rtc.bcd2bin(rtc[4]) % 60
        min = Rtc.bcd2bin(rtc[5]) % 60
        hr  = Rtc.bcd2bin(rtc[6]) % 24
        day = (Rtc.bcd2bin(rtc[7])-1) % 31
        mon = (Rtc.bcd2bin(rtc[9])-1) % 12
        yr  = Rtc.bcd2bin(rtc[10]) + 1980
        dt = datetime(yr, mon+1, day+1, hr, min, sec)
        return dt

class Tester(JtagClient):
    def __init__(self):
        JtagClient.__init__(self, 'localhost', 5000)
        self.check_daemon()
        if self.ecp_read_id() != 0x41111043:
            raise JtagClientException("ColorLight i5 FPGA module not recognized")
        self.ecp_load_fpga(tester_fpga)
        if self.user_read_id() != 0xdead1541:
            raise JtagClientException("Tester User JTAG not working. (bad ID)")
        self.turn_off_dut()
        time.sleep(0.2)
        text = self.user_read_console()
        if 'Tester Module' not in text:
            raise JtagClientException("Tester BootROM failure")
        #self.turn_off_dut()
        self.run_i2c_app()

    def run_i2c_app(self):
        self.user_upload(tester_app, 0x100)
        self.user_run_app(0x100)
        time.sleep(0.2)
        text = self.user_read_console()
        if 'Hello I2C' not in text:
            raise JtagClientException("Tester Application failure")

    def turn_off_dut(self):
        self.user_set_io(0x00)

    def read_adc_channel(self, idx, repeat = 1):
        return TesterADC.read_adc_channel(self, idx, repeat)

    def report_adcs(self):
        TesterADC.report_adcs(self)

#    def user_read_io(self, *args, **kwargs):
#        ret = JtagClient.user_read_io(self, *args, **kwargs)
#        mem = self.user_read_memory(ADC_DATA + 24, 4)
#        logger.info(f'-R-> {struct.unpack("<L", mem)}')
#        logger.info(f'=R=> {self.user_read_debug():08x}')
#        return ret
#
#    def user_write_io(self, *args, **kwargs):
#        JtagClient.user_write_io(self, *args, **kwargs)
#        mem = self.user_read_memory(ADC_DATA + 24, 4)
#        logger.info(f'-W-> {struct.unpack("<L", mem)}')
#        logger.info(f'=W=> {self.user_read_debug():08x}')

class DeviceUnderTest(JtagClient):
    def __init__(self):
        JtagClient.__init__(self, 'localhost', 6000)
        self.check_daemon()

    def perform_test(self, test_id, max_time = 10):
        self.user_write_int32(TESTER_TO_DUT, test_id)
        while self.user_read_int32(TESTER_TO_DUT) == test_id and max_time > 0:
            time.sleep(.2)
            max_time -= 1
        if self.user_read_int32(TESTER_TO_DUT) == test_id:
            raise JtagClientException("Test did not complete in time.")
        text = self.user_read_console()
        result = self.user_read_int32(TEST_STATUS)
        return (result, text)

    def write_current_time(self):
        rtc = Rtc.from_current_time()
        self.user_write_memory(RTC_DATA, rtc)
        TEST_RTC_WRITE = 15
        (result, console) = self.perform_test(TEST_RTC_WRITE)
        logger.debug(f"Console Output:\n{console}")
        return result

    def get_seconds_after_epoch(self):
        TEST_RTC_READ = 14
        (result, console) = self.perform_test(TEST_RTC_READ)
        if (result != 0):
            raise TestFail("Reading RTC from DUT failed.")
        logger.debug(f"Console Output:\n{console}")
        rtc = self.user_read_memory(RTC_DATA, 12)
        dt = Rtc.from_rtc_bytes(rtc)
        logger.info("Date read from RTC: " + dt.strftime("%A %Y-%m-%d, %H:%M:%S"))
        fixed = datetime(1980, 1, 1)
        return dt.timestamp() - fixed.timestamp()

def find_ones(b):
    ret = []
    for idx,byte in enumerate(b):
        for i in range(8):
            if byte & (1 << i) != 0:
                ret.append(idx * 8 + i)
    return ret

def find_zeros(b):
    ret = []
    for idx,byte in enumerate(b):
        for i in range(8):
            if byte & (1 << i) == 0:
                ret.append(idx * 8 + i)
    return ret

if __name__ == '__main__':
    t = Tester()

    #TesterADC.report_adcs(t)
    sup = TesterADC.read_adc_channel(t, 'Supply', 4)
    logger.info(f"Supply is: {sup:.2f} V")
