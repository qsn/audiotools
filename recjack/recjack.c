#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#include <pthread.h>

#include <jack/jack.h>

#define HELP_MSG "space switches mode\n"                        \
	"m toggles the metronome (if a BPM has been set)\n"	\
	"s saves the buffer to a file\n"			\
	"r replays the last recording\n"			\
	"up/down increases/decreases the click by 10 BPM\n"	\
	"right/left increases/decreases the click by 1 BPM\n"	\
	"q exits"

#include "recjack.h"
#include "metronome.h"

static jack_port_t *input_port;
static jack_port_t *output_port;
static jack_port_t *metronome_port;
static jack_client_t *client;

static struct click *click = NULL;
static jack_nframes_t click_offset = 0;
static pthread_mutex_t click_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

static jack_latency_range_t input_latency_range;

static char mode;

#define STOPPED 0
#define RUNNING 1
static char metronome_state = STOPPED;
void connect_physical(jack_port_t *port, unsigned long flags, unsigned int n);
void connect_metronome(void);
void disconnect_metronome(void);
void toggle_metronome(void);
void metronome_synchronize(jack_nframes_t offset, jack_nframes_t *delay);
void init_jack(void);
void init_finish(void);
void display_help(void);

/*
  Connect port to available physical ports
  If n = 0, connect to all ports
  Otherwise, connect to the first n ports
*/
void connect_physical(jack_port_t *port, unsigned long flags, unsigned int n)
{
	unsigned int i;

	const char **ports = jack_get_ports(client, NULL, NULL, JackPortIsPhysical|flags);
	if (ports == NULL) {
		fprintf(stderr, "no physical port found\n");
		exit(1);
	}

	for (i = 0; ports[i] != NULL && (n == 0 || i < n); i++) {
		if (flags & JackPortIsInput) {
			if (jack_connect(client, jack_port_name(port), ports[i]))
				fprintf(stderr, "cannot connect physical port\n");
		} else if (flags & JackPortIsOutput) {
			if (jack_connect(client, ports[i], jack_port_name(port)))
				fprintf(stderr, "cannot connect physical port\n");
		}
	}

	free(ports);
}

/*
  Connect the metronome port to all the physical output ports
*/
void connect_metronome(void)
{
	if (metronome_state == STOPPED) {
		connect_physical(metronome_port, JackPortIsInput, 0);

		metronome_state = RUNNING;
	}
}

/*
  Disconnect the metronome port from all the physical output ports
*/
void disconnect_metronome(void)
{
	if (metronome_state == RUNNING) {
		const char **ports = jack_port_get_connections(metronome_port);
		if (ports == NULL)
			return;
		int i = 0;
		while (ports[i] != NULL) {
			if (jack_disconnect(client, jack_port_name(metronome_port), ports[i]))
				fprintf(stderr, "cannot disconnect output port\n");
			i++;
		}
		free(ports);
		metronome_state = STOPPED;
	}
}

/*
  Change metronome state: connected -> disconnected or disconnected -> connected
*/
void toggle_metronome(void)
{
	if (metronome_state == STOPPED)
		connect_metronome();
	else if (metronome_state == RUNNING)
		disconnect_metronome();
}

/*
  JACK callback function
  If JACK exits, stop running.
*/
void jack_shutdown(void *__attribute__((__unused__))arg)
{
	exit(1);
}

/*
  Synchronize the mode switch for recording/playing with a metronome click

  If we were waiting for synchronization with a new click and we reach
  the beginning of a new click, start recording/playing.

  Store the current offset in delay
*/
void metronome_synchronize(jack_nframes_t offset, jack_nframes_t *delay)
{
	if (click_offset == 0 && (mode == MODE_REWAIT || mode == MODE_LIWAIT)) {
		if (mode == MODE_REWAIT)
			mode = MODE_RECORD;
		else if (mode == MODE_LIWAIT)
			mode = MODE_LISTEN;
		// and offset with the right number of frames to get synchronization
		if (delay != NULL)
			*delay = offset;
	}
}

/*
  JACK callback function
  - first, process the metronome output
  - then, handle the recording/playback
*/
int process(jack_nframes_t nframes, void *arg)
{
	// metronome
	jack_nframes_t record_offset = 0;
	if (pthread_mutex_trylock(&click_mutex) != 0) {
		// mutex unavailable, don't output anything and wait for the next cycle
		click_offset += nframes;
	} else {
		jack_default_audio_sample_t *buf;
		jack_nframes_t remaining = nframes; // how many samples do we need to fill the buffer?
		jack_nframes_t written = 0; // how many samples have been written to the buffer?
		buf = (jack_default_audio_sample_t *) jack_port_get_buffer(metronome_port, nframes);

		// has a metronome been set up?
		// no metronome
		// write some silence, skip waiting mode and start recording/playing immediately
		if (click == NULL) {
			if (mode == MODE_REWAIT)
				mode = MODE_RECORD;
			if (mode == MODE_LIWAIT)
				mode = MODE_LISTEN;
			memset(buf, 0, nframes * sizeof(jack_default_audio_sample_t));
		} else {
			// click != NULL -- we do have a metronome
			// copy the whole click as many times as necessary to fill the buffer
			while ((click->size - click_offset) < remaining) {
				metronome_synchronize(written, &record_offset);

				memcpy(buf + written, click->buf + click_offset,
				       (click->size - click_offset) * sizeof(jack_default_audio_sample_t));
				remaining -= click->size - click_offset;
				written += click->size - click_offset;
				click_offset = 0;
			}

			// and complete if there's still some room in the buffer
			if (remaining > 0) {
				metronome_synchronize(written, &record_offset);

				memcpy(buf + written, click->buf + click_offset,
				       remaining * sizeof(jack_default_audio_sample_t));
				click_offset += remaining;
			}
		}
		pthread_mutex_unlock(&click_mutex);
	}
	// end metronome

	// recording/playing
	if (pthread_mutex_trylock(&buffer_mutex) == 0) {
		jack_default_audio_sample_t *s;
		struct buffer *b = (struct buffer *) arg;
		jack_nframes_t record_size = nframes - record_offset;
		size_t size = sizeof(jack_default_audio_sample_t) * record_size;
		size_t offset = sizeof(jack_default_audio_sample_t) * record_offset;

		if (mode == MODE_RECORD) {
			jack_latency_range_t range;
			jack_port_get_latency_range(input_port, JackCaptureLatency, &range);
			if (range.min != input_latency_range.min || range.max != input_latency_range.max) {
				input_latency_range.min = range.min;
				input_latency_range.max = range.max;
				//printf("Latency change: %d-%d\n", range.min, range.max);
			}

			// make the buffer larger and append the sample
			s = jack_port_get_buffer(input_port, nframes);
			b->size += size;
			b->buf = realloc(b->buf, b->size);
			memcpy(b->buf + b->offset, s + offset, size);
			b->offset += size;
		} else if (mode == MODE_LISTEN) {
			// get a sample from the buffer and play it
			s = jack_port_get_buffer(output_port, nframes);
			memset(s, 0, offset);
			// not enough data in the recording buffer to fill the output buffer?
			if (size > (b->size - b->offset)) {
				// write all we have, fill the rest with zeroes
				memcpy(s + offset, b->buf + b->offset, b->size - b->offset);
				memset(s + offset + b->size - b->offset, 0, size - (b->size - b->offset));
				pthread_mutex_unlock(&buffer_mutex);
				// playback complete, switch mode
				change_mode(b, 0);
			} else {
				memcpy(s + offset, b->buf + b->offset, size);
				b->offset += size;
			}
		} else {
			// if we are neither recording nor playing, write some silence
			s = jack_port_get_buffer(output_port, nframes);
			memset(s, 0, nframes * sizeof(jack_default_audio_sample_t));
		}
		pthread_mutex_unlock(&buffer_mutex);
	}

	return 0;
}

/*
  Save the current audio buffer to a file
  Ask the user for a tag to put in the filename
  Filename format: [date]_[time]_[tag].[ext]
*/
int save_buffer(struct buffer *b)
{
	// ask for a filename and write to disk
	char name[10];
	char saved = 0;
	while (!saved) {
		memset(name, 0, 10);
		printf("\nFilename (max 10 chars, press . to cancel, date/time will be added automatically):\n  > ");
		int n = scanf("%10s", name);
		if (n > 0) {
			if (name[0] == '.' && name[1] == 0) {
				// don't save
				printf("buffer not saved\n");
				break;
			} else {
				// filename format: [date]_[time]_[tag].[ext]
				char *date = malloc(DATELEN);
				char *filename = malloc(DATELEN+strlen(name) + 1+strlen(FILEEXT) + 1);
				time_t t;
				struct tm lt;

				t = time(NULL);
				if (localtime_r(&t, &lt) == NULL) {
					perror("localtime");
					// couldn't save, go back to what we were doing
				}
				strftime(date, DATELEN, DATEFMT, &lt);
				sprintf(filename, FILEFMT, date, name, FILEEXT);
				free(date);

				int fd = open(filename, O_RDONLY); // check that the file doesn't exist
				if (fd > 0) {
					printf("%s already exists, choose another file name or cancel\n", filename);
					close(fd);
					continue;
				}

				fd = open(filename, O_CREAT|O_WRONLY, FILEPERM);
				if (fd < 0) {
					perror("couldn't create the file");
					continue;
				}
				write_wave_header(fd, b->srate, b->size / sizeof(jack_default_audio_sample_t));
				write_wave_samples(fd, b->size / sizeof(jack_default_audio_sample_t), b->buf);
				close(fd);
				printf("buffer saved to %s\n", filename);
				free(filename);
				break;
			}
		}
	}
	mode = MODE_PAUSED;
	printf("Waiting...");
	fflush(stdout);
	return 0;
}

/*
  The main loop cycles through 3 states:
  - MODE_RECORD: the audio input is stored in a buffer
  - MODE_LISTEN: playback the buffer
  - MODE_PAUSE:  wait
  The natural sequence is PAUSE > RECORD > LISTEN > PAUSE

  In MODE_PAUSE, we may switch back directly to MODE_LISTEN, which
  restarts playback of the current audio buffer.

  MODE_LIWAIT and MODE_REWAIT wait for synchronization with the
  metronome. As soon as we are in sync with the metronome (at the
  start of the next click), recording/playback begins.
*/
void change_mode(struct buffer *b, char m)
{
	if (m != 0) {
		mode = m;
		printf("\nPlaying recorded bit...");
		fflush(stdout);
	} else {
		pthread_mutex_lock(&buffer_mutex);
		switch (mode) {
		case MODE_RECORD:
			mode = MODE_LIWAIT;
			printf("\nPlaying recorded bit...");
			fflush(stdout);
			// set the offset to the start of the buffer
			b->offset = 0;
			break;
		case MODE_LISTEN:
			mode = MODE_PAUSED;
			printf("\nWaiting...");
			fflush(stdout);
			b->offset = 0;
			b->offset = (input_latency_range.min + input_latency_range.max) / 2;
			break;
		case MODE_PAUSED:
			mode = MODE_REWAIT;
			printf("\nRecording...");
			fflush(stdout);
			// reset the buffer before starting to record
			free(b->buf);
			b->buf = NULL;
			b->size = 0;
			b->offset = 0;
			fflush(stdout);
			break;
		}
		pthread_mutex_unlock(&buffer_mutex);
	}
}

/*
  Main:
  - Parse command-line arguments
  - Initialize JACK
  - Initialize the terminal
  - Main loop
  - read a keystroke from the terminal to get a command
*/
int main(int argc, char **argv)
{
	char c;
	struct buffer b;
	b.buf = NULL;
	b.offset = 0;
	b.size = 0;
	b.frames_off = 0;
	b.srate = 0;

	printf("Type h for some help\nHit space to start or stop recording\n\n");

	// read bpm on the command line
	// no bpm, no metronome
	unsigned int bpm = 0;
	if (argc == 1) {
		printf("metronome: no bpm provided, disabling the metronome for now\n");
	} else {
		bpm = (unsigned int) atoi(argv[1]);
		printf("metronome: %d bpm\n", bpm);
	}

	init_jack();

	// set callbacks
	jack_set_process_callback(client, process, (void *) &b);
	jack_on_shutdown(client, jack_shutdown, 0);

	init_finish();

	b.srate = jack_get_sample_rate(client);
	if (bpm != 0) {
		pthread_mutex_lock(&click_mutex);
		click = generate_click(bpm, b.srate, 440, 0.5F, 10);
		pthread_mutex_unlock(&click_mutex);
		connect_metronome();
	} else {
		click = NULL;
	}

	//
	// Initialize the terminal
	// setup stdin to get the keystrokes
	//
	struct termios ttystate;
	long flags;

	tcgetattr(STDIN, &ttystate);
	flags = fcntl(STDIN, F_GETFL);
	ttystate.c_lflag &= (tcflag_t) ~ICANON;
	fcntl(STDIN, F_SETFL, flags | O_NONBLOCK);
	tcsetattr(STDIN, TCSANOW, &ttystate);

	//
	// start the main loop
	//
	mode = MODE_PAUSED;
	printf("Waiting...");
	fflush(stdout);

	while (1) {
		if (read(STDIN, &c, 1) == 1) {
			if (c == ' ')
				change_mode(&b, 0);
			else if (c == 'm' && bpm != 0)
				toggle_metronome();
			else if (c == 's' && mode == MODE_PAUSED) {
				// there's something in the buffer and we want to save it
				// temporarily reset the terminal
				ttystate.c_lflag |= ICANON;
				fcntl(STDIN, F_SETFL, flags);
				tcsetattr(STDIN, TCSANOW, &ttystate);

				save_buffer(&b);

				ttystate.c_lflag &= (tcflag_t) ~ICANON;
				fcntl(STDIN, F_SETFL, flags | O_NONBLOCK);
				tcsetattr(STDIN, TCSANOW, &ttystate);
			} else if (c == 'r' && mode == MODE_PAUSED) // replay
				change_mode(&b, MODE_LIWAIT);
			else if (c == 'q')
				break;
			else if (c == 'h')
				display_help();
			else if (c == KEY_ESCAPE) {
				read(STDIN, &c, 1);
				if (c == '[') {
					read(STDIN, &c, 1);
					int bpm_var;
					if (c == 'A') // up
						bpm_var = 10;
					else if (c == 'B') // down
						bpm_var = -10;
					else if (c == 'C') // right
						bpm_var = 1;
					else if (c == 'D') // left
						bpm_var = -1;
					else
						bpm_var = 0;

					if (bpm_var != 0) {
						// metronome has changed, generate the new sound
						struct click *click_tmp = click;
						click = NULL;
						free_click(click_tmp);
						if (bpm == 0) {
							// set to default bpm when first starting the metronome
							bpm = (unsigned int) DEFAULT_BPM;
						} else if (((int) bpm + bpm_var) > 0) {
							bpm = (unsigned int) ((int) bpm + bpm_var);
						} else {
							bpm = 0;
							printf("metronome disabled\n");
							continue;
						}
						printf("bpm: %d\n", bpm);
						click_tmp = generate_click(bpm, b.srate, 440, 0.5F, 10);
						click_offset = 0;
						pthread_mutex_lock(&click_mutex);
						click = click_tmp;
						pthread_mutex_unlock(&click_mutex);
						connect_metronome();
					}
				}
			}
		}
		usleep(250000);
	}

	printf("\nExiting.\n");
	// set the terminal back to normal
	ttystate.c_lflag |= ICANON;
	tcsetattr(STDIN, TCSANOW, &ttystate);

	pthread_mutex_destroy(&click_mutex);

	// shutdown JACK
	jack_client_close(client);

	return 0;
}


void display_help()
{
	printf("\n\nInterface help:\n");
	printf("%s\n", HELP_MSG);
}


/*
  Setup JACK
*/
void init_jack()
{
	const char *client_name = "recjack";
	jack_status_t status;

	client = jack_client_open(client_name, JackNullOption, &status);
	if (client == NULL) {
		fprintf(stderr, "jack_client_open failed, status = 0x%2.0x\n", status);
		if (status & JackServerFailed)
			fprintf(stderr, "Unable to connect to JACK server\n");
		exit(1);
	}

	if (status & JackServerStarted)
		fprintf(stderr, "JACK server started\n");
	if (status & JackNameNotUnique) {
		client_name = jack_get_client_name(client);
		fprintf(stderr, "unique name '%s' assigned\n", client_name);
	}

	// create an input port (recording) and two output ports (playback, metronome)
	input_port = jack_port_register(client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	output_port = jack_port_register(client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	metronome_port = jack_port_register(client, "metronome", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

	if ((input_port == NULL) || (output_port == NULL) || (metronome_port == NULL)) {
		fprintf(stderr, "no more JACK ports available\n");
		exit(1);
	}
}

void init_finish()
{
	if (jack_activate(client)) {
		fprintf(stderr, "cannot activate client\n");
		exit(1);
	}

	// connect the first physical input port to the input port
	connect_physical(input_port, JackPortIsOutput, 1);

	// connect the output port to all physical outputs
	connect_physical(output_port, JackPortIsInput, 0);

	jack_port_get_latency_range(input_port, JackCaptureLatency, &input_latency_range);
	//printf("Input latency range: %d-%d\n", input_latency_range.min, input_latency_range.max);
	jack_latency_range_t range;
	jack_port_get_latency_range(output_port, JackPlaybackLatency, &range);
	//printf("Output latency: %d-%d\n", range.min, range.max);
	jack_port_get_latency_range(metronome_port, JackPlaybackLatency, &range);
	//printf("Metronome latency: %d-%d\n", range.min, range.max);
}
