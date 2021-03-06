# SIDish
SID chip type synthesizer for Arduino

## Goal
The goal for this project is to make a synthesizer that can run on an Arduino Uno
with capabilities similar to that of the Commodore 64's famous SID chip. It is not
meant to be a perfect reproduction, especially as I currently assume the Atmega 328
in the Uno isn't powerful enough to do real-time filtering and possibly not ring
modulation. But I plan to find how close I can get. As a minimum, I hope to get:
* 4 voices (up from the 3 of a SID since mixing 4 voices may be less processor intensive than 3)
* 4 available waveforms: sawtooth, triangle, pulse, and noise
* ASDR envelope

Stretch goals:
* Ring modulation
* Oscillator sync
* Support for filtering
* Extra channels that can sync external hardware effects with music
* Work on desktop systems for faster debugging

## History
Since I grew up with the Commodore 64, I've always loved the kind of music it generated and have been fascinated with synthesizers in general. In the last few years, I've been playing with the Arduino and related processors and recently came across the [Miduino](http://emotiscope.co/miduino/). I like the idea of the producing music with an Arduino, but don't like the limitations of that project, specifically that:
* Only pulse waves are supported
* No percussion sounds are available
* Only 3 voices are supported
* All 3 timers of the 328 are used, which no possibility of using them for anything else
I don't think there's anything wrong with the Miduino at all, I just wanted to see if I could do any better.

## Status
The current status is that basic support for playing goattracker files works. Not everything is implemented or works
correctly, but songs are okay to recognizable. It can be compiled both to write out .wav files on the host system or
play in realtime on an ATmega.
ATmega output works for both PWM output on a single pin or to the [Adafruit Wave Shield](https://www.adafruit.com/product/94). 
It happens to have a 10 bit DAC and a headphone jack which is exactly what I needed to experiment on the couch without bothering anyone else, 
so that's what I've been using.

Feature status:

| Group | Feature      | Status |
| ----- | ------------ | ------ |
| Synthesizer Features | | |
| | Sawtooth Wave | Works |
| | Triangle Waveform | Works |
| | Pulse Waveform | Mostly works though there are still some bugs with pulse width and very big and small pulse widths|
| | Variable pulse width | Works |
| | Synchronize | Not implemented |
| | Ring modulation | Not implemented |
| | Multiple simultaneous waveforms per channel | Not implemented and not likely to get implemented |
| Pattern commands | | |
| | Portamento | Not implemented |
| | Toneportamento | Not implemented |
| | Vibrato | Not implemented |
| | Alter ADSR from pattern data | Not implemented |
| | Filter | Not implemented |
| | Set master volumne | Not implemented |
| | Funktempo | Not implemented |
| | Set global tempo | Works |
| | Set channel tempo | Works |
| | Non audio effects | Not implemented |
| Wavetable commands | | |
| | Delay | Works |
| | Waveform values | Works |
| | Execute command | Not implemented |
| | Relative notes | Works |
| | Negative relative notes | Maybe - needs testing |
| | Unchanged notes | Works |
| | Absolute notes | Works |
| | Non audio effects | Not implemented |
| Pulsetable commands | | |
| | Modulation steps (positive) | Works |
| | Modulation steps (negative) | Maybe - needs to be tested |
| | Jump | Works |
| Instrument features | | |
| | ASDR | Works |
| | Wavetable support | Partial |
| | Pulsetable support | Works |
| | Filtertable support | Not implemented possibly never to be |
| | Vibrato | Not implemented |
| | Gateoff timer | Not implemented |
| | Hard reset | Not implemented |
| Orderlist | | |
| | Pattern number | Works |
| | Repeat pattern | Works |
| | Transpose pattern | Works |
| | End/jump | Partial |
| Song features | | |
| | Subtunes | Just barely (only enough to not break when encountered) |

## To Do
* Split out the synthesizer from the player. I'm not sure why I combined them so much other than the comment in the code about allowing better optimization. Seems like a weak argument to me now.
* Fix Windows support to cleanly exit
* Fix Windows support for printing numbers in hexadecimal
