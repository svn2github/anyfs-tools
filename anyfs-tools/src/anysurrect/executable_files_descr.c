/*
 *	executable_files_descr.c
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
#include "executable_files_descr.h"

/*ELF32*/

char *executable_ELF32_surrect()
{
#define ERROR_VALUE	NULL
	int res;
	EX_STRING("magic", "\x7f""ELF");
	SKIP_STRING("rest_ident", 12);
	SKIP_LESHORT("type");
	SKIP_LESHORT("machine");
	SKIP_LELONG("version");
	SKIP_LELONG("entry");
	uint32_t phoff = READ_LELONG("phoff");
	uint32_t shoff = READ_LELONG("shoff");
	SKIP_LELONG("flags");
	EX_LESHORT("ehsize", 52);
	EX_LESHORT("phentsize", 32);
	uint16_t phnum = READ_LESHORT("phnum");
	EX_LESHORT("shentsize", 40);
	uint16_t shnum = READ_LESHORT("shnum");
	SKIP_LESHORT("shstrndx");

	if (phoff>shoff) return ERROR_VALUE;
	if (shoff<52) return ERROR_VALUE;
	
	uint32_t expect_sectoffset = 52;
	if (phoff) expect_sectoffset = phoff + 32*phnum;

	if (expect_sectoffset>shoff) return ERROR_VALUE;

	SKIP_STRING("jump_to_section_header_table", shoff-52);
	
	int i;
	for (i=0; i<shnum; i++)
	{
#ifdef DEBUG
		printf("i=%d/%d (%d)\n", i, shnum, fd_seek(0, SEEK_CUR));
#endif
		SKIP_LELONG("name");
		uint32_t sect_type = READ_LELONG("type");
		SKIP_LELONG("flags");
		SKIP_LELONG("addr");
		uint32_t sect_offset = READ_LELONG("offset");
		uint32_t sect_size = READ_LELONG("size");
		SKIP_LELONG("link");
		SKIP_LELONG("info");
		uint32_t addralign = READ_LELONG("addralign");
		if (!addralign) addralign=1;
		SKIP_LELONG("entsize");

		if ( addralign>1)
			expect_sectoffset = (expect_sectoffset + 
					addralign-1)/addralign*addralign;

		if (sect_offset)
		{ 
#ifdef DEBUG
			printf("expect=%d, real=%d, size=%d, align=%d, type=%d\n",
					expect_sectoffset, sect_offset, sect_size,
					addralign, sect_type);
#endif
			if ( sect_offset < expect_sectoffset)
				return ERROR_VALUE;
			if ( sect_offset > expect_sectoffset)
			{
				uint32_t align = addralign;
				int s = 0;
				while (align>1) {align>>=1; s++;}
				while (s) {align<<=1; s--;}
				if (align!=addralign) return ERROR_VALUE;

				if ( sect_offset/addralign*addralign != 
						sect_offset )
					return ERROR_VALUE;
			}

			if ( sect_type!=8 )
				expect_sectoffset = sect_offset+sect_size;
		}
	}

	return "executable/ELF32";
#undef	ERROR_VALUE
}
