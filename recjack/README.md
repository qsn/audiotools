recjack
=======

recjack is a simple record/playback tool for JACK, mostly made for music or language practice. Start recjack, hit the space bar to start and stop recording. It includes a basic metronome and can save the last recording to a file.

usage
-----

Compile with `make`, then run with `./recjack`. Type h for some help on how to use the interface.

Hit the space bar to start recording. When you're done, hit the space bar again to listen to your recording. Use 'r' to listen to the last recording again. When you start a new recording, the last one is lost. It can be saved with 's'.

metronome
---------

If you want a metronome, you can specify the bpm on the command-line. The bpm can be modified later with the arrow keys. The metronome can be enabled later on with the arrow keys.
```
up/down: +/- 10bpm
right/left: +/- 1bpm
m: disable/enable the metronome
```
The metronome is synchronized with the recording during replay.

saving
------

In "Waiting" mode, hit 's' and type a tag for the file. The date and time will be prepended.

Example:
```
Filename
 > aa
buffer saved to 2014-02-02_23-11_aa.wav
```