/*
 *	image2_files_descr.c
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
#include "image2_files_descr.h"

/*PNM*/
#define IS_WHITESPACE(val) (val==' ' || val=='\t' || val=='\r' || val=='\n')

#define COMMENT_STRING ({					\
	EX_BYTE("comment sign", '#');				\
	while ( READ_BYTE("wait_eol")!='\n' );			\
})

#define READ_TO_COND(buffer, bufsize, cond) ({			\
	int i;							\
	char val;						\
	for (i=0; i<bufsize; i++)				\
	{							\
		val = buffer[i] = READ_BYTE("char");		\
		if (cond)					\
		{						\
			buffer[i] = '\0';			\
			break;					\
		}						\
	}							\
	if (i>=bufsize) return ERROR_VALUE;			\
})

#define READ_TO_CHAR(buffer, bufsize, chr) 	\
	READ_TO_COND (buffer, bufsize, val == chr )

#define READ_TO_WHITESPACE(buffer, bufsize) 	\
	READ_TO_COND (buffer, bufsize, IS_WHITESPACE(val) )

FUNCOVER(whitespace, COND_BYTE("WhiteSpace", IS_WHITESPACE(val) ));

#define SKIP_WHITESPACES ({					\
	while ( MAYBE(whitespace())!=ERROR_VALUE );		\
})

FUNCOVER(comment, COMMENT_STRING);

#define SKIP_COMMENTS ({					\
	while ( MAYBE(comment())!=ERROR_VALUE );		\
})

extern _declspec(dllexport)
char *image_PNM_surrect()
{
	EX_BYTE("P", 'P');
	uint8_t type = COND_BYTE("TYPE", val>='1' && val<='7');
	EX_BYTE("eol", '\n');
	SKIP_COMMENTS;

	uint32_t width = 0, height = 0;
	uint32_t i, j, d;
	uint8_t depth = 0;
	uint16_t maxval = 0;
	char tupltype[256] = "";
	int plain = 0;

	int bw = 0;

	char *tmp;
	int bufsize=256;
	char buffer[bufsize];

	char *ret = NULL;
	
	int numwidth = 0, numheight = 0, 
	    numdepth = 0, nummaxval = 0;
	
	switch (type)
	{
		case '1':
			plain = 1;
			depth = 1;
			maxval = 1;
			bw = 1;
			
			READ_TO_CHAR(buffer, bufsize, ' ');
			width = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;
			
			READ_TO_CHAR(buffer, bufsize, '\n');
			height = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;

			ret = "image/PNM/Plain_PBM";
			break;
		case '2':
			plain = 1;
			depth = 1;
			
			READ_TO_CHAR(buffer, bufsize, ' ');
			width = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;
			
			READ_TO_CHAR(buffer, bufsize, '\n');
			height = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;

			SKIP_COMMENTS;
			
			READ_TO_CHAR(buffer, bufsize, '\n');
			maxval = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;

			ret = "image/PNM/Plain_PGM";
			break;
		case '3':
			plain = 1;
			depth = 3;
			
			READ_TO_CHAR(buffer, bufsize, ' ');
			width = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;
			
			READ_TO_CHAR(buffer, bufsize, '\n');
			height = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;

			SKIP_COMMENTS;
			
			READ_TO_CHAR(buffer, bufsize, '\n');
			maxval = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;

			ret = "image/PNM/Plain_PPM";
			break;
		case '4':
			depth = 1;
			maxval = 1;

			bw = 1;

			READ_TO_WHITESPACE(buffer, bufsize);
			width = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;

			SKIP_WHITESPACES;
			
			READ_TO_WHITESPACE(buffer, bufsize);
			height = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;

			SKIP_WHITESPACES;
			
			ret = "image/PNM/PBM";
			break;
		case '5':
			depth = 1;
			
			READ_TO_WHITESPACE(buffer, bufsize);
			width = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;

			SKIP_WHITESPACES;
			SKIP_COMMENTS;
			SKIP_WHITESPACES;
			
			READ_TO_WHITESPACE(buffer, bufsize);
			height = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;

			SKIP_WHITESPACES;
			SKIP_COMMENTS;
			SKIP_WHITESPACES;
			
			READ_TO_WHITESPACE(buffer, bufsize);
			maxval = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;

			SKIP_WHITESPACES;

			ret = "image/PNM/PGM";
			break;
		case '6':
			depth = 3;
			
			READ_TO_WHITESPACE(buffer, bufsize);
			width = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;
			
			SKIP_WHITESPACES;
			SKIP_COMMENTS;
			SKIP_WHITESPACES;
			
			READ_TO_WHITESPACE(buffer, bufsize);
			height = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;

			SKIP_WHITESPACES;
			SKIP_COMMENTS;
			SKIP_WHITESPACES;
			
			READ_TO_WHITESPACE(buffer, bufsize);
			maxval = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;
			
			SKIP_WHITESPACES;

			ret = "image/PNM/PPM";
			break;

		case '7':
			while (1)
			{
				READ_TO_WHITESPACE(buffer, bufsize);

				if ( strcmp(buffer, "WIDTH")==0 )
				{
					numwidth++;
					SKIP_WHITESPACES;

					READ_TO_WHITESPACE(buffer, bufsize);
					width = strtol(buffer, &tmp, 10);
					if (*tmp) return ERROR_VALUE;

					SKIP_WHITESPACES;
					SKIP_COMMENTS;
					SKIP_WHITESPACES;
				}

				if ( strcmp(buffer, "HEIGHT")==0 )
				{
					numheight++;
					SKIP_WHITESPACES;

					READ_TO_WHITESPACE(buffer, bufsize);
					height = strtol(buffer, &tmp, 10);
					if (*tmp) return ERROR_VALUE;

					SKIP_WHITESPACES;
					SKIP_COMMENTS;
					SKIP_WHITESPACES;
				}

				if ( strcmp(buffer, "DEPTH")==0 )
				{
					numdepth++;
					SKIP_WHITESPACES;

					READ_TO_WHITESPACE(buffer, bufsize);
					depth = strtol(buffer, &tmp, 10);
					if (*tmp) return ERROR_VALUE;

					SKIP_WHITESPACES;
					SKIP_COMMENTS;
					SKIP_WHITESPACES;
				}

				if ( strcmp(buffer, "MAXVAL")==0 )
				{
					nummaxval++;
					SKIP_WHITESPACES;

					READ_TO_WHITESPACE(buffer, bufsize);
					maxval = strtol(buffer, &tmp, 10);
					if (*tmp) return ERROR_VALUE;

					SKIP_WHITESPACES;
					SKIP_COMMENTS;
					SKIP_WHITESPACES;
				}
				
				if ( strcmp(buffer, "TUPLTYPE")==0 )
				{
					SKIP_WHITESPACES;

					READ_TO_WHITESPACE(buffer, bufsize);

					if ( (strlen(tupltype)+strlen(buffer))>255 ) return ERROR_VALUE;
					strcat(tupltype, buffer);

					SKIP_WHITESPACES;
					SKIP_COMMENTS;
					SKIP_WHITESPACES;
				}
				
				if ( strcmp(buffer, "ENDHDR")==0 )
				{
					SKIP_WHITESPACES;
					SKIP_COMMENTS;
					SKIP_WHITESPACES;

					break;
				}
			}

			if ( numwidth!=1 || numheight!=1 ||
			  	numdepth!=1 || nummaxval!=1 )
				return ERROR_VALUE;

			if ( strcmp(tupltype, "BLACKANDWHITE")==0 )
			{
				if (depth!=1 || maxval!=1) return ERROR_VALUE;
			}
			else
			if ( strcmp(tupltype, "GRAYSCALE")==0 )
			{
				if (depth!=1) return ERROR_VALUE;
			}
			else
			if ( strcmp(tupltype, "RGB")==0 )
			{
				if (depth!=3) return ERROR_VALUE;
			}
			else
			if ( strcmp(tupltype, "GRAYSCALE_ALPHA")==0 )
			{
				if (depth!=2) return ERROR_VALUE;
			}
			else
			if ( strcmp(tupltype, "RGB_ALPHA")==0 )
			{
				if (depth!=4) return ERROR_VALUE;
			}
			else return ERROR_VALUE;

			ret = "image/PNM/PAM";
			break;
	}
			
	if (plain)
	{
		for (j=0; j<height; j++)
		{
			for (i=0; i<width; i++)
			{
				for (d=0; d<depth; d++)
				{
					uint32_t val;
					if (bw)
						val = COND_BYTE("bit", val=='0' || val=='1') - '0';
					else
					{
						READ_TO_WHITESPACE(buffer, bufsize);
						val = strtol(buffer, &tmp, 10);
						if (*tmp) return ERROR_VALUE;

						if (val>maxval) return ERROR_VALUE;
					}

					SKIP_WHITESPACES;
				}
			}
		}
	}
	else
	{
		if (bw) 
		{
			width = (width+7)/8;
			SKIP_STRING("data", width*height);
		}
		else
		{
			for (j=0; j<height; j++)
			{
				for (i=0; i<width; i++)
				{
					for (d=0; d<depth; d++)
					{
						uint32_t val;
						if (maxval<256)
						{
							val = READ_BYTE("tuple");
							if (val>maxval) return ERROR_VALUE;
						}
						else
						{
							val = READ_LESHORT("tuple");
							if (val>maxval) return ERROR_VALUE;
						}
					}
				}
			}
		}

		char *yet;
		any_off_t offset = fd_seek(0, SEEK_CUR);
		while ( (yet=(char*)MAYBE(image_PNM_surrect()))!=ERROR_VALUE )
		{
			if (strcmp(yet, ret)==0)
				offset = fd_seek(0, SEEK_CUR);
			else
			{
				fd_seek(offset, SEEK_SET);
				break;
			}
		}
	}

	return ret;
}
