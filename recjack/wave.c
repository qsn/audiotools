#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <jack/jack.h>

#include "recjack.h"
#include "wave.h"

ssize_t write_wave_header(int fd, unsigned long srate, size_t wave_size)
{
	struct wave_header h;

	uint32_t wsize = (uint32_t) wave_size * (DEPTH / 8);

	h.chunkid = HEADER_RIFF;
	h.chunksize = HEADER_LENGTH + wsize;
	h.format = HEADER_WAVE;
	h.fmtchunkid = HEADER_FMT;
	h.fmtchunksize = 16;
	h.audiofmt = 1;
	h.nchannels = 1;
	h.srate = (uint32_t) srate;
	h.brate = h.srate * h.nchannels * DEPTH / 8;
	h.balign = h.nchannels * DEPTH / 8;
	h.bps = DEPTH;
	h.datachunkid = HEADER_DATA;
	h.datachunksize = wsize;

	return write(fd, &h, HEADER_LENGTH);
}

int write_wave_samples(int fd, size_t wave_size, char *buf)
{
	// convert from floats to DEPTH integers and write
	unsigned int i, offset;
	unsigned short blen = 1024;
	uint16_t *tmp = malloc(blen * sizeof(uint16_t));
	jack_default_audio_sample_t *samples = (jack_default_audio_sample_t *) buf;

	for (offset = 0; offset < wave_size; offset += blen) {
		for (i = 0; i < blen && i < (wave_size - offset); i++)
			tmp[i] = (uint16_t) (DEPTH_MAX * samples[i + offset]);
		if (wave_size < (offset + blen))
			blen = (unsigned short) (wave_size - offset);
		if (write(fd, tmp, blen * sizeof(uint16_t)) < 0) {
			perror("write failed");
			free(tmp);
			return -1;
		}
	}

	free(tmp);
	return 0;
}
