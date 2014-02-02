#ifndef RECJACK_H
#define RECJACK_H

#include <sys/types.h>
#include <sys/stat.h>

#include <jack/jack.h>

#define DIR_OUT 1
#define DIR_IN 2

#define STDIN 1
#define MODE_RECORD 1
#define MODE_LISTEN 2
#define MODE_PAUSED 3
#define MODE_REWAIT 4
#define MODE_LIWAIT 5

#define FILEEXT "wav"
#define DATEFMT "%Y-%m-%d_%H-%M"
#define DATELEN 17
#define FILEFMT "%s_%s.%s"
#define FILEPERM S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH

#define KEY_ESCAPE 27

struct buffer
{
	char *buf;
	size_t size;
	size_t offset;
	jack_nframes_t frames_off;
	unsigned long srate;
};

void jack_shutdown(void *arg) __attribute__((noreturn));
int process(jack_nframes_t nframes, void *arg);
void change_mode(struct buffer *b, char m);
int save_buffer(struct buffer *b);
ssize_t write_wave_header(int fd, unsigned long srate, size_t wave_size);
int write_wave_samples(int fd, size_t size, char *buf);

#endif // RECJACK_H
