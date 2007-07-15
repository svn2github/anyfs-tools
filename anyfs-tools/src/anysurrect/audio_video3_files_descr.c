/*
 *	audio_video3_files_descr.c
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
#include "audio_video3_files_descr.h"

/*MPEG 1/2 stream program multiplex*/
#include "any.h"
#include "scan_pes/scan_pes.h"

extern _declspec(dllexport)
char *audio_video_MPEG12PM_surrect()
{
	COND_BELONG("magic", (val&0xFFFFFF00)==0x00000100);
	fd_seek(0, SEEK_SET);

	any_size_t size = scan_pes(0);
	if (!size) return ERROR_VALUE;
	
	fd_seek(size, SEEK_SET);

	return "audio_video/MPEG1-2_stream_program_multiplex";
}
