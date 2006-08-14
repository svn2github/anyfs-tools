/*
 *	image_files_descr.c
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
#include "image_files_descr.h"

/*JPEG*/

#define JPEG_SEGMENT ({						\
	while ( MAYBE( jpeg_ff() ) != ERROR_VALUE );	\
	COND_BYTE("type", (val>=0xC0 && val<=0xCF) || 		\
			(val>=0xDB && val<=0xFE) );		\
	uint16_t size = READ_BESHORT("size");			\
	SKIP_STRING("data", size-2);				\
})

#define JPEG_SEGMENT_IN_DATA ({						\
	while ( MAYBE( jpeg_ff() ) != ERROR_VALUE );		\
	COND_BYTE("type", val<=0x01 || (val>=0xD0 && val<=0xD7) );	\
})

#define JPEG_DATA ({							\
	if ( MAYBE( jpeg_databyte() )==ERROR_VALUE &&		\
		MAYBE( jpeg_segment_in_data() )==ERROR_VALUE )	\
		return ERROR_VALUE;					\
})

char *image_JPEG_surrect()
{
#define ERROR_VALUE	0
	int res;
	FUNCOVER( jpeg_ff, EX_BYTE("magic", 0xFF) );
	FUNCOVER( jpeg_segment, JPEG_SEGMENT );
	
	FUNCOVER( jpeg_databyte, COND_BYTE("data_byte", val!=0xFF) );
	FUNCOVER( jpeg_segment_in_data, JPEG_SEGMENT_IN_DATA );

	FUNCOVER( jpeg_data, JPEG_DATA);
	
	EX_BYTE("magic", 0xFF);
	EX_BYTE("jpeg_magic", 0xD8);
	
	while ( MAYBE( jpeg_ff() ) != ERROR_VALUE );
	while ( MAYBE( jpeg_segment() ) != ERROR_VALUE );
	
	while ( MAYBE( jpeg_ff() ) != ERROR_VALUE );
	EX_BYTE("sos_segment", 0xDA );
	uint16_t size = READ_BESHORT("size");
	SKIP_STRING("data", size-2);

	while ( MAYBE( jpeg_data() ) != ERROR_VALUE );
	
	EX_BYTE("magic", 0xFF);
	EX_BYTE("jpeg_magic_end", 0xD9);

	return "image/JPEG";
#undef	ERROR_VALUE
}

/*PNG*/
#define PNG_CHUNK(TYPES...) ({				\
	uint32_t length = READ_BELONG("length");	\
	LIST_STRING("type", 4, TYPES);			\
	SKIP_STRING("data", length);			\
	SKIP_BELONG("crc");				\
})

char *image_PNG_surrect()
{
#define ERROR_VALUE	0
	int res;
	FUNCOVER( png_chunk, PNG_CHUNK({"PLTE", "IDAT", "bKGD", "cHRM",
				"gAMA", "hIST", "pHYs", "sBIT",
				"tEXt", "tIME", "tRNS", "zTXt", 
				NULL}) );
	
	EX_BELONG("signature_part_1", 0x89504e47);
	EX_BELONG("signature_part_2", 0x0d0a1a0a);
	PNG_CHUNK( {"IHDR", NULL} );
	while ( MAYBE( png_chunk() ) != ERROR_VALUE );
	PNG_CHUNK( {"IEND", NULL} );
	return "image/PNG";
#undef	ERROR_VALUE
}

/*BMP*/

char *image_BMP_surrect()
{
#define ERROR_VALUE	0
	int res;
	EX_STRING("magic", "BM");
	uint32_t size=READ_LELONG("size");
	EX_LESHORT("magic_1", 0);
	EX_LESHORT("magic_2", 0);
	SKIP_LELONG("bitmap_offset");
	SKIP_STRING("data", size-14);
	return "image/BMP";
#undef	ERROR_VALUE
}
