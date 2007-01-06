/*
 *	audio_video_files_descr.c
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
#include "audio_video_files_descr.h"

/*OGG*/
#define ID3V1 ({						\
	EX_STRING("tag_magic", "TAG");				\
	SKIP_STRING("title", 30);				\
	SKIP_STRING("artist", 30);				\
	SKIP_STRING("album", 30);				\
	SKIP_STRING("year",  4);				\
	SKIP_STRING("comment", 30);				\
	SKIP_BYTE("genre");					\
})

#define OGG_PAGE ({						\
	EX_STRING("magic_string", "OggS");			\
	SKIP_BYTE("version");					\
	SKIP_BYTE("header_type");				\
	SKIP_STRING("granule_position", 8);			\
	SKIP_LELONG("bitstream_serial_number");			\
	SKIP_LELONG("page_sequence_number");			\
	SKIP_LELONG("CRC_checksum");				\
	uint8_t page_segments = READ_BYTE("page_segments");	\
	uint32_t page_body_size = 0;				\
	for (; page_segments; page_segments--)			\
		page_body_size += READ_BYTE("segment_table");	\
	SKIP_STRING("page_body", page_body_size);		\
})

FUNCOVER(ogg_page, OGG_PAGE);
FUNCOVER(id3v1, ID3V1);

char *audio_video_OGG_surrect()
{
	OGG_PAGE;
	while ( MAYBE( ogg_page() )!=ERROR_VALUE );
	MAYBE( id3v1() );

	return "audio_video/OGG";
}


/*AVI*/
#define RIFF_header ({                                  \
	EX_STRING("magic", "RIFF");                     \
	READ_LELONG("size");                            \
})

#define CKID(ckid_type) ({				\
	EX_STRING("ckid_type", ckid_type);		\
	uint32_t chunk_size = READ_LELONG("size");	\
	SKIP_STRING("chunk_data", chunk_size);		\
})

#define CKID_LIST(ckid_type...) ({			\
	LIST_STRING("ckid_type", 4, ckid_type);		\
	uint32_t chunk_size = READ_LELONG("size");	\
	SKIP_STRING("chunk_data", chunk_size);		\
})

FUNCOVER(is_null, EX_BYTE("null", 0));

#define LIST_strl ({						\
	EX_STRING("magic", "LIST");                     	\
	uint32_t list_strl_size = READ_LELONG("size");		\
	any_off_t list_strl_offset = fd_seek(0, SEEK_CUR);		\
	EX_STRING("LIST_TYPE", "strl");				\
								\
	while ( (fd_seek(0, SEEK_CUR) - list_strl_offset) <	\
			list_strl_size )			\
	{							\
		CKID_LIST( { "strh", "strf", "strd", "strn",	\
				"indx", "vprp",			\
				"JUNK", NULL } );		\
		MAYBE(is_null());				\
	}							\
								\
	if ( (fd_seek(0, SEEK_CUR) - list_strl_offset) !=	\
	  	list_strl_size ) return ERROR_VALUE;		\
})

#define LIST_odml ({						\
	EX_STRING("magic", "LIST");                     	\
	uint32_t list_odml_size = READ_LELONG("size");		\
	any_off_t list_odml_offset = fd_seek(0, SEEK_CUR);		\
	EX_STRING("LIST_TYPE", "odml");				\
								\
	CKID("dmlh");						\
								\
	if ( (fd_seek(0, SEEK_CUR) - list_odml_offset) !=	\
	  	list_odml_size ) return ERROR_VALUE;		\
})

FUNCOVER(vedt, CKID("vedt"));
FUNCOVER(junk, CKID("JUNK"));

FUNCOVER(list_strl, LIST_strl);
FUNCOVER(list_odml, LIST_odml);

#define LIST_hdrl ({						\
	EX_STRING("magic", "LIST");                     	\
	uint32_t list_hdrl_size = READ_LELONG("size");		\
	any_off_t list_hdrl_offset = fd_seek(0, SEEK_CUR);		\
	EX_STRING("LIST_TYPE", "hdrl");				\
	CKID("avih");						\
								\
	while ( MAYBE( list_strl() )!=ERROR_VALUE );		\
	MAYBE(vedt());						\
	if ( (fd_seek(0, SEEK_CUR) - list_hdrl_offset) <	\
	  	list_hdrl_size ) MAYBE(junk());			\
	MAYBE( list_odml() );					\
								\
	if ( (fd_seek(0, SEEK_CUR) - list_hdrl_offset) !=	\
	  	list_hdrl_size ) return ERROR_VALUE;		\
})

#define NNCK ({							\
	COND_BYTE("digit1", val>='0' && val<='9');		\
	COND_BYTE("digit2", val>='0' && val<='9');		\
	LIST_STRING( "movi_chunks", 2, 				\
			{ "db", "dc", "pc", "wb", 		\
			  "iv", NULL } );			\
})

#define IXNN ({							\
	LIST_STRING( "movi_chunks", 2, 				\
			{ "ix", NULL } );	\
	COND_BYTE("digit1", val>='0' && val<='9');		\
	COND_BYTE("digit2", val>='0' && val<='9');		\
})

FUNCOVER(nnck, NNCK);
FUNCOVER(ixnn, IXNN);

#define MOVI_CHUNK ({						\
	if ( MAYBE(nnck())==ERROR_VALUE &&			\
	  	MAYBE(ixnn())==ERROR_VALUE )			\
		return ERROR_VALUE;				\
	uint32_t chunk_size = READ_LELONG("size");		\
	SKIP_STRING("chunk_data", chunk_size);			\
	MAYBE(is_null());					\
})

#define AVI_TAG ({						\
	COND_BYTE("1", (val>='0' && val<='9') ||		\
		(val>='A' && val<='Z') );			\
	COND_BYTE("2", (val>='0' && val<='9') ||		\
		(val>='A' && val<='Z') );			\
	COND_BYTE("3", (val>='0' && val<='9') ||		\
		(val>='A' && val<='Z') );			\
	COND_BYTE("4", (val>='0' && val<='9') ||		\
		(val>='A' && val<='Z') );			\
	uint32_t chunk_size = READ_LELONG("size");		\
	SKIP_STRING("chunk_data", chunk_size);			\
})

FUNCOVER(null, EX_BYTE("null", 0));

#define LIST_INFO ({						\
	EX_STRING("LIST", "LIST");				\
	uint32_t list_info_size = READ_LELONG("size");		\
	any_off_t list_info_offset = fd_seek(0, SEEK_CUR);		\
	EX_STRING("LIST_TYPE", "INFO");				\
	CKID("ISFT");						\
	MAYBE(null());						\
	while ( (fd_seek(0, SEEK_CUR) - list_info_offset) < 	\
		list_info_size )				\
	{							\
		AVI_TAG;					\
		MAYBE(null());					\
	}							\
	if ( (fd_seek(0, SEEK_CUR) - list_info_offset + 1) ==	\
	  	list_info_size ) EX_BYTE("null", 0);		\
	if ( (fd_seek(0, SEEK_CUR) - list_info_offset) !=	\
	  	list_info_size ) return ERROR_VALUE;		\
})

#if 0
#define LIST_rec ({						\
	EX_STRING("magic", "LIST");                     	\
	uint32_t list_rec_size = READ_LELONG("size");		\
	any_off_t list_rec_offset = fd_seek(0, SEEK_CUR);		\
	EX_STRING("LIST_TYPE", "rec");				\
								\
	while ( (fd_seek(0, SEEK_CUR) - list_movi_offset) <	\
			list_movi_size )			\
	{							\
		MOVI_CHUNK;					\
	}							\
	if ( (fd_seek(0, SEEK_CUR) - list_movi_offset) !=	\
	  	list_movi_size ) return ERROR_VALUE;		\
})
#endif

#define LIST_rec ({					\
	EX_STRING("LIST", "LIST");			\
	uint32_t chunk_size = READ_LELONG("size");	\
	EX_STRING("LIST_TYPE", "rec ");			\
	SKIP_STRING("chunk_data", chunk_size-4);	\
})

FUNCOVER(list_rec, LIST_rec);

#define LIST_movi ({						\
	EX_STRING("magic", "LIST");                     	\
	uint32_t list_movi_size = READ_LELONG("size");		\
	any_off_t list_movi_offset = fd_seek(0, SEEK_CUR);		\
	EX_STRING("LIST_TYPE", "movi");				\
								\
	while ( MAYBE( list_rec() )!=ERROR_VALUE );		\
								\
	while ( (fd_seek(0, SEEK_CUR) - list_movi_offset) <	\
			list_movi_size )			\
	{							\
		MOVI_CHUNK;					\
	}							\
	if ( (fd_seek(0, SEEK_CUR) - list_movi_offset) !=	\
	  	list_movi_size ) return ERROR_VALUE;		\
})

FUNCOVER(list_info, LIST_INFO);
FUNCOVER(ckid_junk, CKID("JUNK"));
FUNCOVER(idx1, CKID("idx1"));
FUNCOVER(fxtc, CKID("FXTC"));

char *audio_video_AVI_surrect()
{
	uint32_t riff_size = RIFF_header;
	any_off_t riff_offset = fd_seek(0, SEEK_CUR);

	EX_STRING("RIFF_TYPE", "AVI ");

	LIST_hdrl;
	MAYBE(list_info());
	MAYBE(ckid_junk());
	LIST_movi;
	MAYBE( idx1() );
	MAYBE( fxtc() );
	
	if ( (fd_seek(0, SEEK_CUR) - riff_offset) < riff_size )
		MAYBE(ckid_junk());
	
	if ( (fd_seek(0, SEEK_CUR) - riff_offset) != riff_size ) 
		return ERROR_VALUE;

	return "audio_video/AVI";
}

