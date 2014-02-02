#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <jack/jack.h>

#include "metronome.h"

// create the sound sample for a metronome click at the chosen BPM
struct click *generate_click(unsigned int bpm, unsigned long srate, int freq, float amplitude, unsigned int beep_ratio)
{
	struct click *click = malloc(sizeof(struct click));
	memset(click, 0, sizeof(struct click));

	click->size = (jack_nframes_t) (60 * srate / bpm);
	jack_nframes_t beep_frames = click->size / beep_ratio;
	// if bpm is too low, don't let the click get too long
	if (bpm < 60)
		beep_frames = (jack_nframes_t) (srate / beep_ratio);
	double omega = 2 * M_PI * freq / srate;
	click->buf = malloc(click->size * sizeof(jack_default_audio_sample_t));
	jack_nframes_t k;
	for (k = 0; k < beep_frames; k++)
		click->buf[k] = (jack_default_audio_sample_t) (amplitude * sin(k * omega));
	for (k = beep_frames; k < click->size; k++)
		click->buf[k] = 0;

	return click;
}

void free_click(struct click *click)
{
	if (click != NULL) {
		free(click->buf);
		free(click);
		click = NULL;
	}
}
