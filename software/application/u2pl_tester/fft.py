
# Python example - Fourier transform using numpy.fft method
import numpy as np
import matplotlib.pyplot as plotter
from io import  BytesIO

def do_fft(amplitude, samplingFrequency):
    # Frequency domain representation
    fourierTransform = np.fft.fft(amplitude)/len(amplitude)           # Normalize amplitude
    fourierTransform = fourierTransform[range(int(len(amplitude)/2))] # Exclude sampling frequency
    
    tpCount     = len(amplitude)
    values      = np.arange(int(tpCount/2))
    timePeriod  = tpCount/samplingFrequency
    frequencies = values/timePeriod
    fftdata = abs(fourierTransform)

    peak = np.ptp(fftdata)
    index = np.argmax(fftdata) 
    return (peak, frequencies[index], fftdata)


def plot_all(left, right, samplingFrequency, fftleft, fftright):
    # At what intervals time points are sampled
    samplingInterval    = 1 / samplingFrequency
    
    # Begin time period of the signals
    beginTime           = 0
    
    # End time period of the signals
    endTime             = len(left) * samplingInterval; 
    
    # Time points
    time                = np.arange(beginTime, endTime, samplingInterval)

    tpCount     = len(left)
    values      = np.arange(int(tpCount/2))
    timePeriod  = tpCount/samplingFrequency
    frequencies = values/timePeriod

    # Create subplot
    figure, axis = plotter.subplots(4 if right else 2, 1)
    plotter.subplots_adjust(hspace=1)

    # Time domain representation for sine wave 1
    axis[0].set_title('Left Wave')
    axis[0].plot(time, left)
    axis[0].set_xlabel('Time')
    axis[0].set_ylabel('Amplitude')
    
    # Frequency domain representation
    axis[1].set_title('Fourier transform Left')
    axis[1].plot(frequencies, fftleft)
    axis[1].set_xlabel('Frequency')
    axis[1].set_ylabel('Amplitude')

    if right:
        # Time domain representation for sine wave 2
        axis[2].set_title('Right Wave')
        axis[2].plot(time, right)
        axis[2].set_xlabel('Time')
        axis[2].set_ylabel('Amplitude')
    
        # Frequency domain representation
        axis[3].set_title('Fourier transform Right')
        axis[3].set_yscale("log")
        axis[3].plot(frequencies, fftright)
        axis[3].set_xlabel('Frequency')
        axis[3].set_ylabel('Amplitude')

    plotter.show()

def calc_fft(filename, plot = False):
    # Create arrays from file
    filedata = np.fromfile(filename, [('left', np.int32), ('right', np.int32)])
    left, right = zip(*filedata)
    left = np.array(left) / pow(2.0, 31)
    right = np.array(right) / pow(2.0, 31)

    # How many time points are needed i,e., Sampling Frequency
    samplingFrequency   = 48000
    left_ampl, left_freq, fft_left = do_fft(left, samplingFrequency)    
    right_ampl, right_freq, fft_right = do_fft(right, samplingFrequency)    

    if plot:
        plot_all(left, right, samplingFrequency, fft_left, fft_right)

    return ((left_ampl, left_freq), (right_ampl, right_freq))

def calc_fft_mono(filename, plot = False):
    # Create arrays from file
    filedata = np.fromfile(filename, [('spk', np.int32)])
    (spk,) = zip(*filedata)
    spk = np.array(spk) / pow(2.0, 31)

    # How many time points are needed i,e., Sampling Frequency
    samplingFrequency   = 48000
    ampl, freq, fft = do_fft(spk, samplingFrequency)    

    if plot:
        plot_all(spk, None, samplingFrequency, fft, fft)

    return (ampl, freq)
