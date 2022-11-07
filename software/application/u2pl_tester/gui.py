import tkinter as tk
from tkinter import ttk, messagebox
import tkinter.scrolledtext as tkscrolled
from PIL import ImageTk, Image
from pprint import pprint
import logging
from tests import UltimateIIPlusLatticeTests, TestFail, TestFailCritical, JtagClientException
from support import TesterADC

# sudo apt install python3-pil python3-pil.imagetk

class TextboxLogHandler(logging.StreamHandler):
    def __init__(self, widget):
        logging.StreamHandler.__init__(self)
        self.widget = widget

    def emit(self, record):
        # Record has: name, msg, args, levelname, levelno, pathname, filename, module,
        # exc_info, exc_text,stack_info, lineno, funcName, created, msecs, relativeCreated,
        # thread, threadName, processName, process, message, asctime
        if self.formatter:
            text = self.formatter.format(record)
        else:
            text = record.message
        self.widget.insert(tk.END, text + '\n')
        self.widget.see(tk.END)
        self.widget.update()

class InfoField:
    def __init__(self, parent, name, col, row, label_width, info_width):
        self.frame = tk.Frame(parent)
        self.label = tk.Label(self.frame, text = name, padx = 10, pady = 2, width = label_width)
        self.label.grid(column = 0, row = 0, sticky=tk.W)
        self.entry = tk.Label(self.frame, borderwidth=2, relief="groove", width = info_width, anchor = 'w')
        self.entry.grid(column = 1, row = 0, pady = 2)
        self.frame.grid(column = col, row = row)
        self._info = ""

    @property
    def info(self):
        return self._info

    @info.setter
    def info(self, data):
        self._info = data
        self.entry.configure(text = data)
        self.frame.update()

class InfoFields:
    def __init__(self, parent, fields, col, row, label_width, info_width):
        #self.field_names = fields
        self.fields = { }
        self.frame = tk.Frame(parent, borderwidth=2, relief="ridge")
        for idx, field in enumerate(fields):
            self.fields[field] = InfoField(self.frame, field, 0, idx, label_width, info_width)
        self.frame.grid(column = col, row = row, padx = 8, pady = 8)

    def set(self, name, value):
        self.fields[name].info = value

    def clear(self):
        for stat in self.fields:
            self.fields[stat].info = ""

class MyGui:
    def __init__(self):
        self.CollectTests()

    def CollectTests(self):
        self.testsuite = UltimateIIPlusLatticeTests()
        self.after = { }
        functions = self.testsuite.get_all_tests()
        self.functions = { }
        for (name, func) in functions.items():
            if func.__doc__:
                self.functions[name] = (func, func.__doc__)
            else:
                self.functions[name] = (func, name[9:])

        self.after['test_001_regulators'] = self.UpdateVoltages
        self.after['test_000_boot_current'] = self.UpdateCurrent
        self.after['test_019_unique_id'] = self.UpdateUniqueId
        self.after['test_018_frequencies'] = self.UpdateOscillator
        self.after['test_020_board_revision'] = self.UpdateBoardRevision

    def FlashUpdateFPGA(self, val):
        self.progress[0]['value'] = val
        self.progress[0].update()

    def FlashUpdateAppl(self, val):
        self.progress[1]['value'] = val
        self.progress[1].update()

    def FlashUpdateFAT(self, val):
        self.progress[2]['value'] = val
        self.progress[2].update()

    def StartButtonClick(self, param):
        self.window.after(10, self.ExecuteTests)

    def RunOneTest(self, name):
        (func, doc) = self.functions[name]
        self.textbox.insert(tk.END, f"Running test '{doc}'\n{'-' * (14 + len(doc))}\n")
        self.textbox.see(tk.END)
        self.window.update()
        critical = False
        try:
            func(self.testsuite)
            self.test_icon_canvases[name].itemconfig(self.test_icon_images[name], image = self.img_pass)
            self.textbox.insert(tk.END, "-> Result: OK!\n\n")
        except TestFailCritical as e:
            self.test_icon_canvases[name].itemconfig(self.test_icon_images[name], image = self.img_fail)
            self.textbox.insert(tk.END, "-> Result: CRITICAL FAILURE!\n")
            self.textbox.insert(tk.END, f"Reason: {e}\n\n")
            critical = True
        except TestFail as e:
            self.test_icon_canvases[name].itemconfig(self.test_icon_images[name], image = self.img_fail)
            self.textbox.insert(tk.END, "-> Result: FAIL!\n")
            self.textbox.insert(tk.END, f"Reason: {e}\n\n")
            self.errors += 1
        except JtagClientException as e:
            messagebox.showerror("Failure!", f"Communication Error!\n{e}\nRestart Tester Application!")
            exit()

        if name in self.after:
            self.after[name]()

        self.textbox.see(tk.END)
        self.window.update()
        return critical

    def ExecuteTests(self):
        self.start_button.configure(state = 'disabled')
        self.progress[0]['value'] = 0
        self.progress[1]['value'] = 0
        self.progress[2]['value'] = 0
        self.window.update()

        self.serial = self.serial_entry.get()
        if len(self.serial) == 0:
            messagebox.showerror("Serial #", "No serial number given!")
            self.start_button.configure(state = 'normal')
            return

        if not hasattr(self.testsuite, 'dut'):
            try:
                self.testsuite.startup()
            except JtagClientException as e:
                messagebox.showerror("Failure!", f"Could not communicate with tester.\n{e}\nCheck if it is powered.\nCheck the USB connection.")
                self.start_button.configure(state = 'normal')
                return

        self.errors = 0
        self.testsuite.proto = False

        for name, (func, _) in self.functions.items():
            self.test_icon_canvases[name].itemconfig(self.test_icon_images[name], image = self.img_err)

        self.stats.clear()
        self.stats.set('Serial #', self.serial)
        self.serial_entry.delete(0, tk.END)
        self.window.update()

        for name in self.functions:
            if self.RunOneTest(name): # Returns 'true' when a critical error occurred (could also have re-raised)
                break

        # If all tests are successful, the board can be flashed
        if self.errors == 0:
            self.testsuite.program_flash([self.FlashUpdateFPGA, self.FlashUpdateAppl, self.FlashUpdateFAT])
        else:
            messagebox.showerror("Reject", "Board has not been programmed due to errors.")

        if self.errors == 0:
            self.textbox.insert(tk.END, "\n*** BOARD SUCCESSFULLY TESTED AND PROGRAMMED! ***\n\n")
            self.textbox.see(tk.END)
            self.window.update()

        self.testsuite.shutdown()
        self.start_button.configure(state = 'normal')

    def setup(self):
        self.window=tk.Tk()
        self.window.title('Ultimate-II+L Factory Tester')
        self.img_logo = ImageTk.PhotoImage(Image.open("logo.jpg"))
        self.img_pass = ImageTk.PhotoImage(Image.open("pass.png"))
        self.img_fail = ImageTk.PhotoImage(Image.open("fail.png"))
        self.img_err  = ImageTk.PhotoImage(Image.open("err.png"))
        self.window.wm_iconphoto(False, self.img_pass)

        self.window.columnconfigure(0, weight = 2)
        self.window.columnconfigure(1, weight = 1)
        self.window.columnconfigure(2, weight = 1)
        self.window.columnconfigure(3, weight = 1)
        self.logo_canvas = tk.Canvas(self.window, width = self.img_logo.width(), height = self.img_logo.height())
        self.logo_canvas.create_image(self.img_logo.width() // 2, self.img_logo.height() // 2, image = self.img_logo)
        self.logo_canvas.grid(column = 0, row = 0, columnspan=4)

        self.test_frames = [ tk.Frame(self.window, padx = 5, pady = 5), tk.Frame(self.window, padx = 5, pady = 5) ]
        num_tests = len(self.functions)
        per_frame = (num_tests + 1) // 2
        self.test_icon_canvases = { }
        self.test_icon_images = { }
        for idx, name in enumerate(self.functions):
            (_func, doc) = self.functions[name]
            test_frame = self.test_frames[idx // per_frame]
            tk.Label(test_frame, text = doc, font=("Liberation Serif", 16)).grid(column = 0, row = len(self.test_icon_canvases), sticky=tk.W)
            canvas = tk.Canvas(test_frame, width = 48, height = 48)
            canvas.grid(column = 1, row = len(self.test_icon_canvases))
            self.test_icon_canvases[name] = canvas
            self.test_icon_images[name] = canvas.create_image(24, 24)

        self.test_frames[0].grid(column = 2, row = 1)
        self.test_frames[1].grid(column = 3, row = 1)

        self.serial_frame = tk.Frame(self.window)
        tk.Label(self.serial_frame, text = "Serial #:").grid(column = 0, row = 0)
        self.serial_entry = tk.Entry(self.serial_frame)
        self.serial_entry.grid(column = 1, row = 0)
        self.serial_frame.grid(column = 1, row = 3)

        self.start_button = tk.Button(self.window, text="Start!", font=("Liberation Serif", 16))
        self.start_button.grid(columnspan = 2, column = 2, row = 3, sticky = tk.EW, padx = 15, pady = 15)
        self.start_button.bind('<Button-1>', self.StartButtonClick)

        self.textbox = tkscrolled.ScrolledText(self.window, width = 80, height = 20, wrap='word')
        self.textbox.grid(column = 0, row = 1, padx = 8, pady = 8, sticky="nsew", rowspan = 3)

        self.progress_frame = tk.Frame(self.window)
        self.progress = []
        self.progress.append(ttk.Progressbar(self.progress_frame, orient = 'horizontal', mode = 'determinate', length = 500))
        self.progress.append(ttk.Progressbar(self.progress_frame, orient = 'horizontal', mode = 'determinate', length = 500))
        self.progress.append(ttk.Progressbar(self.progress_frame, orient = 'horizontal', mode = 'determinate', length = 500))
        self.progress[0].grid(column = 1, row = 0, pady = 4)
        self.progress[1].grid(column = 1, row = 1, pady = 4)
        self.progress[2].grid(column = 1, row = 2, pady = 4)
        tk.Label(self.progress_frame, text = "Flashing FPGA Bitfile", anchor = 'w').grid(column = 0, row = 0, sticky = tk.W, padx = 10)
        tk.Label(self.progress_frame, text = "Flashing Application", anchor = 'w').grid(column = 0, row = 1, sticky = tk.W, padx = 10)
        tk.Label(self.progress_frame, text = "Flashing FAT FileSystem", anchor = 'w').grid(column = 0, row = 2, sticky = tk.W, padx = 10)
        self.progress_frame.grid(columnspan = 3, column = 1, row = 2, padx = 10, pady = 5)

        self.stats = InfoFields(self.window, ["Serial #", "Tester Supply", "Boot Current", "+5.0V", "+4.3V", "+3.3V", "+2.5V", "+1.8V",
                                              "+1.1V", "+0.9V", "Flash ID",
                                              "FPGA ID", "Lot #", "Wafer #", "X / Y / Step",
                                              "Ref. Clock", "Oscillator", "Board Revision"], 1, 1, 14, 20)

        ch = TextboxLogHandler(self.textbox)
        ch.setFormatter(logging.Formatter('%(asctime)s - %(name)s - %(message)s'))
        UltimateIIPlusLatticeTests.add_log_handler(ch)
        TesterADC.add_log_handler(ch)

        messagebox.showinfo("Welcome!", "Welcome to the U2+L Tester.\nPlace a board and press start!\n")

        self.textbox.insert(tk.END, "Welcome to the U2+L Tester.\nPlace a board and press start!\n")
        self.textbox.see(tk.END)
        self.window.update()

    def run(self):
        self.window.mainloop()

    def UpdateCurrent(self):
        self.stats.set('Tester Supply', f'{self.testsuite.supply:.2f} V')
        self.stats.set('Boot Current', f'{self.testsuite.current:.0f} mA')

    def UpdateVoltages(self):
        self.stats.set('+5.0V', self.testsuite.voltages[0])
        self.stats.set('+4.3V', self.testsuite.voltages[1])
        self.stats.set('+3.3V', self.testsuite.voltages[2])
        self.stats.set('+2.5V', self.testsuite.voltages[3])
        self.stats.set('+1.8V', self.testsuite.voltages[4])
        self.stats.set('+1.1V', self.testsuite.voltages[5])
        self.stats.set('+0.9V', self.testsuite.voltages[6])

    def UpdateOscillator(self):
        self.stats.set('Ref. Clock', f'{self.testsuite.refclk:.6f} MHz')
        self.stats.set('Oscillator', f'{self.testsuite.osc:.6f} MHz')
        
    def UpdateUniqueId(self):
        self.stats.set('FPGA ID', f'{self.testsuite.unique:016X}')
        self.stats.set('Lot #', f'{self.testsuite.lot:08X}')
        self.stats.set('Wafer #', f'{self.testsuite.wafer}')
        self.stats.set('X / Y / Step', f'({self.testsuite.x_pos}, {self.testsuite.y_pos}, {self.testsuite.extra})')

    def UpdateBoardRevision(self):
        self.stats.set('Board Revision', f'{self.testsuite.revision}')
        self.stats.set('Flash ID', f'{self.testsuite.flashid:016X}')
        if self.testsuite.revision == 16:
            self.testsuite.proto = True
            self.errors -= 1
            # Rerun test!
            self.RunOneTest('test_001_regulators')

if __name__ == '__main__':
    gui = MyGui()
    gui.setup()
    gui.run()
