#ifndef METRONOME_H
#define METRONOME_H

struct click
{
	jack_default_audio_sample_t *buf;
	jack_nframes_t size;
};

#define DEFAULT_BPM 60

struct click *generate_click(unsigned int bpm, unsigned long srate, int freq, float amplitude, unsigned int beep_ratio);
void free_click(struct click *click);

#endif // METRONOME_H
