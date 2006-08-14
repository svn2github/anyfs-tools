/*
 *	document_files_descr.c
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
#include "document_files_descr.h"

/*PDF*/
#define IS_WHITESPACE(val) (val==' ' || val=='\t' || val=='\r' || val=='\n')

#define COMMENT_STRING ({					\
	EX_BYTE("comment sign", '%');				\
	char val;						\
	while ( ( val=READ_BYTE("wait_eol") )!='\n' &&		\
	     		val!='\r' );				\
	fd_seek(-1, SEEK_CUR);					\
	EOL;							\
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
	val;							\
})

#define READ_TO_CHAR(buffer, bufsize, chr) 	\
	READ_TO_COND (buffer, bufsize, val == chr )

#define READ_TO_WHITESPACE(buffer, bufsize) 	\
	READ_TO_COND (buffer, bufsize, IS_WHITESPACE(val) )

#define SKIP_COMMENTS ({					\
	FUNCOVER(comment, COMMENT_STRING);			\
	while ( MAYBE(comment())!=ERROR_VALUE );		\
})

#define DOS_EOL		EX_STRING("dos_eol", "\r\n")
#define UNIX_EOL	EX_BYTE("unix_eol", '\n')
#define MAC_EOL		EX_BYTE("mac_eol", '\r')

#define EOL ({						\
	FUNCOVER(dos_eol, DOS_EOL);			\
	FUNCOVER(unix_eol, UNIX_EOL);			\
	FUNCOVER(mac_eol, MAC_EOL);			\
	if ( MAYBE(dos_eol())==ERROR_VALUE &&		\
		MAYBE(unix_eol())==ERROR_VALUE &&	\
		MAYBE(mac_eol())==ERROR_VALUE )		\
		return ERROR_VALUE;			\
})

#define WHITE_SPACE ({							\
	FUNCOVER(space, COND_BYTE("space", val==' ' || val=='\t'));	\
	FUNCOVER(eol, EOL);						\
									\
	if ( MAYBE(space())==ERROR_VALUE && MAYBE(eol())==ERROR_VALUE )	\
		return ERROR_VALUE;					\
})
		
#define SKIP_WHITESPACES ({					\
	FUNCOVER(whitespace, COND_BYTE("WhiteSpace",		\
			IS_WHITESPACE(val) ));			\
	while ( MAYBE(whitespace())!=ERROR_VALUE );		\
})


#define TRUE_OBJECT	EX_STRING("true", "true")
#define FALSE_OBJECT	EX_STRING("false", "false")
#define NULL_OBJECT	EX_STRING("null", "null")

#define BOOLEAN	({						\
	FUNCOVER(true_object, TRUE_OBJECT);			\
	FUNCOVER(false_object, FALSE_OBJECT);			\
	if ( MAYBE(true_object())==ERROR_VALUE &&		\
		 MAYBE(false_object())==ERROR_VALUE )		\
		return ERROR_VALUE;				\
})

#define SIGN	COND_BYTE("sign", val=='+' || val=='-')
#define DIGIT	COND_BYTE("digit", val>='0' && val<='9')
#define POINT	EX_BYTE("point", '.')

#define FLOAT ({						\
	FUNCOVER(sign, SIGN);					\
	FUNCOVER(digit, DIGIT);					\
	MAYBE( sign() );					\
	while (MAYBE( digit() )!=ERROR_VALUE);			\
	POINT;							\
	DIGIT;							\
	while (MAYBE( digit() )!=ERROR_VALUE);			\
})

#define INTEGER ({						\
	FUNCOVER(sign, SIGN);					\
	FUNCOVER(digit, DIGIT);					\
	MAYBE( sign() );					\
	DIGIT;							\
	while (MAYBE( digit() )!=ERROR_VALUE);			\
})

#define NUMBER ({						\
	FUNCOVER(float_object, FLOAT);				\
	FUNCOVER(integer_object, INTEGER);			\
	if ( MAYBE(float_object())==ERROR_VALUE &&		\
		 MAYBE(integer_object())==ERROR_VALUE )		\
		return ERROR_VALUE;				\
})

#define NBS_SYMBOL COND_BYTE( "symbol", (val>=0x20 && \
		val!='\\' && val!='(' && val!=')') || val=='\t' )
#define BACKSLASH_EOL ({					\
	EX_BYTE("backslash", '\\');				\
	EOL;							\
})
#define BACKSLASH_SYMBOL ({					\
	EX_BYTE("backslash", '\\');				\
	COND_BYTE("symbol", val>=0x20 && val<=0x7F);		\
})

#define STRING_SYMBOL ({					\
	FUNCOVER(nbs_symbol, NBS_SYMBOL);			\
	FUNCOVER(backslash_eol, BACKSLASH_EOL);			\
	FUNCOVER(backslash_symbol, BACKSLASH_SYMBOL);		\
								\
	if ( MAYBE( nbs_symbol() )==ERROR_VALUE && 		\
		MAYBE( backslash_eol() )==ERROR_VALUE &&	\
		MAYBE( backslash_symbol() )==ERROR_VALUE )	\
		return ERROR_VALUE;				\
})

int text_string()
{
#define ERROR_VALUE	0
	int res;
	
	FUNCOVER(string_symbol, STRING_SYMBOL);

	EX_BYTE("begin string", '(');
	while ( MAYBE( string_symbol() )!=ERROR_VALUE
	     || MAYBE( text_string() )!=ERROR_VALUE );
	
	EX_BYTE("end string", ')');
	
	return !ERROR_VALUE;
#undef	ERROR_VALUE
}

#define TEXT_STRING 							\
	if (MAYBE(text_string())==ERROR_VALUE) return ERROR_VALUE

#define HEX_SYMBOL COND_BYTE("hex symbol", 			\
		(val>='0' && val<='9') ||			\
		(val>='a' && val<='f') ||			\
		(val>='A' && val<='F') ||			\
		IS_WHITESPACE(val) )

#define HEX_STRING ({						\
	FUNCOVER(hex_symbol, HEX_SYMBOL);			\
								\
	EX_BYTE("begin string", '<');				\
	while ( MAYBE( hex_symbol() )!=ERROR_VALUE );		\
	EX_BYTE("end string", '>');				\
})

#define STRING ({						\
	FUNCOVER(hex_string, HEX_STRING);			\
								\
	if ( MAYBE(text_string())==ERROR_VALUE &&		\
		MAYBE(hex_string())==ERROR_VALUE )		\
		return ERROR_VALUE;				\
})

#define NAME_SYMBOL 						\
	COND_BYTE("name_symbol", val>='!' && val<='~' &&	\
			val!='%' && val!='(' && val!=')' &&	\
			val!='<' && val!='>' && val!='[' &&	\
			val!=']' && val!='{' && val!='}' &&	\
			val!='/')

#define NAME ({							\
	FUNCOVER(name_symbol, NAME_SYMBOL);			\
	EX_BYTE("begin", '/');					\
	while ( MAYBE( name_symbol() )!=ERROR_VALUE );		\
})

#define SIMPLE_OBJECT ({					\
	FUNCOVER(boolean, BOOLEAN);				\
	FUNCOVER(number, NUMBER);				\
	FUNCOVER(string, STRING);				\
	FUNCOVER(name, NAME);					\
	FUNCOVER(null, NULL_OBJECT);				\
								\
	if ( MAYBE(boolean())==ERROR_VALUE &&			\
	  	MAYBE(number())==ERROR_VALUE &&			\
	  	MAYBE(string())==ERROR_VALUE &&			\
		MAYBE(name())==ERROR_VALUE &&			\
	  	MAYBE(null())==ERROR_VALUE )			\
		return ERROR_VALUE;				\
})

int array();
int dictionary();
int stream();

#define ARRAY 							\
	if (MAYBE(array())==ERROR_VALUE) return ERROR_VALUE

#define DICTIONARY 						\
	if (MAYBE(dictionary())==ERROR_VALUE) return ERROR_VALUE

#define STREAM 							\
	if (MAYBE(stream())==ERROR_VALUE) return ERROR_VALUE

#define OBJECT ({						\
	FUNCOVER(simple_object, SIMPLE_OBJECT);			\
	if ( MAYBE(simple_object())==ERROR_VALUE &&		\
		MAYBE(array())==ERROR_VALUE &&			\
		MAYBE(stream())==ERROR_VALUE &&			\
		MAYBE(dictionary())==ERROR_VALUE )		\
		return ERROR_VALUE;				\
})

#define INDIRECT_OBJECT	({					\
	INTEGER;	 		SKIP_WHITESPACES;	\
	INTEGER;	 		SKIP_WHITESPACES;	\
	EX_STRING("obj", "obj");	SKIP_WHITESPACES;	\
	OBJECT;				SKIP_WHITESPACES;	\
	EX_STRING("endobj", "endobj");				\
})	

#define INDIRECT_REFERENCE ({					\
	INTEGER;	 		SKIP_WHITESPACES;	\
	INTEGER;	 		SKIP_WHITESPACES;	\
	EX_BYTE("R", 'R');					\
})	

#define OBJECT_OR_REFERENCE ({					\
	FUNCOVER(reference, INDIRECT_REFERENCE);		\
	FUNCOVER(object, OBJECT);				\
	if ( MAYBE(reference())==ERROR_VALUE &&			\
	  	MAYBE(object())==ERROR_VALUE )		\
		return ERROR_VALUE;				\
})

int array()
{
#define ERROR_VALUE	0
	int res;
	
	FUNCOVER(end_array, EX_STRING("end", "]"));
	
	EX_STRING("begin", "[");
	SKIP_WHITESPACES;
	
	while( MAYBE(end_array())==ERROR_VALUE )
	{
		OBJECT_OR_REFERENCE;
		SKIP_WHITESPACES;
	}
	
	return !ERROR_VALUE;
#undef	ERROR_VALUE
}

int dictionary()
{
#define ERROR_VALUE	0
	int res;

	FUNCOVER(end_dict, EX_STRING("end", ">>"));
	
	EX_STRING("begin", "<<");
	SKIP_WHITESPACES;
	
	while( MAYBE(end_dict())==ERROR_VALUE )
	{
#ifdef DEBUG
	printf("d %x\n", fd_seek(0, SEEK_CUR));
#endif
		NAME;
		SKIP_WHITESPACES;
#ifdef DEBUG
	printf("f %x\n", fd_seek(0, SEEK_CUR));
#endif
		OBJECT_OR_REFERENCE;
		SKIP_WHITESPACES;
#ifdef DEBUG
	printf("g %x\n", fd_seek(0, SEEK_CUR));
#endif
	}
	
	return !ERROR_VALUE;
#undef	ERROR_VALUE
}
	
int stream()
{
#define ERROR_VALUE	0
	int res;

	FUNCOVER(end_dict, EX_STRING("end", ">>"));
	FUNCOVER(length_name, ({ EX_STRING("length_name", "/Length"); 
				WHITE_SPACE; }) );
	FUNCOVER(reference, INDIRECT_REFERENCE);		\
	FUNCOVER(end_stream, EX_STRING("end stream", "endstream"));
	
	EX_STRING("begin", "<<");
	SKIP_WHITESPACES;

	uint32_t length=0;
	
	while( MAYBE(end_dict())==ERROR_VALUE )
	{
		if (MAYBE(length_name())!=ERROR_VALUE)
		{
			SKIP_WHITESPACES;
#ifdef DEBUG
	printf("Hello world! %x\n", fd_seek(0, SEEK_CUR));
#endif

			if ( MAYBE( reference() )!=ERROR_VALUE )
			{
				SKIP_WHITESPACES;
				continue;
			}

			char *tmp;
			char buffer[256];
			char c=READ_TO_COND(buffer, 256, val<'0' || val>'9');
			if (!IS_WHITESPACE(c)) fd_seek(-1, SEEK_CUR);
			length = strtol(buffer, &tmp, 10);
			if (*tmp) return ERROR_VALUE;
			
			SKIP_WHITESPACES;
#ifdef DEBUG
	printf("%d %x\n", length, fd_seek(0, SEEK_CUR));
#endif
		}
		else
		{
			NAME;
			SKIP_WHITESPACES;
			OBJECT_OR_REFERENCE;
			SKIP_WHITESPACES;
		}
	}
	
	SKIP_WHITESPACES;
	EX_STRING("begin stream", "stream"); EOL;
#ifdef DEBUG
	printf("begin stream %x\n", fd_seek(0, SEEK_CUR));
#endif
	if (length)
	{
		SKIP_STRING("stream data", length);
		SKIP_WHITESPACES;
#ifdef DEBUG
	printf("expect end stream %x\n", fd_seek(0, SEEK_CUR));
#endif
		EX_STRING("end stream", "endstream");
	}
	else
	{
		unsigned long s = 0;
		while ( MAYBE(end_stream())==ERROR_VALUE )
		{
			SKIP_BYTE("data");
			s++;
			if (s>MAX_PDF_STREAM_SIZE) {printf("\n"); break;}
		}
	}
#ifdef DEBUG
	printf("end stream %x\n", fd_seek(0, SEEK_CUR));
#endif
	
	return !ERROR_VALUE;
#undef	ERROR_VALUE
}

#define HEADER ({						\
	EX_STRING("magic", "\%PDF");				\
	char val;						\
	while ( ( val=READ_BYTE("wait_eol") )!='\n' &&		\
	     		val!='\r' );				\
	fd_seek(-1, SEEK_CUR);					\
	EOL;							\
})

#define BODY ({							\
	FUNCOVER(indirect_object, INDIRECT_OBJECT);		\
	SKIP_COMMENTS;						\
	SKIP_WHITESPACES;					\
	while (MAYBE(indirect_object())!=ERROR_VALUE)		\
	{							\
		SKIP_WHITESPACES;				\
		SKIP_COMMENTS;					\
		SKIP_WHITESPACES;				\
	}							\
})

#define CROSS_REFERENCE_SUBSECTION ({				\
	uint32_t num_entries;					\
	INTEGER; SKIP_WHITESPACES;				\
								\
	char *tmp;						\
	char buffer[256];					\
	READ_TO_COND(buffer, 256, val<'0' || val>'9');	\
	num_entries = strtol(buffer, &tmp, 10);			\
	if (*tmp) return ERROR_VALUE;				\
								\
	if (!num_entries) return ERROR_VALUE;			\
	SKIP_WHITESPACES;					\
								\
	uint32_t i;						\
	for (i=0; i<num_entries; i++)				\
	{							\
		SKIP_STRING("data", 17);			\
		COND_BYTE("free indicator", 			\
				val=='n' || val=='f');		\
		COND_BYTE("eol1", 				\
				val==' ' || val=='\r');		\
		COND_BYTE("eol2", 				\
				val=='\r' || val=='\n');	\
	}							\
})

#define CROSS_REFERENCE_TABLE ({				\
	FUNCOVER (crs, CROSS_REFERENCE_SUBSECTION);		\
	EX_STRING("xref", "xref");				\
	SKIP_WHITESPACES;					\
	CROSS_REFERENCE_SUBSECTION;				\
	SKIP_WHITESPACES;					\
	while ( MAYBE(crs())!=ERROR_VALUE )			\
		SKIP_WHITESPACES;				\
})

#define TRAILER_HEAD ({						\
	EX_STRING("trailer", "trailer"); SKIP_WHITESPACES;	\
	DICTIONARY;	SKIP_WHITESPACES;			\
})

#define TRAILER_TAIL ({						\
	EX_STRING("startxref", "startxref"); SKIP_WHITESPACES;	\
	INTEGER;	SKIP_WHITESPACES;			\
	EX_STRING("eof", "\%\%EOF");				\
	EOL;							\
})

#define UPDATE ({						\
	BODY;				SKIP_WHITESPACES;	\
	MAYBE( crt_th() ); SKIP_WHITESPACES;	\
	TRAILER_TAIL;						\
})

char *document_PDF_surrect()
{
#define ERROR_VALUE	0
	int res;
	FUNCOVER(crt_th, ({ CROSS_REFERENCE_TABLE; TRAILER_HEAD; }) );

	HEADER;				SKIP_WHITESPACES;
	BODY;				SKIP_WHITESPACES;
	MAYBE( crt_th() );
	TRAILER_TAIL;

	FUNCOVER(update, UPDATE);
	while ( MAYBE(update())!=ERROR_VALUE );
	
	return "documents/PDF";
#undef	ERROR_VALUE
}
