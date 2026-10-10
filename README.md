# FreqWeave

An audio plugin that swaps or blends frequency bands between a main input and a sidechain input, using FFT-based processing.
A detailed technical report can be found [here](https://github.com/enqilian/FreqWeave/blob/main/TechnicalReport.pdf).

## Introduction

FreqWeave lets you take a frequency band from a sidechain signal and put it into a main audio signal. Everything outside the two bands you choose passes through untouched. It can be used for creative sound design, mixing, or experimenting with how two sounds share the spectrum.

## Features

- **Two independent bands**: choose the centre frequency of each band separately; both share the same width, set in octaves.
- **Swap or blend**: replace a band completely with the sidechain, or blend main and sidechain per band with a Mix control for a gradual effect.
- **Sidechain input**: the second input bus is used as the source for the band content.
- **Live spectrum display**: shows the main input, the sidechain input and the plugin output, with the two bands highlighted.
- **Latency reporting**: the plugin reports its 2048 samples of latency to the host so the DAW can compensate.

## Building

1. **Clone the repository**

   ```bash
   git clone https://github.com/enqilian/FreqWeave.git
   ```

2. **Open the project**

   - Install [JUCE](https://juce.com) (developed against JUCE 9) and Xcode.
   - Open `FreqWeave.jucer` in the Projucer and point the module paths to your local JUCE `modules` folder.
   - Choose **Save and Open in IDE** to generate the Xcode project.

3. **Build**

   - In Xcode, build the `FreqWeave - VST3` target.
   - The Xcode build copies the plugin to `~/Library/Audio/Plug-Ins/VST3/`. Restart your DAW and rescan plugins.

Developed and tested on macOS (Apple silicon) with Ableton Live 12, as a VST3 plugin.

## Usage

1. **Load the plugin** on the track you want to process. This track is the **main input**.
2. **Route the sidechain**: in your DAW, send another track into the plugin's sidechain input (in Ableton Live, use the plugin's sidechain section and pick the source under *Audio From*).
3. **Choose the bands**:
   - **Cut From 1 / Cut From 2**: centre frequency of Band 1 and Band 2.
   - **Band Length**: width of both bands in octaves (0.1 to 3 octaves).
4. **Choose how the bands are filled**:
   - **Swap Mode on**: each band is completely replaced by the sidechain. The Mix knobs are greyed out because they have no effect in this mode.
   - **Swap Mode off**: use **Band 1 Mix** and **Band 2 Mix** to blend, from 0 % (main only) to 100 % (sidechain only), for a gradual effect.

### Quick test

Use white noise as the main input and a 1 kHz sine wave as the sidechain, with Swap Mode on. Set both *Cut From* knobs to 4 kHz: the sidechain is silent there, so a hole appears in the noise. Move them to 1 kHz: the hole fills with the sine tone. The spectrum display shows the main input (white), the sidechain (orange) and the output (pink).

## Parameters

| Name in the host | Description |
|---|---|
| `CutFrequencyFrom1` | Centre frequency of Band 1 (20 Hz to 20 kHz). |
| `CutFrequencyFrom2` | Centre frequency of Band 2 (20 Hz to 20 kHz). |
| `FrequencyBandLength` | Width of both bands. Range 0 to 2, mapped to 0.1 to 3 octaves. |
| `ExchangeBandValueOrNot` | Swap Mode: 0 = blend with the Mix controls, 1 = replace the bands with the sidechain. |
| `Band1Mix` | Blend amount for Band 1 (0.0 = main, 1.0 = sidechain). Used when Swap Mode is off. |
| `Band2Mix` | Blend amount for Band 2 (0.0 = main, 1.0 = sidechain). Used when Swap Mode is off. |

## Notes

- The processing uses a 2048-point FFT with a Hann window and 50 % overlap, so the plugin has a fixed latency of 2048 samples (about 43 ms at 48 kHz).
- In blend mode the magnitude and the phase of the two signals are interpolated linearly. This is simple and works for many sources, but it can sound rough on some material.
- The centre frequencies are limited so that each band stays between 20 Hz and the Nyquist frequency.

## Author

- **Enqi Lian**
- [enqilian619@gmail.com](mailto:enqilian619@gmail.com)

## Acknowledgments

- Built using [JUCE](https://juce.com), a widely used framework for audio application development.
