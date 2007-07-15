/*
 *	archieve_files_descr.c
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
#include "config.h"
#include "archieve_files_descr.h"

/*TAR*/
struct tar_header_block
{                               /* byte offset */
	char name[100];               /*   0 */
	char mode[8];                 /* 100 */
	char uid[8];                  /* 108 */
	char gid[8];                  /* 116 */
	char size[12];                /* 124 */
	char mtime[12];               /* 136 */
	char chksum[8];               /* 148 */
	char typeflag;                /* 156 */
	char linkname[100];           /* 157 */
	char magic[6];                /* 257 */
	char version[2];              /* 263 */
	char uname[32];               /* 265 */
	char gname[32];               /* 297 */
	char devmajor[8];             /* 329 */
	char devminor[8];             /* 337 */
	char prefix[155];             /* 345 */
	char tail[12];   /* 500 */
};

#define TAR_BLOCK ({						\
	struct tar_header_block header;				\
	int res = fd_read(&header, 512);				\
	if (!res) return ERROR_VALUE;				\
	header.magic[5] = '\0';					\
	if (strcmp(header.magic, "ustar")!=0)			\
		return ERROR_VALUE;				\
	char *endptr;						\
	long long int size = strtoll(header.size, &endptr, 8);	\
	if (*endptr) return ERROR_VALUE;			\
	SKIP_STRING("data", (size+511)/512*512);		\
})

FUNCOVER(tar_block, TAR_BLOCK);

extern _declspec(dllexport)
char *archieve_TAR_surrect()
{
	TAR_BLOCK;
	while ( MAYBE( tar_block() )!=ERROR_VALUE );

	return "archieve/TAR";
}


/*ZIP*/
int zip_local_file_tail( uint32_t size )
{
	EX_BELONG("magic", 0x504b0708);
	SKIP_LELONG("crc");
	EX_LELONG("size", size);
	SKIP_LELONG("usize");

	return !ERROR_VALUE;
}

#define ZIP_LOCAL_FILE ({						\
	EX_BELONG("magic", 0x504b0304);					\
	SKIP_LESHORT("version_for_extract");				\
	uint16_t gpbits = READ_LESHORT("gpbits");			\
	SKIP_LESHORT("method");						\
	SKIP_LESHORT("time");						\
	SKIP_LESHORT("date");						\
	SKIP_LELONG("crc");						\
	uint32_t size = READ_LELONG("size");				\
	SKIP_LELONG("usize");						\
	uint16_t namelen = READ_LESHORT("namelen");			\
	uint16_t extralen = READ_LESHORT("extralen");			\
	SKIP_STRING("filename", namelen);				\
	SKIP_STRING("extrafield", extralen);				\
	if ( !(gpbits&8) ) { SKIP_STRING("data", size); }		\
	else 								\
	{								\
		size = 0;						\
		do {							\
			if ( MAYBE( zip_local_file_tail(size) )!=	\
							ERROR_VALUE )	\
				break;					\
			size++;						\
			SKIP_BYTE();					\
		} while (size<=MAX_SIZE_IN_ZIP);			\
		if (size>MAX_SIZE_IN_ZIP) {return ERROR_VALUE;}		\
	}								\
})

#define ZIP_CD_FILE_HEADER ({						\
	EX_BELONG("magic", 0x504b0102);					\
	SKIP_LESHORT("version");					\
	SKIP_LESHORT("version_for_extract");				\
	SKIP_LESHORT("gpbits");						\
	SKIP_LESHORT("method");						\
	SKIP_LESHORT("time");						\
	SKIP_LESHORT("date");						\
	SKIP_LELONG("crc");						\
	SKIP_LELONG("size");						\
	SKIP_LELONG("usize");						\
	uint16_t namelen = READ_LESHORT("namelen");			\
	uint16_t extralen = READ_LESHORT("extralen");			\
	uint16_t commentlen = READ_LESHORT("commentlen");		\
	SKIP_LESHORT("disknumber");					\
	SKIP_LESHORT("inattr");						\
	SKIP_LELONG("outattr");						\
	SKIP_LELONG("offset");						\
	SKIP_STRING("filename", namelen);				\
	SKIP_STRING("extrafield", extralen);				\
	SKIP_STRING("comment", commentlen);				\
})

#define ZIP_CD_END_RECORD ({						\
	EX_BELONG("magic", 0x504b0506);					\
	SKIP_LESHORT("disknumber");					\
	SKIP_LESHORT("cd_disknumber");					\
	SKIP_LESHORT("disk_numentries");				\
	SKIP_LESHORT("numentries");					\
	SKIP_LELONG("cd_size");						\
	SKIP_LELONG("cd_offset");					\
	uint16_t commentlen = READ_LESHORT("commentlen");		\
	SKIP_STRING("comment", commentlen);				\
})

FUNCOVER(zip_local_file, ZIP_LOCAL_FILE);

extern _declspec(dllexport)
char *archieve_ZIP_surrect()
{
	long num_files=0;
	while( MAYBE( zip_local_file() )!=ERROR_VALUE ) num_files++;
	for (; num_files; num_files--)
		ZIP_CD_FILE_HEADER;

	ZIP_CD_END_RECORD;
	
	return "archieve/ZIP";
}

/*RAR*/
#define RAR_BLOCK ({							\
	SKIP_LESHORT("crc");						\
	uint8_t type = COND_BYTE("type", val>=0x72 && val<=0x7F);	\
	uint16_t flags = READ_LESHORT("flags");				\
	uint16_t size = READ_LESHORT("size");				\
	uint32_t add_size=0;						\
	if (flags & 0x8000 && type != 0x76)				\
		add_size = READ_LELONG("add_size") - 4;			\
	SKIP_STRING("data", add_size + size - 7);			\
})

FUNCOVER(rar_block, RAR_BLOCK);

extern _declspec(dllexport)
char *archieve_RAR_surrect()
{
	EX_LESHORT("crc", 0x6152);
	EX_BYTE("type", 0x72);
	EX_LESHORT("flags", 0x1a21);
	EX_LESHORT("size", 0x0007);
	
	while( MAYBE( rar_block() )!=ERROR_VALUE );
	
	return "archieve/RAR";
}
