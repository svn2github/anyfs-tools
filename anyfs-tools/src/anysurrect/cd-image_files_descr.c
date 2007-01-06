/*
 *	cd-image_files_descr.c
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
#include "config.h"
#include "cd-image_files_descr.h"

/*ISO9660 CD Image*/

#if 0
inline int d_character(char a) {
	return (a>='A' && a<='Z') || (a>='0' && a<='9') || (a=='_')
}

inline int a_character(char a) {
	return (a>='A' && a<='Z') || (a=='_') || 
		(a>=' ' && a<='"') || (a>='%' && a<='?');
}
#endif

#define PVD ({						\
})

#define READ_MIRROR_LONG(name) ({				\
	uint32_t le = READ_LELONG(name);			\
	uint32_t be = READ_BELONG(name);			\
	if (le!=be) return NULL;				\
	le;							\
})

#define READ_MIRROR_SHORT(name) ({				\
	uint16_t le = READ_LESHORT(name);			\
	uint16_t be = READ_BESHORT(name);			\
	if (le!=be) return NULL;				\
	le;							\
})

#define DIRECTORY_RECORD(name, flen, ploc, plen) ({		\
	uint8_t reclen = READ_BYTE("Length Of Directory Record");\
	uint32_t loc=0, len=0;					\
	if (reclen) {						\
		SKIP_BYTE("Extended Attribute Record Length");	\
		loc = READ_MIRROR_LONG("Location of Extent");	\
		len = READ_MIRROR_LONG("Data Length");		\
		if (flen>=34) {					\
			reclen=flen;				\
			SKIP_STRING("data", reclen-18);		\
		}						\
		else {						\
			SKIP_STRING("data", 14);		\
			uint8_t len_fi = READ_BYTE("Length of File Identifier"); \
			int pad = (len_fi%2)?0:1;		\
			SKIP_STRING("File Identifier", len_fi);	\
			if ( pad ) SKIP_BYTE("Padding Field"); \
			if ( reclen < (33 + len_fi + pad) ) return NULL; \
			SKIP_STRING("data", reclen - (33 + len_fi + pad) ); \
		}						\
	}							\
	if (ploc) *((uint32_t*)ploc) = loc;			\
	if (plen) *((uint32_t*)plen) = len;			\
})

#define PATH_TABLE_RECORD(name) ({				\
	uint8_t len_di = READ_BYTE("Length of Directory Identifier");\
	SKIP_BYTE("Extended Attribute Record Length");		\
	uint32_t loc = READ_LELONG("Location of Extent");	\
	SKIP_LESHORT("Parent Directory Number");		\
	SKIP_STRING("Directory Identifier", len_di);		\
	SKIP_STRING("Padding Field", len_di & 1);		\
	loc;							\
})

#define BLOCK_ALIGNED_SIZE(size) ( ( (size)+blocksize-1 )/blocksize*blocksize )

char *cd_image_ISO_surrect()
{
	int i;

	any_size_t image_size = 32768 + 2048;

	SKIP_STRING("32 Kb", 32768);
	EX_BYTE("Volume Descritor Type", 1);
	EX_STRING("Standart Identifier", "CD001");
	EX_BYTE("Volume Descritor Version", 1);
	EX_BYTE("Unused Field", 0);
	SKIP_STRING("System Identifier", 32);
	SKIP_STRING("Volume Identifier", 32);
	for (i=0; i<2; i++)
		EX_BELONG("Unused Field", 0);
	READ_MIRROR_LONG("Volume Space Size");
	for (i=0; i<8; i++)
		EX_BELONG("Unused Field", 0);
	READ_MIRROR_SHORT("Volume Set Size");
	READ_MIRROR_SHORT("Volume Sequence Number");
	uint16_t blocksize = READ_MIRROR_SHORT("Logical Block Size");
	uint32_t pt_size = READ_MIRROR_LONG("Path Table Size");
	
	uint32_t le_pt_loc = READ_LELONG("Location of Type L Path Table");
	SKIP_LELONG("Location of Optional Type L Path Table");
	uint32_t be_pt_loc = READ_BELONG("Location of Type M Path Table");
	SKIP_BELONG("Location of Optional Type M Path Table");

	image_size = max_t( any_size_t, image_size, le_pt_loc*blocksize + 
			BLOCK_ALIGNED_SIZE(pt_size) );
	image_size = max_t( any_size_t, image_size, be_pt_loc*blocksize + 
			BLOCK_ALIGNED_SIZE(pt_size) );

#ifdef	DEBUG
	printf ("\n blocksize = %u, size=%lu, pt_size = %lu, le_pt=%lu, be_pt=%lu\n",
			blocksize, image_size, pt_size, le_pt_loc, be_pt_loc);
#endif
	
	uint32_t RDR_loc, RDR_len;
	DIRECTORY_RECORD("Root Directory Record", 34, &RDR_loc, &RDR_len);
#ifdef	DEBUG
	printf ("RDR_loc=%lu, RDR_len=%lu\n", RDR_loc, RDR_len);
#endif

	image_size = max_t( any_size_t, image_size, RDR_loc*blocksize +
			BLOCK_ALIGNED_SIZE(RDR_len) );
	
	SKIP_STRING("Volume Set Identifier", 128);
	SKIP_STRING("Publisher Identifier", 128);
	SKIP_STRING("Data Preparer Identifier", 128);
	SKIP_STRING("Application Identifier", 128);
	
	SKIP_STRING("Copyright File Identifier", 37);
	SKIP_STRING("Abstract File Identifier", 37);
	SKIP_STRING("Bibliographic File Identifier", 37);
	
	SKIP_STRING("Volume Creation Date and Time", 17);
	SKIP_STRING("Volume Modification Date and Time", 17);
	SKIP_STRING("Volume Expiration Date and Time", 17);
	SKIP_STRING("Volume Effective Date and Time", 17);

	EX_BYTE("File Structure Version", 1);
	EX_BYTE("Reserved for future standartization", 0);
	
	SKIP_STRING("Application Use", 512);

	for (i=0; i<163; i++)
		EX_BELONG("Reserved for future standartization", 0);
	EX_BYTE("Reserved for future standartization", 0);

	fd_seek(le_pt_loc*blocksize, SEEK_SET);

	while ( (fd_seek(0, SEEK_CUR) - le_pt_loc*blocksize)<pt_size )
	{
		uint32_t ext_loc = PATH_TABLE_RECORD("Path Table Record");
		any_off_t offset = fd_seek(0, SEEK_CUR);

		fd_seek(ext_loc*blocksize, SEEK_SET);
		
		uint32_t dir_loc, dir_len;
		DIRECTORY_RECORD("Directory Record", 0, &dir_loc, &dir_len);
		
		image_size = max_t( any_size_t, image_size, dir_loc*blocksize +
				BLOCK_ALIGNED_SIZE(dir_len) );

		if (dir_loc!=ext_loc) return NULL;

		while ( (fd_seek(0, SEEK_CUR) - ext_loc*blocksize)<dir_len )
		{
			uint32_t file_loc, file_len;
			DIRECTORY_RECORD("Directory Record", 0, &file_loc, &file_len);

			if (!file_loc && !file_len) break;

			image_size = max_t( any_size_t, image_size, file_loc*blocksize +
					BLOCK_ALIGNED_SIZE(file_len) );
		}
		
		if ( (fd_seek(0, SEEK_CUR) - ext_loc*blocksize)>dir_len )
			return NULL;
		
		fd_seek(offset, SEEK_SET);
	}
	
	if ( (fd_seek(0, SEEK_CUR) - le_pt_loc*blocksize)<pt_size )
		return NULL;

	fd_seek(image_size, SEEK_SET);
	
	return "cd-image/ISO9660";
}

