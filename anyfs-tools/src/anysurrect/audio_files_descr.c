/*
 *	audio_files_descr.c
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "anysurrect.h"
#include "audio_files_descr.h"

/*MIDI*/

char *audio_MIDI_surrect()
{
	unsigned short number_tracks;
	EX_STRING("header", "MThd");
	EX_BELONG("magic_number", 0x00000006);
	COND_BESHORT("FileFormat", val<=2);
	number_tracks = 
		READ_BESHORT("NumberTracks");
	SKIP_BESHORT("ticks_per_note");
	
	for (int i=0; i<number_tracks; i++)
	{
		unsigned long	size;
		EX_STRING("track_header", "MTrk");
		size = READ_BELONG("track_size");
		SKIP_STRING("track_body", size);
	}

	return "audio/MIDI";
}

/*RIFF*/

#define RIFF_header ({					\
	EX_STRING("magic", "RIFF");			\
	READ_LELONG("size");				\
})

#define RIFF_chunk(len, LIST...) ({			\
	LIST_STRING("magic", len, LIST);		\
	unsigned long size=READ_LELONG("size");		\
	SKIP_STRING("data", size);			\
})

/*WAV*/

char *audio_WAV_surrect()
{
	unsigned long size;
	any_off_t	offset;
	size=RIFF_header;
	offset = fd_seek(0, SEEK_CUR);
	EX_STRING("wave_header", "WAVE");
	do {
		RIFF_chunk(4, { "fmt ", "data", "fact", "cue ",
				"plst", "LIST", "labl", "ltxt",
				"note", "smpl", "loop", "adtl",
				NULL });
	} while ( (fd_seek(0, SEEK_CUR)-offset)<size );
	return "audio/WAV";
}

/*Mpeg3*/

struct audio_MP3_frame_header {
	uint32_t  
		emphasis:2,
		original:1,
		copyright:1,
		mode_extension:2,
		channel_mode:2,
		private:1,
		padding:1,
		sampling_rate:2,
		bitrate:4,
		protection:1,
		layer:2,
		version:2,
		magic:11;
};

union audio_MP3_frame_header_or_uint32_t {
	struct audio_MP3_frame_header	s;
	uint32_t			u;
};

int bitrate(int version_bits, int layer_bits, int bitrate_bits)
{
	switch(version_bits)
	{
		case 1: return -1;
		case 3:
			switch(layer_bits)
			{
				case 0: return -1;
				case 3:
					switch(bitrate_bits)
					{
						case 0x0: return 0;
						case 0x1: return 32;
						case 0x2: return 64;
						case 0x3: return 96;
						case 0x4: return 128;
						case 0x5: return 160;
						case 0x6: return 192;
						case 0x7: return 224;
						case 0x8: return 256;
						case 0x9: return 288;
						case 0xA: return 320;
						case 0xB: return 352;
						case 0xC: return 384;
						case 0xD: return 416;
						case 0xE: return 448;
						case 0xF: return -2;
					}
				case 2:
					switch(bitrate_bits)
					{
						case 0x0: return 0;
						case 0x1: return 32;
						case 0x2: return 48;
						case 0x3: return 56;
						case 0x4: return 64;
						case 0x5: return 80;
						case 0x6: return 96;
						case 0x7: return 112;
						case 0x8: return 128;
						case 0x9: return 160;
						case 0xA: return 192;
						case 0xB: return 224;
						case 0xC: return 256;
						case 0xD: return 320;
						case 0xE: return 384;
						case 0xF: return -2;
					}
				case 1:
					switch(bitrate_bits)
					{
						case 0x0: return 0;
						case 0x1: return 32;
						case 0x2: return 40;
						case 0x3: return 48;
						case 0x4: return 56;
						case 0x5: return 64;
						case 0x6: return 80;
						case 0x7: return 96;
						case 0x8: return 112;
						case 0x9: return 128;
						case 0xA: return 160;
						case 0xB: return 192;
						case 0xC: return 224;
						case 0xD: return 256;
						case 0xE: return 320;
						case 0xF: return -2;
					}
			}
		case 0:
		case 2:
			switch(layer_bits)
			{
				case 0: return -1;
				case 3:
					switch(bitrate_bits)
					{
						case 0x0: return 0;
						case 0x1: return 32;
						case 0x2: return 48;
						case 0x3: return 56;
						case 0x4: return 64;
						case 0x5: return 80;
						case 0x6: return 96;
						case 0x7: return 112;
						case 0x8: return 128;
						case 0x9: return 144;
						case 0xA: return 160;
						case 0xB: return 176;
						case 0xC: return 192;
						case 0xD: return 224;
						case 0xE: return 256;
						case 0xF: return -2;
					}
				case 2:
				case 1:
					switch(bitrate_bits)
					{
						case 0x0: return 0;
						case 0x1: return 8;
						case 0x2: return 16;
						case 0x3: return 24;
						case 0x4: return 32;
						case 0x5: return 40;
						case 0x6: return 48;
						case 0x7: return 56;
						case 0x8: return 64;
						case 0x9: return 80;
						case 0xA: return 96;
						case 0xB: return 112;
						case 0xC: return 128;
						case 0xD: return 144;
						case 0xE: return 160;
						case 0xF: return -2;
					}
			}
	}
	return -3;
}

int sampling_rate (int version_bits, int sampling_rate_bits)
{
	switch(version_bits)
	{
		case 1: return -1;
		case 3:
			switch(sampling_rate_bits)
			{
				case 3: return -2;
				case 0: return 44100;
				case 1: return 48000;
				case 2: return 32000;
			}
		case 2:
			switch(sampling_rate_bits)
			{
				case 3: return -2;
				case 0: return 22050;
				case 1: return 24000;
				case 2: return 16000;
			}
		case 0:
			switch(sampling_rate_bits)
			{
				case 3: return -2;
				case 0: return 11025;
				case 1: return 12000;
				case 2: return 8000;
			}
	}
	return -3;
}

int samples_per_frame (int version_bits, int layer_bits)
{
	switch(version_bits)
	{
		case 1: return -1;
		case 3:
			switch(layer_bits)
			{
				case 0: return -1;
				case 3: return 384;
				case 2: return 1152;
				case 1: return 1152;
			}
		case 0:
		case 2:
			switch(layer_bits)
			{
				case 0: return -1;
				case 3: return 384;
				case 2: return 1152;
				case 1: return 576;
			}
	}
	return -3;
}

int frame_size (struct audio_MP3_frame_header header)
{
	int SamplesPerFrame =
		samples_per_frame(header.version, header.layer);
	int Bitrate =
		bitrate(header.version, header.layer, header.bitrate);
	int SamplingRate =
		sampling_rate(header.version, header.sampling_rate);

	if (Bitrate==0) return 0;
	if (Bitrate<0 || SamplesPerFrame<0 || SamplingRate<0)
		return -2;

	return SamplesPerFrame/8*Bitrate*1000/SamplingRate +
		header.padding;
}

struct ID3V2_tag_size {
	uint32_t
		s0:7, a0:1,
		s1:7, a1:1,
		s2:7, a2:1,
		s3:7, a3:1;
};

union ID3V2_tag_size_or_uint32_t {
	struct ID3V2_tag_size 	s;
	uint32_t		u;
};

#define ID3V2 ({					\
	EX_STRING("id3_magic", "ID3");			\
	SKIP_LESHORT("version");			\
	SKIP_BYTE("flags");				\
	union ID3V2_tag_size_or_uint32_t size;		\
	size.u = READ_BELONG("size");			\
	struct ID3V2_tag_size tag_size = size.s;	\
	if ( tag_size.a3!=0 ||				\
		tag_size.a2!=0 ||			\
	  	tag_size.a1!=0 ||			\
	  	tag_size.a0!=0 ) 			\
		return ERROR_VALUE;			\
	SKIP_STRING("tag",	(tag_size.s3<<21) +	\
	       			(tag_size.s2<<14) +	\
				(tag_size.s1<< 7) +	\
			 	tag_size.s0	);	\
})

#define MP3_frame_header ({					\
	union audio_MP3_frame_header_or_uint32_t header;	\
	header.u = READ_BELONG("header");			\
	struct audio_MP3_frame_header frame_header =		\
		header.s;					\
	if (frame_header.magic!=0x7ff) return ERROR_VALUE;	\
	int FrameSize = frame_size(frame_header);		\
	if (FrameSize<=0)					\
		return ERROR_VALUE;				\
	FrameSize;						\
})

#define MP3_frame	({					\
	int FrameSize = MP3_frame_header;			\
	SKIP_STRING("frame", FrameSize-4);			\
})

#define ID3V1 ({						\
	EX_STRING("tag_magic", "TAG");				\
	SKIP_STRING("title", 30);				\
	SKIP_STRING("artist", 30);				\
	SKIP_STRING("album", 30);				\
	SKIP_STRING("year",  4);				\
	SKIP_STRING("comment", 30);				\
	SKIP_BYTE("genre");					\
})


FUNCOVER(ID3V1_func, ID3V1);

#define ID3V1_in_frame ({					\
	int FrameSize = MP3_frame_header;			\
	for(int i=0; i<(FrameSize-4-255); i++)			\
	{							\
		if ( MAYBE( ID3V1_func() )!=ERROR_VALUE )	\
			break;					\
		SKIP_BYTE("seek_tag_begining");			\
	}							\
})

FUNCOVER(ID3V2_func, 		ID3V2);
FUNCOVER(MP3_frame_func, 	MP3_frame);
FUNCOVER(ID3V1_in_frame_func, 	ID3V1_in_frame);

char *audio_MP3_surrect()
{
	int last_res;
	any_off_t last_offset=0;
	
	MAYBE( ID3V2_func() );
	for (int i=0; i<5; i++)
		MP3_frame;
	do {
		any_off_t pre_offset = fd_seek(0, SEEK_CUR);
		last_res=MAYBE( MP3_frame_func() );
		if (last_res!=ERROR_VALUE)
			last_offset = pre_offset;
	} while	(last_res!=ERROR_VALUE);

	any_off_t offset = fd_seek(0, SEEK_CUR);
	
	if ( MAYBE(ID3V1_func())!=ERROR_VALUE )
		return "audio/MPEG3";
	
	if ( MAYBE(ID3V1_in_frame_func())!=ERROR_VALUE )
		return "audio/MPEG3";

	fd_seek(last_offset, SEEK_SET);
	
	if ( MAYBE(ID3V1_in_frame_func())!=ERROR_VALUE )
		return "audio/MPEG3";
	
	fd_seek(offset, SEEK_SET);
	
	return "audio/MPEG3";
}
