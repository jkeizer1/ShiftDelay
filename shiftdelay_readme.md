# ShiftDelay

A pitch-adaptive phase shifter and delay effect for the Expert Sleepers Disting NT module.

## Overview

ShiftDelay combines two complementary effects in a single module: frequency-aware phase shifting and traditional time delay. Unlike conventional phase shifters that use all-pass filters, ShiftDelay implements phase shifting in the time domain, creating musical phase relationships that remain harmonically consistent across different pitches.

CV modulation of phase is important for to achieve the phase cancellation/augmentation effect. 
CV v/Oct is used to maintain harmonic relationships, patch in from your sequencer or whatever.
Changing the delay time causes interesting shifts, I am working to smooth this out.

## Key Features

- **Pitch-Adaptive Phase Shifting**: Phase offset automatically scales with input frequency to maintain musical relationships
- **Independent Time Delay**: Traditional delay line (0.5-6 seconds) runs in parallel with phase shifting
- **Smooth Parameter Changes**: Intelligent crossfading prevents clicks when delay times change
- **CV Pitch Tracking**: Responds to 1V/octave CV input for frequency-aware processing
- **Clean Audio Path**: Optimized for low-latency embedded processing

## DSP Architecture

### Phase Shifting Algorithm
The phase shifter converts phase offset (in degrees) to time delay using the formula:
```
delayTime = (phaseOffset / 360°) / frequency
```
See the shift delay math analysis readme for details of of the DSP used.

This approach ensures that a 90° phase shift at 440Hz produces the same harmonic relationship as 90° at 880Hz, maintaining musical coherence across the frequency spectrum.

### Dual Delay Structure
- **Phase Delay**: Calculated from CV pitch input and phase offset parameter (can go negative)
- **Base Delay**: Fixed time delay independent of pitch
- **Total Delay**: Sum of both delays, creating complex phase/time relationships

### Parameter Smoothing
- Exponential smoothing prevents zipper noise during parameter changes
- Adaptive smoothing rates based on parameter stability
- Crossfading during delay time changes eliminates audio artifacts

## Parameters

| Parameter | Range | Description |
|-----------|-------|-------------|
| Phase Offset | -360° to +360° | Phase shift amount (frequency-adaptive) |
| Delay Time | 5ms to 6000ms | Fixed time delay |
| Dry/Wet Mix | 0% to 100% | Balance between processed and dry signal |
| Pitch CV | -5V to +5V | 1V/octave pitch input for frequency tracking |

## Use Cases

- **Harmonic Phase Shifting**: Create phase relationships that track musical intervals
- **Complex Delays**: Combine pitch-dependent and fixed delays for unique textures
- **Stereo Width**: Use phase shifting to create spatial effects
- **Harmonic Modulation**: Modulate phase offset for frequency-aware chorus effects
- **Sound Design**: Create pitch-tracking delays for evolving textures

## Technical Specifications

- **Sample Rate**: 32Khz to 192kHz
- **Delay Buffer**: 6 seconds maximum at 192kHz
- **Interpolation**: Linear interpolation for smooth delay line reading
- **Processing**: 32-bit floating point
- **Memory Usage**: ~4.6MB (primarily delay buffer)

## Building

Requires ARM cross-compiler and Disting NT API SDK:

```bash
# Install cross-compiler
sudo apt ugprade
sudo apt install -y build-essential git gcc-arm-none-eabi binutils-arm-none-eabi

# Check
arm-none-eabi-gcc --version
    arm-none-eabi-gcc (15:10.3-2021.07-4) 10.3.1 20210621 (release)
    Copyright (C) 2020 Free Software Foundation, Inc.
    This is free software; see the source for copying conditions.  There is NO
    warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# Clone Disting NT API
git clone https://github.com/expertsleepersltd/distingNT_API

# Create Makefile, sources...

# Build plugin
make

# Copy ShiftDelay.o to Disting NT SD card /plugins/ directory
```

## Implementation Notes

This plugin demonstrates several advanced embedded audio techniques:
- Memory-efficient circular buffer management
- Parameter stability detection with hysteresis
- Crossfading for glitch-free parameter changes
- Frequency-adaptive DSP processing
- Optimized fixed-point and floating-point operations

The algorithm was originally developed as a VCV Rack module and adapted for the Disting NT's embedded ARM environment, showcasing the translation of desktop audio concepts to real-time embedded systems.

## License

MIT License

## Credits

Developed for the Expert Sleepers Disting NT module using the official distingNT SDK.
