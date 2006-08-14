/*
 *	image3_files_descr.c
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
#include "any.h"
#include "image3_files_descr.h"

/*TIFF*/
#define ASSIGN_AR3(array, type, count, offset) ({		\
	array[0] = type;					\
	array[1] = count;					\
	array[2] = offset;					\
})

#define COPY_AR3(src, dest) ({					\
	dest[0] = src[0];					\
	dest[1] = src[1];					\
	dest[2] = src[2];					\
})

#define TIFF(XE) ({							\
	EX_##XE##SHORT("magic", 42);					 \
	uint32_t ifd_offset = COND_##XE##LONG("first_ifd_offset", val>=8);  \
									 \
	while (1)							 \
	{								 \
		fd_seek(ifd_offset, SEEK_SET);				 \
		uint16_t ifd_entries =					 \
			READ_##XE##SHORT("count of ifd entries");	 \
		uint16_t i;						 \
									 \
		uint32_t strip_offsets[3] 	= {0, 0, 0};		 \
		uint32_t strip_byte_counts[3] 	= {0, 0, 0};		 \
		uint32_t free_offsets[3] 	= {0, 0, 0};		 \
		uint32_t free_byte_counts[3] 	= {0, 0, 0};		 \
 \
		for (i=0; i<ifd_entries; i++)				 \
		{							 \
			uint16_t tag = READ_##XE##SHORT("tag");		 \
			uint16_t type = READ_##XE##SHORT("type");	 \
			uint32_t count = READ_##XE##LONG("count");	 \
			any_off_t offset_next_ifd_entry =			 \
				fd_seek(0, SEEK_CUR) + 4;		 \
 \
			uint32_t value_size;				 \
			switch (type)					 \
			{						 \
				case 1:						 \
				case 2:						 \
				case 6:						 \
				case 7:						 \
					value_size = 1;				 \
					break;					 \
				case 3:						 \
				case 8:						 \
					value_size = 2;				 \
					break;					 \
				case 4:						 \
				case 9:						 \
				case 11:					 \
					value_size = 4;				 \
					break;					 \
				case 5:						 \
				case 10:					 \
				case 12:					 \
					value_size = 8;				 \
					break;					 \
				default:					 \
					return ERROR_VALUE;			 \
			};						 \
 \
			uint32_t offset;				 \
 \
			int is_offset = ( (value_size * count) > 4 );	 \
			if (is_offset)					 \
			{						 \
				offset = COND_##XE##LONG("offset", 	 \
						val>=8);		 \
				size = max_t (uint32_t, size,		 \
						offset + value_size * count);	 \
			}						 \
			else offset = fd_seek(0, SEEK_CUR);		 \
 \
			switch(tag)					 \
			{						 \
				case 273:				 \
					if ( !(type==3 || type==4) )	 \
						return ERROR_VALUE;	 \
					if ( strip_offsets[0] )		 \
						return ERROR_VALUE;	 \
					ASSIGN_AR3( strip_offsets, 		 \
							type, count, offset );	 \
					break;				 \
 \
				case 279:				 \
					if ( !(type==3 || type==4) )	 \
						return ERROR_VALUE;	 \
					if ( strip_byte_counts[0] )	 \
						return ERROR_VALUE;	 \
					ASSIGN_AR3( strip_byte_counts,		 \
							type, count, offset );	 \
					break;				 \
 \
				case 288:				 \
					if ( !(type==4) )		 \
						return ERROR_VALUE;	 \
					if ( free_offsets[0] )		 \
						return ERROR_VALUE;	 \
					ASSIGN_AR3( free_offsets,			 \
							type, count, offset );	 \
					break;				 \
				case 289:				 \
					if ( !(type==4) )		 \
						return ERROR_VALUE;	 \
					if ( free_byte_counts[0] )	 \
						return ERROR_VALUE;	 \
					ASSIGN_AR3( free_byte_counts,		 \
							type, count, offset );	 \
					break;				 \
			}						 \
 \
			uint32_t offsets[3] 	= {0, 0, 0};		 \
			uint32_t byte_counts[3] = {0, 0, 0};		 \
 \
			if ( strip_offsets[0] && strip_byte_counts[0] )	 \
			{						 \
				COPY_AR3 (strip_offsets, offsets);	 \
				COPY_AR3 (strip_byte_counts, byte_counts); \
 \
				ASSIGN_AR3 (strip_offsets, 0, 0, 0);	 \
				ASSIGN_AR3 (strip_byte_counts, 0, 0, 0); \
			}						 \
			\
			if ( free_offsets[0] && free_byte_counts[0] )	 \
			{						 \
				COPY_AR3 (free_offsets, offsets);	 \
				COPY_AR3 (free_byte_counts, byte_counts); \
				\
				ASSIGN_AR3 (free_offsets, 0, 0, 0);	 \
				ASSIGN_AR3 (free_byte_counts, 0, 0, 0);	 \
			}						 \
			\
			if ( offsets[0] && byte_counts[0] )		 \
			{						 \
				if ( offsets[1] != byte_counts[1] )	 \
				return ERROR_VALUE;		 \
				\
				uint32_t o_offsets = offsets[2];	 \
				uint32_t o_byte_counts = byte_counts[2];	 \
				uint32_t i;				 \
				for ( i=0; i<offsets[1]; i++ )		 \
				{					 \
					uint32_t offset = 0;		 \
					uint32_t byte_count = 0;		 \
					\
					fd_seek(o_offsets, SEEK_SET);	 \
					switch (offsets[0])		 \
					{				 \
						case 3:			 \
									 offset =	 \
						READ_##XE##SHORT(	 \
								"offset");	 \
						case 4:			 \
									 offset =	 \
						READ_##XE##LONG(	 \
								"offset");	 \
					}				 \
					o_offsets = fd_seek(0, 		 \
							SEEK_CUR);	 \
					\
					fd_seek(o_byte_counts, 		 \
							SEEK_SET);	 \
					switch (byte_counts[0])		 \
					{				 \
						case 3:			 \
									 byte_count =	 \
						READ_##XE##SHORT(	 \
								"offset");	 \
						case 4:			 \
									 byte_count =	 \
						READ_##XE##LONG(	 \
								"offset");	 \
					}				 \
					o_byte_counts = fd_seek(0,	 \
							SEEK_CUR);	 \
					\
					size = max_t (uint32_t, size,	 \
							offset + byte_count);	 \
				}					 \
			}						 \
			\
			fd_seek(offset_next_ifd_entry, SEEK_SET);	 \
		}							 \
		\
		ifd_offset = READ_##XE##LONG("next_ifd_offset"); \
		size = max_t(uint32_t, size, fd_seek(0, SEEK_CUR));	 \
		if (!ifd_offset) break; \
	} \
})									\

char *image_TIFF_surrect()
{
#define ERROR_VALUE	0
	int res;

	int big_endian;
	
	char endian_string[2];
	fd_read(endian_string, 2);
	if (endian_string[0]!=endian_string[1]) return ERROR_VALUE;

	if (endian_string[0]=='I') big_endian=0;
	else if (endian_string[0]=='M') big_endian=1;
	else return ERROR_VALUE;

	uint32_t size = 8;

	if (big_endian) TIFF(BE);
	else	TIFF(LE);

	fd_seek(size, SEEK_SET);
	
	return "image/TIFF";
#undef	ERROR_VALUE
}
