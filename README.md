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

## History
Since I grew up with the Commodore 64, I've always loved the kind of music it generated and have been fascinated with synthesizers in general. In the last few years, I've been playing with the Arduino and related processors and recently came across the [Miduino](http://emotiscope.co/miduino/). I like the idea of the producing music with an Arduino, but don't like the limitations of that project, specifically that:
* Only pulse waves are supported
* Only 3 voices are supported
* All 3 timers of the 328 are used, which no possibility of using htem for anything else
I don't think there's anything wrong with the Miduino at all, I just wanted to see if I could do any better.

## Status
The current status is that I prototype in python writing out .wav files. Once I get results I want, I port the code to the 328 and test and debug there.
I currently have a python script that reads a midi file (my currently sample uses sid2midi to convert one of my favorite C64 tunes) and outputs a format I use to output a tune.
The audio is being output only with sine waves at the moment because my first attempt at pulse waves sounded really rough and I wanted to try to do better. However, my sine wave output also has problems and isn't necessary to emulate a SID chip anyway, so I'll work on the other waveforms now.
There are many different ways to get audio out from the Arduino, but I happened to have an [Adafruit Wave Shield](https://www.adafruit.com/product/94) which has a 10 bit DAC and a headphone jack which is exactly what I needed to experiment on the couch without bothering anyone else, so that's what I've been using.
