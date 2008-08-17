/*
 *	quick_time_based_files_descr.c
 *      CopyRight (C) 2008, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#define _LARGEFILE64_SOURCE

#define DEBUG
//#define DEBUG2

#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "anysurrect.h"
#include "quick_time_based_files_descr.h"

extern char *concat_strings(int n, ...);

/*Quick Time Based file formats*/

#ifdef DEBUG
struct unknown_box {
	uint32_t type;
	char container[5];
};

struct unknown_box unknown_boxes[1024];
int num_unknown_boxes;
#endif

struct ftyp_box {
	uint32_t major_brand;
	uint32_t minor_version;
	uint32_t *compatible_brands;
	int	 num_compatible_brands;
};

struct ftyp_box ftyp_box;

#define IS_ASCII_ALNUMSPC(a)						\
	( ((a) >= 'a' && (a) <= 'z') || ((a) >= 'A' && (a) <= 'Z') ||   \
	  ((a) >= '0' && (a) <= '9') || (a) == ' ' ) 

#define IS_TYPE_ASCII_ALNUMSPC(type) ({					\
	char *type_str = (char *) &type;				\
	( IS_ASCII_ALNUMSPC(type_str[3]) && IS_ASCII_ALNUMSPC(type_str[2]) &&   \
	  IS_ASCII_ALNUMSPC(type_str[1]) && IS_ASCII_ALNUMSPC(type_str[3]) );	\
})

#define STRING_TO_BELONG(a,b,c,d) 					\
	(0x1000000*a + 0x10000*b + 0x100*c + 0x1*d)   

#define FREE_MAGIC STRING_TO_BELONG('f', 'r', 'e', 'e')
#define SKIP_MAGIC STRING_TO_BELONG('s', 'k', 'i', 'p')

int num_sound_only_boxes;
int num_video_only_boxes;

/*The sinf container*/
int num_boxes_in_sinf;
int num_frma_in_sinf;
int num_imif_in_sinf;
int num_schm_in_sinf;
int num_schi_in_sinf;

#define FRMA_MAGIC STRING_TO_BELONG('f', 'r', 'm', 'a')
#define IMIF_MAGIC STRING_TO_BELONG('i', 'm', 'i', 'f')
#define SCHM_MAGIC STRING_TO_BELONG('s', 'c', 'h', 'm')
#define SCHI_MAGIC STRING_TO_BELONG('s', 'c', 'h', 'i')

int sinf_box_level_3()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n4. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_sinf = %d\n", num_boxes_in_sinf);
	}
#endif

	switch (type)
	{
		case FRMA_MAGIC:
			if (num_frma_in_sinf) return ERROR_VALUE;
			SKIP_STRING("Original Format Box", size - offset);
			num_frma_in_sinf++;
			break;
		case IMIF_MAGIC:
			if (num_imif_in_sinf) return ERROR_VALUE;
			SKIP_STRING("IPMP Info Box", size - offset);
			num_imif_in_sinf++;
			break;
		case SCHM_MAGIC:
			if (num_schm_in_sinf) return ERROR_VALUE;
			SKIP_STRING("Scheme Type Box", size - offset);
			num_schm_in_sinf++;
			break;
		case SCHI_MAGIC:
			if (num_schi_in_sinf) return ERROR_VALUE;
			SKIP_STRING("Scheme Information Box", size - offset);
			num_schi_in_sinf++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "sinf");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_sinf++;

	return !ERROR_VALUE;
}

int sinf_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_sinf = 0;
	num_frma_in_sinf = 0; /*Maybe Exactly one*/
	num_imif_in_sinf = 0; /*Maybe Exactly one*/
	num_schm_in_sinf = 0; /*Maybe Exactly one*/
	num_schi_in_sinf = 0; /*Must be Zero or one*/

	while ( MAYBE( sinf_box_level_3() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	//if (!num_tkhd_in_sinf) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}

/*The ipro container*/
int num_boxes_in_ipro;
int num_sinf_in_ipro;

#define SINF_MAGIC STRING_TO_BELONG('s', 'i', 'n', 'f')

int ipro_box_level_2()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n3. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_ipro = %d\n", num_boxes_in_ipro);
	}
#endif

	switch (type)
	{
		case SINF_MAGIC:
			if (num_sinf_in_ipro) return ERROR_VALUE;
			/*Protection Scheme Information Box*/
			if (sinf_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_sinf_in_ipro++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "ipro");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_ipro++;

	return !ERROR_VALUE;
}

int ipro_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_ipro = 0;
	num_sinf_in_ipro = 0; /*Must be Exactly one*/

	while ( MAYBE( ipro_box_level_2() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	if (!num_sinf_in_ipro) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}

/*The meta container*/
int num_boxes_in_meta;
int num_hdlr_in_meta;
int num_dinf_in_meta;
int num_ipmc_in_meta;
int num_iloc_in_meta;
int num_ipro_in_meta;
int num_iinf_in_meta;
int num_xml_in_meta;
int num_bxml_in_meta;
int num_pitm_in_meta;

#define HDLR_MAGIC STRING_TO_BELONG('h', 'd', 'l', 'r')
#define DINF_MAGIC STRING_TO_BELONG('d', 'i', 'n', 'f')
#define IPMC_MAGIC STRING_TO_BELONG('i', 'p', 'm', 'c')
#define ILOC_MAGIC STRING_TO_BELONG('i', 'l', 'o', 'c')
#define IPRO_MAGIC STRING_TO_BELONG('i', 'p', 'r', 'o')
#define IINF_MAGIC STRING_TO_BELONG('i', 'i', 'n', 'f')
#define  XML_MAGIC STRING_TO_BELONG('x', 'm', 'l', ' ')
#define BXML_MAGIC STRING_TO_BELONG('b', 'x', 'm', 'l')
#define PITM_MAGIC STRING_TO_BELONG('p', 'i', 't', 'm')

int dinf_box(uint64_t size);

int meta_box_level_1()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n3. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_meta = %d\n", num_boxes_in_meta);
	}
#endif

	switch (type)
	{
		case HDLR_MAGIC:
			if (num_hdlr_in_meta) return ERROR_VALUE;
			SKIP_STRING("Handler Reference Box", size - offset);
			num_hdlr_in_meta++;
			break;
		case DINF_MAGIC:
			if (num_dinf_in_meta) return ERROR_VALUE;
			/*Data Information Box*/
			if (dinf_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_dinf_in_meta++;
			break;
		case IPMC_MAGIC:
			if (num_ipmc_in_meta) return ERROR_VALUE;
			SKIP_STRING("IPMP Control Box", size - offset);
			num_ipmc_in_meta++;
			break;
		case ILOC_MAGIC:
			if (num_iloc_in_meta) return ERROR_VALUE;
			SKIP_STRING("Item Location Box", size - offset);
			num_iloc_in_meta++;
			break;
		case IPRO_MAGIC:
			if (num_ipro_in_meta) return ERROR_VALUE;
			/*Item Protection Box*/
			if (ipro_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_ipro_in_meta++;
			break;
		case IINF_MAGIC:
			if (num_iinf_in_meta) return ERROR_VALUE;
			SKIP_STRING("Item Information Box", size - offset);
			num_iinf_in_meta++;
			break;
		case XML_MAGIC:
			if (num_xml_in_meta || num_bxml_in_meta) return ERROR_VALUE;
			SKIP_STRING("XML Box", size - offset);
			num_xml_in_meta++;
			break;
		case BXML_MAGIC:
			if (num_xml_in_meta || num_bxml_in_meta) return ERROR_VALUE;
			SKIP_STRING("Binary XML Box", size - offset);
			num_bxml_in_meta++;
			break;
		case PITM_MAGIC:
			if (num_pitm_in_meta) return ERROR_VALUE;
			SKIP_STRING("Primary Item Box", size - offset);
			num_pitm_in_meta++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "meta");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_meta++;

	return !ERROR_VALUE;
}

int meta_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_meta = 0;
	num_hdlr_in_meta = 0; /*Must be Exactly one*/
	num_dinf_in_meta = 0; /*May be one*/
	num_ipmc_in_meta = 0; /*Must be Zero or one*/
	num_iloc_in_meta = 0; /*Must be Zero or one*/
	num_ipro_in_meta = 0; /*Must be Zero or one*/
	num_iinf_in_meta = 0; /*Must be Zero or one*/
	num_xml_in_meta  = num_bxml_in_meta = 0; /*Must be Zero or one*/
	num_pitm_in_meta = 0; /*Must be Zero or one*/

	while ( MAYBE( meta_box_level_1() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	if (!num_hdlr_in_meta) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}

/*The mfra container*/
int num_boxes_in_mfra;
int num_tfra_in_mfra;
int num_mfro_in_mfra;

#define TFRA_MAGIC STRING_TO_BELONG('t', 'f', 'r', 'a')
#define MFRO_MAGIC STRING_TO_BELONG('m', 'f', 'r', 'o')

int mfra_box_level_1()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n3. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_mfra = %d\n", num_boxes_in_mfra);
	}
#endif

	switch (type)
	{
		case TFRA_MAGIC:
			SKIP_STRING("Movie Fragment Random Access Box", size - offset);
			num_tfra_in_mfra++;
			break;
		case MFRO_MAGIC:
			if (num_mfro_in_mfra) return ERROR_VALUE;
			SKIP_STRING("Track Extends Box", size - offset);
			num_mfro_in_mfra++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "mfra");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_mfra++;

	return !ERROR_VALUE;
}

int mfra_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_mfra = 0;
	num_tfra_in_mfra = 0; /*Must be One or more*/
	num_mfro_in_mfra = 0; /*Must be Exactly one*/

	while ( MAYBE( mfra_box_level_1() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	if (!num_mfro_in_mfra) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}

/*The mvex container*/
int num_boxes_in_mvex;
int num_mehd_in_mvex;
int num_trex_in_mvex;

#define MEHD_MAGIC STRING_TO_BELONG('m', 'e', 'h', 'd')
#define TREX_MAGIC STRING_TO_BELONG('t', 'r', 'e', 'x')

int mvex_box_level_2()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n3. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_mvex = %d\n", num_boxes_in_mvex);
	}
#endif

	switch (type)
	{
		case MEHD_MAGIC:
			if (num_boxes_in_mvex) return ERROR_VALUE;
			if (num_mehd_in_mvex) return ERROR_VALUE;
			SKIP_STRING("Movie Extends Header Box", size - offset);
			num_mehd_in_mvex++;
			break;
		case TREX_MAGIC:
			SKIP_STRING("Track Extends Box", size - offset);
			num_trex_in_mvex++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "mvex");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_mvex++;

	return !ERROR_VALUE;
}

int mvex_box(uint64_t size, int num_trak)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_mvex = 0;
	num_mehd_in_mvex = 0; /*Must be Zero or one*/
	num_trex_in_mvex = 0; /*Must be Exactly one per track in the Movie Box */

	while ( MAYBE( mvex_box_level_2() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	if (!num_trex_in_mvex) return ERROR_VALUE;
	if (num_trex_in_mvex != num_trak) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}

/*The stbl container*/
int num_boxes_in_stbl;
int num_stsd_in_stbl;
int num_stts_in_stbl;
int num_ctts_in_stbl;
int num_stsc_in_stbl;
int num_stsz_in_stbl;
int num_stz2_in_stbl;
int num_stco_in_stbl;
int num_co64_in_stbl;
int num_stss_in_stbl;
int num_stsh_in_stbl;
int num_padb_in_stbl;
int num_stdp_in_stbl;
int num_sdtp_in_stbl;
int num_sbgp_in_stbl;
int num_sgpd_in_stbl;
int num_subs_in_stbl;

#define STSD_MAGIC STRING_TO_BELONG('s', 't', 's', 'd')
#define STTS_MAGIC STRING_TO_BELONG('s', 't', 't', 's')
#define CTTS_MAGIC STRING_TO_BELONG('c', 't', 't', 's')
#define STSC_MAGIC STRING_TO_BELONG('s', 't', 's', 'c')
#define STSZ_MAGIC STRING_TO_BELONG('s', 't', 's', 'z')
#define STZ2_MAGIC STRING_TO_BELONG('s', 't', 'z', '2')
#define STCO_MAGIC STRING_TO_BELONG('s', 't', 'c', 'o')
#define CO64_MAGIC STRING_TO_BELONG('c', 'o', '6', '4')
#define STSS_MAGIC STRING_TO_BELONG('s', 't', 's', 's')
#define STSH_MAGIC STRING_TO_BELONG('s', 't', 's', 'h')
#define PADB_MAGIC STRING_TO_BELONG('p', 'a', 'd', 'b')
#define STDP_MAGIC STRING_TO_BELONG('s', 't', 'd', 'p')
#define SDTP_MAGIC STRING_TO_BELONG('s', 'd', 't', 'p')
#define SBGP_MAGIC STRING_TO_BELONG('s', 'b', 'g', 'p')
#define SGPD_MAGIC STRING_TO_BELONG('s', 'g', 'p', 'd')
#define SUBS_MAGIC STRING_TO_BELONG('s', 'u', 'b', 's')

int stbl_box_level_5()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n6. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_stbl = %d\n", num_boxes_in_stbl);
	}
#endif

	switch (type)
	{
		case STSD_MAGIC:
			if (num_boxes_in_stbl) return ERROR_VALUE;
			if (num_stsd_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Sample Description Box", size - offset);
			num_stsd_in_stbl++;
			break;
		case STTS_MAGIC:
			if (num_stts_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Decoding Time to Sample Box", size - offset);
			num_stts_in_stbl++;
			break;
		case CTTS_MAGIC:
			if (num_ctts_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Composition Time to Sample Box", size - offset);
			num_ctts_in_stbl++;
			break;
		case STSC_MAGIC:
			if (num_stsc_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Sample To Chunk Box", size - offset);
			num_stsc_in_stbl++;
			break;
		case STSZ_MAGIC:
			if (num_stsz_in_stbl || num_stz2_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Sample Size Box", size - offset);
			num_stsz_in_stbl++;
			break;
		case STZ2_MAGIC:
			if (num_stsz_in_stbl || num_stz2_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Compact Sample Size Box", size - offset);
			num_stz2_in_stbl++;
			break;
		case STCO_MAGIC:
			if (num_stco_in_stbl || num_co64_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Chunk Offset Box", size - offset);
			num_stco_in_stbl++;
			break;
		case CO64_MAGIC:
			if (num_stco_in_stbl || num_co64_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Chunk Large Offset Box", size - offset);
			num_co64_in_stbl++;
			break;
		case STSS_MAGIC:
			if (num_stss_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Sync Sample Box", size - offset);
			num_stss_in_stbl++;
			break;
		case STSH_MAGIC:
			if (num_stsh_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Shadow Sync Sample Box", size - offset);
			num_stsh_in_stbl++;
			break;
		case PADB_MAGIC:
			if (num_padb_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Padding Bits Box", size - offset);
			num_padb_in_stbl++;
			break;
		case STDP_MAGIC:
			if (num_stdp_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Degradation Priority Box", size - offset);
			num_stdp_in_stbl++;
			break;
		case SDTP_MAGIC:
			if (num_sdtp_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Sample Dependency Type Box", size - offset);
			num_sdtp_in_stbl++;
			break;
		case SBGP_MAGIC:
			SKIP_STRING("Sample To Group Box", size - offset);
			num_sbgp_in_stbl++;
			break;
		case SGPD_MAGIC:
			SKIP_STRING("Sample Group Description Box", size - offset);
			num_sgpd_in_stbl++;
			break;
		case SUBS_MAGIC:
			if (num_subs_in_stbl) return ERROR_VALUE;
			SKIP_STRING("Sub-Sample Information Box", size - offset);
			num_subs_in_stbl++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "stbl");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_stbl++;

	return !ERROR_VALUE;
}

int stbl_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_stbl = 0;
	num_stsd_in_stbl = 0; /*Must be Exactly one*/
	num_stts_in_stbl = 0; /*Must be Exactly one*/
	num_ctts_in_stbl = 0; /*Must be Zero or one*/
	num_stsc_in_stbl = 0; /*Must be Exactly one*/
	num_stsz_in_stbl = num_stz2_in_stbl = 0; /*Must be Exactly one variant*/
	num_stco_in_stbl = num_co64_in_stbl = 0; /*Must be Exactly one variant*/
	num_stss_in_stbl = 0; /*Must be Zero or one*/
	num_stsh_in_stbl = 0; /*Must be Zero or one*/
	num_padb_in_stbl = 0; /*Must be Zero or one*/
	num_stdp_in_stbl = 0; /*Must be Zero or one*/
	num_sdtp_in_stbl = 0; /*Must be Zero or one*/
	num_sbgp_in_stbl = 0; /*Must be Zero or more*/
	num_sgpd_in_stbl = 0; /*Must be Zero or more and ==num_sbgp*/
	num_subs_in_stbl = 0; /*Must be Zero or one*/

	while ( MAYBE( stbl_box_level_5() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	if (!num_stsd_in_stbl) return ERROR_VALUE;
	if (!num_stts_in_stbl) return ERROR_VALUE;
	if (!num_stsc_in_stbl) return ERROR_VALUE;
	if (!(num_stsz_in_stbl + num_stz2_in_stbl)) return ERROR_VALUE;
	if (!(num_stco_in_stbl + num_co64_in_stbl)) return ERROR_VALUE;
	if (num_sbgp_in_stbl!=num_sgpd_in_stbl) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}

/*The dinf container*/
int num_boxes_in_dinf;
int num_dref_in_dinf;

#define DREF_MAGIC STRING_TO_BELONG('d', 'r', 'e', 'f')

int dinf_box_level_5()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n6. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_dinf = %d\n", num_boxes_in_dinf);
	}
#endif

	switch (type)
	{
		case DREF_MAGIC:
			if (num_dref_in_dinf) return ERROR_VALUE;
			SKIP_STRING("Data Reference Box", size - offset);
			num_dref_in_dinf++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "dinf");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_dinf++;

	return !ERROR_VALUE;
}

int dinf_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_dinf = 0;
	num_dref_in_dinf = 0; /*Must be Exactly one*/

	while ( MAYBE( dinf_box_level_5() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	if (!num_dref_in_dinf) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}


/*The minf container*/
int num_boxes_in_minf;
int num_media_headers_in_minf;
int num_vmhd_in_minf;
int num_smhd_in_minf;
int num_hmhd_in_minf;
int num_nmhd_in_minf;
int num_dinf_in_minf;
int num_stbl_in_minf;
int num_hdlr_in_minf;
int num_gmhd_in_minf;

#define VMHD_MAGIC STRING_TO_BELONG('v', 'm', 'h', 'd')
#define SMHD_MAGIC STRING_TO_BELONG('s', 'm', 'h', 'd')
#define HMHD_MAGIC STRING_TO_BELONG('h', 'm', 'h', 'd')
#define NMHD_MAGIC STRING_TO_BELONG('n', 'm', 'h', 'd')
//#define DINF_MAGIC STRING_TO_BELONG('d', 'i', 'n', 'f')
#define STBL_MAGIC STRING_TO_BELONG('s', 't', 'b', 'l')
#define GMHD_MAGIC STRING_TO_BELONG('g', 'm', 'h', 'd')

int minf_box_level_4()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n5. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_minf = %d\n", num_boxes_in_minf);
	}
#endif

	switch (type)
	{
		case VMHD_MAGIC:
			if (num_boxes_in_minf) return ERROR_VALUE;
			if (num_vmhd_in_minf) return ERROR_VALUE;
			SKIP_STRING("Video Media Header Box", size - offset);
			num_vmhd_in_minf++;
			num_media_headers_in_minf++;
			num_video_only_boxes++;
			break;
		case SMHD_MAGIC:
			if (num_boxes_in_minf) return ERROR_VALUE;
			if (num_smhd_in_minf) return ERROR_VALUE;
			SKIP_STRING("Sound Media Header Box", size - offset);
			num_smhd_in_minf++;
			num_media_headers_in_minf++;
			num_sound_only_boxes++;
			break;
		case HMHD_MAGIC:
			if (num_boxes_in_minf) return ERROR_VALUE;
			if (num_hmhd_in_minf) return ERROR_VALUE;
			SKIP_STRING("Hint Media Header Box", size - offset);
			num_hmhd_in_minf++;
			num_media_headers_in_minf++;
			break;
		case NMHD_MAGIC:
			if (num_boxes_in_minf) return ERROR_VALUE;
			if (num_nmhd_in_minf) return ERROR_VALUE;
			SKIP_STRING("Null Media Header Box", size - offset);
			num_nmhd_in_minf++;
			num_media_headers_in_minf++;
			break;
		case GMHD_MAGIC:
			if (num_boxes_in_minf) return ERROR_VALUE;
			if (num_gmhd_in_minf) return ERROR_VALUE;
			SKIP_STRING("GMHD Box (QuickTime)", size - offset);
			num_gmhd_in_minf++;
			num_media_headers_in_minf++;
			break;
		case DINF_MAGIC:
			if (num_dinf_in_minf) return ERROR_VALUE;
			/*Data Information Box*/
			if (dinf_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_dinf_in_minf++;
			break;
		case STBL_MAGIC:
			if (num_stbl_in_minf) return ERROR_VALUE;
			/*Sample Table Box*/
			if (stbl_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_stbl_in_minf++;
			break;
		case HDLR_MAGIC:
			if (num_hdlr_in_minf) return ERROR_VALUE;
			SKIP_STRING("Handler Reference Box (QuickTime)", size - offset);
			num_hdlr_in_minf++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "minf");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_minf++;

	return !ERROR_VALUE;
}

int minf_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_minf = 0;
	num_media_headers_in_minf = 0; /*Must be Exactly one*/
	num_vmhd_in_minf = 0; /*Must be Zero or one*/
	num_smhd_in_minf = 0; /*Must be Zero or one*/
	num_hmhd_in_minf = 0; /*Must be Zero or one*/
	num_nmhd_in_minf = 0; /*Must be Zero or one*/
	num_dinf_in_minf = 0; /*Must be Exactly one*/
	num_stbl_in_minf = 0; /*Must be Exactly one*/
	num_hdlr_in_minf = 0;
	num_gmhd_in_minf = 0;

	while ( MAYBE( minf_box_level_4() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	if (!num_media_headers_in_minf) return ERROR_VALUE;
	if (!num_dinf_in_minf) return ERROR_VALUE;
	if (!num_stbl_in_minf) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}

/*The mdia container*/
int num_boxes_in_mdia;
int num_mdhd_in_mdia;
int num_hdlr_in_mdia;
int num_minf_in_mdia;

#define MDHD_MAGIC STRING_TO_BELONG('m', 'd', 'h', 'd')
//#define HDLR_MAGIC STRING_TO_BELONG('h', 'd', 'l', 'r')
#define MINF_MAGIC STRING_TO_BELONG('m', 'i', 'n', 'f')

int mdia_box_level_3()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n4. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_mdia = %d\n", num_boxes_in_mdia);
	}
#endif

	switch (type)
	{
		case MDHD_MAGIC:
			if (num_boxes_in_mdia) return ERROR_VALUE;
			if (num_mdhd_in_mdia) return ERROR_VALUE;
			SKIP_STRING("Media Header Box", size - offset);
			num_mdhd_in_mdia++;
			break;
		case HDLR_MAGIC:
			if (num_hdlr_in_mdia) return ERROR_VALUE;
			SKIP_STRING("Handler Reference Box", size - offset);
			num_hdlr_in_mdia++;
			break;
		case MINF_MAGIC:
			if (num_minf_in_mdia) return ERROR_VALUE;
			/*Media Information Box*/
			if (minf_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_minf_in_mdia++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "mdia");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_mdia++;

	return !ERROR_VALUE;
}

int mdia_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_mdia = 0;
	num_mdhd_in_mdia = 0; /*Must be Exactly one*/
	num_hdlr_in_mdia = 0; /*Must be Exactly one*/
	num_minf_in_mdia = 0; /*Must be Exactly one*/

	while ( MAYBE( mdia_box_level_3() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	if (!num_mdhd_in_mdia) return ERROR_VALUE;
	if (!num_hdlr_in_mdia) return ERROR_VALUE;
	if (!num_minf_in_mdia) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}

/*The edts container*/
int num_boxes_in_edts;
int num_elst_in_edts;

#define ELST_MAGIC STRING_TO_BELONG('e', 'l', 's', 't')

int edts_box_level_3()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n4. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_edts = %d\n", num_boxes_in_edts);
	}
#endif

	switch (type)
	{
		case ELST_MAGIC:
			if (num_elst_in_edts) return ERROR_VALUE;
			SKIP_STRING("Edit List Box", size - offset);
			num_elst_in_edts++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "edts");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_edts++;

	return !ERROR_VALUE;
}

int edts_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_edts = 0;
	num_elst_in_edts = 0; /*Must be Zero or one*/

	while ( MAYBE( edts_box_level_3() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}


/*The trak container*/
int num_boxes_in_trak;
int num_tkhd_in_trak;
int num_tref_in_trak;
int num_edts_in_trak;
int num_mdia_in_trak;

#define TKHD_MAGIC STRING_TO_BELONG('t', 'k', 'h', 'd')
#define TREF_MAGIC STRING_TO_BELONG('t', 'r', 'e', 'f')
#define EDTS_MAGIC STRING_TO_BELONG('e', 'd', 't', 's')
#define MDIA_MAGIC STRING_TO_BELONG('m', 'd', 'i', 'a')

int trak_box_level_2()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n3. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_trak = %d\n", num_boxes_in_trak);
	}
#endif

	switch (type)
	{
		case TKHD_MAGIC:
			if (num_boxes_in_trak) return ERROR_VALUE;
			if (num_tkhd_in_trak) return ERROR_VALUE;
			SKIP_STRING("Track Header Box", size - offset);
			num_tkhd_in_trak++;
			break;
		case TREF_MAGIC:
			if (num_tref_in_trak) return ERROR_VALUE;
			SKIP_STRING("Track Reference Box", size - offset);
			num_tref_in_trak++;
			break;
		case EDTS_MAGIC:
			if (num_edts_in_trak) return ERROR_VALUE;
			/*Edit Box*/
			if (edts_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_edts_in_trak++;
			break;
		case MDIA_MAGIC:
			if (num_mdia_in_trak) return ERROR_VALUE;
			/*Media Box*/
			if (mdia_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_mdia_in_trak++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "trak");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_trak++;

	return !ERROR_VALUE;
}

int trak_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_trak = 0;
	num_tkhd_in_trak = 0; /*Must be Exactly one*/
	num_tref_in_trak = 0; /*Must be Zero or one*/
	num_edts_in_trak = 0; /*Must be Zero or one*/
	num_mdia_in_trak = 0; /*Must be Exactly one*/

	while ( MAYBE( trak_box_level_2() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	if (!num_tkhd_in_trak) return ERROR_VALUE;
	if (!num_mdia_in_trak) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}

/*The traf container*/
int num_boxes_in_traf;
int num_tfhd_in_traf;
int num_trun_in_traf;
int num_sdtp_in_traf;
int num_sbgp_in_traf;
int num_subs_in_traf;

#define TFHD_MAGIC STRING_TO_BELONG('t', 'f', 'h', 'd')
#define TRUN_MAGIC STRING_TO_BELONG('t', 'r', 'u', 'n')

int traf_box_level_2()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n3. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_traf = %d\n", num_boxes_in_traf);
	}
#endif

	switch (type)
	{
		case TFHD_MAGIC:
			if (num_boxes_in_traf) return ERROR_VALUE;
			if (num_tfhd_in_traf) return ERROR_VALUE;
			SKIP_STRING("Track Fragment Header Box", size - offset);
			num_tfhd_in_traf++;
			break;
		case TRUN_MAGIC:
			SKIP_STRING("Track Fragment Run Box", size - offset);
			num_trun_in_traf++;
			break;
		case SDTP_MAGIC:
			if (num_sdtp_in_traf) return ERROR_VALUE;
			SKIP_STRING("Sample Dependency Type Box", size - offset);
			num_sdtp_in_traf++;
			break;
		case SBGP_MAGIC:
			SKIP_STRING("Sample To Group Box", size - offset);
			num_sbgp_in_traf++;
			break;
		case SUBS_MAGIC:
			if (num_subs_in_traf) return ERROR_VALUE;
			SKIP_STRING("Sub-Sample Information Box", size - offset);
			num_subs_in_traf++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "traf");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_traf++;

	return !ERROR_VALUE;
}

int traf_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_traf = 0;
	num_tfhd_in_traf = 0; /*Must be Exactly one*/
	num_trun_in_traf = 0; /*Must be Zero or more*/
	num_sdtp_in_traf = 0; /*Must be Zero or one*/
	num_sbgp_in_traf = 0; /*Must be Zero or more*/
	num_subs_in_traf = 0; /*Must be Zero or one*/

	while ( MAYBE( traf_box_level_2() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	if (!num_tfhd_in_traf) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}

/*The moof container*/
int num_boxes_in_moof;
int num_mfhd_in_moof;
int num_traf_in_moof;

#define MFHD_MAGIC STRING_TO_BELONG('m', 'f', 'h', 'd')
#define TRAF_MAGIC STRING_TO_BELONG('t', 'r', 'a', 'f')

int moof_box_level_1()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n2. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_moof = %d\n", num_boxes_in_moof);
	}
#endif

	switch (type)
	{
		case MFHD_MAGIC:
			if (num_boxes_in_moof) return ERROR_VALUE;
			if (num_mfhd_in_moof) return ERROR_VALUE;
			SKIP_STRING("Movie Fragment Header Box", size - offset);
			num_mfhd_in_moof++;
			break;
		case TRAF_MAGIC:
			/*Track Fragment Box*/
			if (traf_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_traf_in_moof++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "moof");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_moof++;

	return !ERROR_VALUE;
}

int moof_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_moof = 0;
	num_mfhd_in_moof = 0; /*Must be Exactly one*/
	num_traf_in_moof = 0; /*Must be Zero or more*/

	while ( MAYBE( moof_box_level_1() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	if (!num_mfhd_in_moof) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}

/*The moov container*/
int num_boxes_in_moov;
int num_mvhd_in_moov;
int num_trak_in_moov;
int num_mvex_in_moov;
int num_ipmc_in_moov;

int num_iods_in_moov;
int num_udta_in_moov;

#define MVHD_MAGIC STRING_TO_BELONG('m', 'v', 'h', 'd')
#define TRAK_MAGIC STRING_TO_BELONG('t', 'r', 'a', 'k')
#define MVEX_MAGIC STRING_TO_BELONG('m', 'v', 'e', 'x')
//#define IPMC_MAGIC STRING_TO_BELONG('i', 'p', 'm', 'c')
#define IODS_MAGIC STRING_TO_BELONG('i', 'o', 'd', 's')
#define UDTA_MAGIC STRING_TO_BELONG('u', 'd', 't', 'a')

int moov_box_level_1()
{
	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			size = READ_BELONG64("size");
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n2. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes_in_moov = %d\n", num_boxes_in_moov);
	}
#endif

	switch (type)
	{
		case MVHD_MAGIC:
			if (num_boxes_in_moov) return ERROR_VALUE;
			if (num_mvhd_in_moov) return ERROR_VALUE;
			SKIP_STRING("Movie Header Box", size - offset);
			num_mvhd_in_moov++;
			break;
		case TRAK_MAGIC:
			/*Track Box*/
			if (trak_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_trak_in_moov++;
			break;
		case MVEX_MAGIC:
			/*Movie Extends Box*/
			if (num_mvex_in_moov) return ERROR_VALUE;
			if (mvex_box(size - offset, num_trak_in_moov) == ERROR_VALUE) return ERROR_VALUE;
			num_mvex_in_moov++;
			break;
		case IPMC_MAGIC:
			if (num_ipmc_in_moov) return ERROR_VALUE;
			SKIP_STRING("IPMP Control Box", size - offset);
			num_ipmc_in_moov++;
			break;
		case IODS_MAGIC:
			if (num_iods_in_moov) return ERROR_VALUE;
			SKIP_STRING("IODS Box (MPEG4)", size - offset);
			num_iods_in_moov++;
			break;
		case UDTA_MAGIC:
			if (num_udta_in_moov) return ERROR_VALUE;
			SKIP_STRING("UDTA Box (MPEG4)", size - offset);
			num_udta_in_moov++;
			break;
		case FREE_MAGIC:
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "moov");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes_in_moov++;

	return !ERROR_VALUE;
}

int moov_box(uint64_t size)
{
	any_off_t start_offset, end_offset;
	start_offset = fd_seek(0, SEEK_CUR);

	num_boxes_in_moov = 0;
	num_mvhd_in_moov = 0; /*Must be Exactly one*/
	num_trak_in_moov = 0; /*Must be One or more*/
	num_mvex_in_moov = 0; /*Must be Zero or one*/
	num_ipmc_in_moov = 0; /*Must be Zero or one*/
	num_iods_in_moov = 0;
	num_udta_in_moov = 0;

	while ( MAYBE( moov_box_level_1() )!=ERROR_VALUE )
	{
		end_offset = fd_seek(0, SEEK_CUR);
		if ( (end_offset - start_offset) >= size ) break;
	};

	if (!num_mvhd_in_moov) return ERROR_VALUE;
	if (!num_trak_in_moov) return ERROR_VALUE;

	end_offset = fd_seek(0, SEEK_CUR);
	if ( (end_offset - start_offset) != size )
		return ERROR_VALUE;
	return !ERROR_VALUE;
}

/*The file container*/
#define CLIP_MAGIC STRING_TO_BELONG('c', 'l', 'i', 'p')
#define CRGN_MAGIC STRING_TO_BELONG('c', 'r', 'g', 'n')
#define MATT_MAGIC STRING_TO_BELONG('m', 'a', 't', 't')
#define KMAT_MAGIC STRING_TO_BELONG('k', 'm', 'a', 't')
#define PNOT_MAGIC STRING_TO_BELONG('p', 'n', 'o', 't')
#define CTAB_MAGIC STRING_TO_BELONG('c', 't', 'a', 'b')
#define LOAD_MAGIC STRING_TO_BELONG('l', 'o', 'a', 'd')
#define IMAP_MAGIC STRING_TO_BELONG('i', 'm', 'a', 'p')
#define PICT_MAGIC STRING_TO_BELONG('P', 'I', 'C', 'T')

#define FTYP_MAGIC STRING_TO_BELONG('f', 't', 'y', 'p')
#define PDIN_MAGIC STRING_TO_BELONG('p', 'd', 'i', 'n')
#define MOOV_MAGIC STRING_TO_BELONG('m', 'o', 'o', 'v')
#define MOOF_MAGIC STRING_TO_BELONG('m', 'o', 'o', 'f')
#define MFRA_MAGIC STRING_TO_BELONG('m', 'f', 'r', 'a')
#define MDAT_MAGIC STRING_TO_BELONG('m', 'd', 'a', 't')
#define META_MAGIC STRING_TO_BELONG('m', 'e', 't', 'a')
#define WIDE_MAGIC STRING_TO_BELONG('w', 'i', 'd', 'e')

int num_boxes;

/*QuickTime only boxes*/
int num_clip;
int num_crgn;
int num_matt;
int num_kmat;
int num_pnot;
int num_ctab;
int num_load;
int num_imap;

int num_pict;

/*ISO Base Media boxes*/
int num_ftyp;
int num_pdin;
int num_moov;
int num_moof;
int num_mfra;
int num_mdat;
int num_free;
int num_skip;
int num_meta;
int num_wide;


int box_level_0()
{
	uint64_t l_size, h_size;

	any_off_t offset = 0;
	uint64_t size = READ_BELONG("box_size");
	uint32_t type = READ_BELONG("box_type");
	offset += 8;
	switch (size)
	{
		case 0:
			return ERROR_VALUE;
		case 1:
			h_size = READ_BELONG("size_high");
			l_size = READ_BELONG("size_low");
			size = (h_size << 32) | l_size;
			offset += 8;
	}
	
#ifdef	DEBUG2
	{
		char *type_str = (char *) &type;
		fprintf(stderr, "\n1. Box type '%c%c%c%c'\n", 
				type_str[3], type_str[2],
				type_str[1], type_str[0]);
		fprintf(stderr, "Box size: %lld\n", size);
		fprintf(stderr, "num_boxes = %d\n", num_boxes);
	}
#endif

	switch (type)
	{
		/*QuickTime only boxes*/
		case CLIP_MAGIC:
			SKIP_STRING("", size - offset);
			num_clip++;
			break;
		case CRGN_MAGIC:
			SKIP_STRING("", size - offset);
			num_crgn++;
			break;
		case MATT_MAGIC:
			SKIP_STRING("", size - offset);
			num_matt++;
			break;
		case KMAT_MAGIC:
			SKIP_STRING("", size - offset);
			num_kmat++;
			break;
		case PNOT_MAGIC:
			SKIP_STRING("", size - offset);
			num_pnot++;
			break;
		case CTAB_MAGIC:
			SKIP_STRING("", size - offset);
			num_ctab++;
			break;
		case LOAD_MAGIC:
			SKIP_STRING("", size - offset);
			num_load++;
			break;
		case IMAP_MAGIC:
			SKIP_STRING("", size - offset);
			num_imap++;
			break;
		case PICT_MAGIC:
			SKIP_STRING("", size - offset);
			num_pict++;
			break;
		/*ISO Base Media boxes*/
		case FTYP_MAGIC:
			if (num_boxes) return ERROR_VALUE;
			if (num_ftyp) return ERROR_VALUE;
			if (size > 1024) return ERROR_VALUE;
			ftyp_box.major_brand = READ_BELONG("major_brand");
			ftyp_box.minor_version = READ_BELONG("minor_version");
			offset += 8;
			if (size - offset)
			{
				ftyp_box.compatible_brands = malloc(size - offset);
				if ( !ftyp_box.compatible_brands )
				{
					fprintf(stderr, _("Not enough memory\n"));
					exit(1);
				}
				fd_read(ftyp_box.compatible_brands, size - offset);
			}
			ftyp_box.num_compatible_brands = (size - offset)>>2;
			num_ftyp++;
			break;
		case PDIN_MAGIC:
			if (num_pdin) return ERROR_VALUE;
			SKIP_STRING("Progressive Download Information", size - offset);
			num_pdin++;
			break;
		case MOOV_MAGIC:
			if (num_moov) return ERROR_VALUE;
			if (moov_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_moov++;
			break;
		case MOOF_MAGIC:
			/*Movie Fragment Box*/
			if (moof_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_moof++;
			break;
		case MFRA_MAGIC:
			if (num_mfra) return ERROR_VALUE;
			/*Movie Fragment Random Access Box*/
			if (mfra_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_mfra++;
			break;
		case MDAT_MAGIC:
			SKIP_STRING("Media Data", size - offset);
			num_mdat++;
			break;
		case FREE_MAGIC:
			SKIP_STRING("Free space", size - offset);
			num_free++;
			break;
		case SKIP_MAGIC:
			SKIP_STRING("Free space", size - offset);
			num_skip++;
			break;
		case META_MAGIC:
			if (num_meta) return ERROR_VALUE;
			/*The Meta Box*/
			if (meta_box(size - offset) == ERROR_VALUE) return ERROR_VALUE;
			num_meta++;
			break;
		case WIDE_MAGIC:
			SKIP_STRING("WIDE box", size - offset);
			num_wide++;
			break;
		default:
			if ( IS_TYPE_ASCII_ALNUMSPC(type) )
			{
#ifdef	DEBUG
				if (num_unknown_boxes < 1024)
				{
					unknown_boxes[num_unknown_boxes].type = type;
					strcpy(unknown_boxes[num_unknown_boxes].container, "file");
					num_unknown_boxes++;
				}
#endif
				SKIP_STRING("UNKNOWN box", size - offset);
			}
			else return ERROR_VALUE;
	}

	num_boxes++;

	return !ERROR_VALUE;
}

extern _declspec(dllexport)
char *quick_time_based_surrect()
{
	int not_iso_based_media_file = 0;

	num_unknown_boxes = 0;

	num_boxes = 0;

	ftyp_box.num_compatible_brands = 0;
	ftyp_box.compatible_brands = NULL;

	num_sound_only_boxes = 0;
	num_video_only_boxes = 0;

	/*QuickTime only boxes*/
	num_clip = 0;
	num_crgn = 0;
	num_matt = 0;
	num_kmat = 0;
	num_pnot = 0;
	num_ctab = 0;
	num_load = 0;
	num_imap = 0;
	num_pict = 0;

	/*ISO Base Media boxes*/
	num_ftyp = 0; /*Must be Exactly one*/
	num_pdin = 0; /*Must be Exactly one or zero*/
	num_moov = 0; /*Must be Exactly one*/
	num_moof = 0; /*Maybe any number*/
	num_mfra = 0; /*May be Exactly one*/
	num_mdat = 0; /*Maybe any number*/
	num_free = 0; /*Maybe any number*/
	num_skip = 0; /*Maybe any number*/
	num_meta = 0; /*Must be Zero or one*/
	num_wide = 0;

	while ( MAYBE( box_level_0() )!=ERROR_VALUE );

	//fprintf(stderr, "num_boxes=%d\n", num_boxes);

	if (!num_boxes) 
	{
		if (ftyp_box.compatible_brands) free(ftyp_box.compatible_brands);
		return ERROR_VALUE;
	}

	if (num_pnot) not_iso_based_media_file++;
	if (num_clip) not_iso_based_media_file++;
	if (num_crgn) not_iso_based_media_file++;
	if (num_matt) not_iso_based_media_file++;
	if (num_kmat) not_iso_based_media_file++;
	if (num_pnot) not_iso_based_media_file++;
	if (num_ctab) not_iso_based_media_file++;
	if (num_load) not_iso_based_media_file++;
	if (num_imap) not_iso_based_media_file++;

	if (!num_ftyp) not_iso_based_media_file++;
	if (!num_moov) 
	{
		if (ftyp_box.compatible_brands) free(ftyp_box.compatible_brands);
		return ERROR_VALUE;//not_iso_based_media_file++;
	}

	char *Root_Directory = "Quick_Time_Based";
	char *Subdirectory = "";

	char *VA_Directory = "";
	if (num_sound_only_boxes && num_video_only_boxes)
	{
		VA_Directory = "/Video_and_Audio";
	}
	else if (!num_sound_only_boxes && num_video_only_boxes)
	{
		VA_Directory = "/Video_Only";
	}
	else if (num_sound_only_boxes && !num_video_only_boxes)
	{
		VA_Directory = "/Audio_Only";
	}

	if (!not_iso_based_media_file)
	{
		Subdirectory = "/ISO_Base_Media_File";
	}

	char File_Format_Directory[6] = "/";

	if (num_ftyp && IS_TYPE_ASCII_ALNUMSPC(ftyp_box.major_brand))
	{
		char *brand_str = (char*) &ftyp_box.major_brand;
		int i;

		File_Format_Directory[0] = '/';

		for (i=1; i<=4 && (brand_str[4-i] != ' '); i++)
		{
			File_Format_Directory[i] = brand_str[4-i];
		}
		File_Format_Directory[i] = '\0';
	}

	if (ftyp_box.compatible_brands) free(ftyp_box.compatible_brands);

	if (strcmp(File_Format_Directory, "/") == 0)
		File_Format_Directory[0] = '\0';

	static char *ReturnPath = NULL;
	if (ReturnPath) free(ReturnPath);

	ReturnPath = concat_strings(4, 
			Root_Directory, Subdirectory, 
			VA_Directory, File_Format_Directory);

#ifdef	DEBUG
	{
		int i;
		for (i=0; i<num_unknown_boxes; i++)
		{
			char *type_str = (char *) &unknown_boxes[i].type;
			fprintf(stderr, "Unknown box type '%c%c%c%c' in %s container\n", 
					type_str[3], type_str[2],
					type_str[1], type_str[0],
					unknown_boxes[i].container);
		}
		if (num_unknown_boxes > 0)
		{
			fprintf(stderr, "Please, mail %d lines above to undefer@gmail.com\n",
					num_unknown_boxes); 
		}
	}
#endif

	return ReturnPath;
}

