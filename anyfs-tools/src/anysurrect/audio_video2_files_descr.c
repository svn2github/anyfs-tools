/*
 *	audio_video2_files_descr.c
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "anysurrect.h"
#include "audio_video2_files_descr.h"

/*MPEG 1/2*/
#include <mpeg2.h>
#include "any.h"

extern _declspec(dllexport)
char *audio_video_MPEG12_surrect()
{
	COND_BELONG("magic", (val&0xFFFFFF00)==0x00000100);
	fd_seek(0, SEEK_SET);

	char buffer[512];

	mpeg2dec_t * decoder;
	const mpeg2_info_t * info;
	const mpeg2_sequence_t * sequence;
	mpeg2_state_t state;
	any_size_t size;
	int good_end = 0;

	decoder = mpeg2_init ();
	if (decoder == NULL) {
		fprintf (stderr, "Could not allocate a decoder object.\n");
		exit (1);
	}
	info = mpeg2_info (decoder);

	size = (size_t)-1;
	do {
		state = mpeg2_parse (decoder);
		sequence = info->sequence;
		switch (state) {
			case STATE_BUFFER:
				size = fd_read (buffer, 512);
				mpeg2_buffer (decoder, buffer, buffer + size);
				break;
			case STATE_END:
			case STATE_INVALID_END:
				size = 0;
				good_end = 1;
				break;
			case STATE_INVALID:
				size = 0;
				break;
			default:
				if ( (int) state==-1 ) size = 0;
				break;
		}
	} while (size);

	mpeg2_close (decoder);

	if (!good_end) return ERROR_VALUE;

	return "audio_video/MPEG1-2_stream";
}
