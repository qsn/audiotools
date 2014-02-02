#ifndef WAVE_H
#define WAVE_H

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define HEADER_RIFF 0x46464952
#define HEADER_WAVE 0x45564157
#define HEADER_FMT  0x20746d66
#define HEADER_DATA 0x61746164
#elif __BYTE_ORDER == __BIG_ENDIAN
#define HEADER_RIFF 0x52494646
#define HEADER_WAVE 0x57415645
#define HEADER_FMT  0x666d7420
#define HEADER_DATA 0x64617461
#endif

#define HEADER_LENGTH 44
#define DEPTH 16
#define DEPTH_MAX 32768

struct wave_header
{
	uint32_t chunkid;
	uint32_t chunksize;
	uint32_t format;
	uint32_t fmtchunkid;
	uint32_t fmtchunksize;
	uint16_t audiofmt;
	uint16_t nchannels;
	uint32_t srate;
	uint32_t brate;
	uint16_t balign;
	uint16_t bps;
	uint32_t datachunkid;
	uint32_t datachunksize;
};

#endif // WAVE_H
