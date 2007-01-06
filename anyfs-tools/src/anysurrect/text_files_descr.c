/*
 *	text_files_descr.c
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
#include "text_files_descr.h"

/*General defines*/
static any_size_t max_offset;
#define fd_read(val, len) ({ 					\
		fd_read(val, len); 				\
		max_offset = max_t(any_size_t, max_offset, fd_seek(0, SEEK_CUR) );\
})

#define dos_NEW_LINE	EX_BESHORT("dos_new_line", 0x0D0A)
#define uniX_NEW_LINE	EX_BYTE("uniX_new_line", 0x0A)
#define mac_NEW_LINE	EX_BYTE("mac_new_line", 0x0D)

#define any_NEW_LINE ({							\
	uint8_t c = COND_BYTE("any_new_line", val==0x0D || val==0x0A);	\
	if (c==0x0D) MAYBE( mac_new_line() );				\
})

#define SPACE	EX_BYTE("space", 0x20)
#define TAB	EX_BYTE("space", 0x09)
#define WHITE	COND_BYTE("white", val==0x20 || val==0x09)

#define DELIMITER(os)  ({					\
	if ( MAYBE( white() )==ERROR_VALUE &&			\
		MAYBE( os##_new_line() )==ERROR_VALUE )		\
		return ERROR_VALUE;				\
})

/*ASCII*/

#define ASCII_SYMBOL	COND_BYTE("ascii_symbol", val>=0x21 && val<=0x7F)

#define ASCII_WORD ({						\
	while ( MAYBE( white() )!=ERROR_VALUE );		\
	ASCII_SYMBOL;						\
	int i;							\
	for (i=0; i<128 && 					\
		MAYBE( ascii_symbol() )!=ERROR_VALUE; i++)	\
	if (i>=128) return ERROR_VALUE;				\
})

#define ASCII_LINE(os) ({					\
	while ( MAYBE( os##_delimiter() )!=ERROR_VALUE );	\
	ASCII_WORD;						\
	int i;							\
	for (i=0; i<128 && 					\
		MAYBE( ascii_word() )!=ERROR_VALUE; i++ );	\
	if (i>=128) return ERROR_VALUE;				\
})

#define ASCII_TEXT(os) ({					\
	while ( MAYBE( ascii_word() )!=ERROR_VALUE );		\
	os##_NEW_LINE;						\
	ASCII_LINE(os);						\
	os##_NEW_LINE;						\
	ASCII_LINE(os);						\
	os##_NEW_LINE;						\
	while ( MAYBE( os##_ascii_line() )!=ERROR_VALUE );	\
	while ( MAYBE( os##_delimiter() )!=ERROR_VALUE );	\
})

FUNCOVER(dos_new_line, dos_NEW_LINE);
FUNCOVER(uniX_new_line, uniX_NEW_LINE);
FUNCOVER(mac_new_line, mac_NEW_LINE);
FUNCOVER(any_new_line, any_NEW_LINE);

FUNCOVER(space, SPACE);
FUNCOVER(tab, TAB);
FUNCOVER(white, WHITE);

FUNCOVER(dos_delimiter, DELIMITER(dos));
FUNCOVER(uniX_delimiter, DELIMITER(uniX));
FUNCOVER(mac_delimiter, DELIMITER(mac));
FUNCOVER(any_delimiter, DELIMITER(any));

FUNCOVER(ascii_symbol, ASCII_SYMBOL);
FUNCOVER(ascii_word, ASCII_WORD);

FUNCOVER(dos_ascii_line, ASCII_LINE(dos));
FUNCOVER(uniX_ascii_line, ASCII_LINE(uniX));
FUNCOVER(mac_ascii_line, ASCII_LINE(mac));
FUNCOVER(any_ascii_line, ASCII_LINE(any));

FUNCOVER(dos_ascii_text, ASCII_TEXT(dos));
FUNCOVER(uniX_ascii_text, ASCII_TEXT(uniX));
FUNCOVER(mac_ascii_text, ASCII_TEXT(mac));
FUNCOVER(any_ascii_text, ASCII_TEXT(any));

char *text_ASCII_surrect()
{
	max_offset = 0;

#ifdef ANYLINE
	if ( MAYBE( any_ascii_text() )!=ERROR_VALUE )
		return "text/ASCII";
#else

#ifdef DOSLINE
	if ( MAYBE( dos_ascii_text() )!=ERROR_VALUE )
		return "text/ASCII/DOS";
#endif
	
#ifdef UNIXLINE
	if ( MAYBE( uniX_ascii_text() )!=ERROR_VALUE )
		return "text/ASCII/UNIX";
#endif
	
#ifdef MACLINE
	if ( MAYBE( mac_ascii_text() )!=ERROR_VALUE )
		return "text/ASCII/MAC";
#endif

#endif /*ANYLINE*/

	fd_seek(max_offset, SEEK_SET);

	return NULL;
}

/*8-bit encoding*/

#define EIGHT_BIT_SYMBOL					\
	COND_BYTE("eight_bit_symbol", 				\
			(val>=0x21 && val<=0x7F) || val>=0xA0 )

#define EIGHT_BIT_WORD ({					\
	while ( MAYBE( white() )!=ERROR_VALUE );		\
	EIGHT_BIT_SYMBOL;					\
	int i;							\
	for (i=0; i<128 && 					\
		MAYBE( eight_bit_symbol() )!=ERROR_VALUE; i++ );\
	if (i>=128) return ERROR_VALUE;				\
})

#define EIGHT_BIT_LINE(os) ({					\
	while ( MAYBE( os##_delimiter() )!=ERROR_VALUE );	\
	EIGHT_BIT_WORD;						\
	int i;							\
	for (i=0; i<128 && 					\
		MAYBE( eight_bit_word() )!=ERROR_VALUE; i++ );	\
	if (i>=128) return ERROR_VALUE;				\
})

#define EIGHT_BIT_TEXT(os) ({					\
	while ( MAYBE( os##_ascii_line() )!=ERROR_VALUE );	\
	while ( MAYBE( os##_delimiter() )!=ERROR_VALUE );	\
	EIGHT_BIT_SYMBOL;					\
	while ( MAYBE( eight_bit_word() )!=ERROR_VALUE );	\
	os##_NEW_LINE;						\
	EIGHT_BIT_LINE(os);					\
	os##_NEW_LINE;						\
	EIGHT_BIT_LINE(os);					\
	os##_NEW_LINE;						\
	while ( MAYBE( os##_eight_bit_line() )!=ERROR_VALUE );	\
	while ( MAYBE( os##_delimiter() )!=ERROR_VALUE );	\
})

FUNCOVER(eight_bit_symbol, EIGHT_BIT_SYMBOL);
FUNCOVER(eight_bit_word, EIGHT_BIT_WORD);

FUNCOVER(dos_eight_bit_line, EIGHT_BIT_LINE(dos));
FUNCOVER(uniX_eight_bit_line, EIGHT_BIT_LINE(uniX));
FUNCOVER(mac_eight_bit_line, EIGHT_BIT_LINE(mac));
FUNCOVER(any_eight_bit_line, EIGHT_BIT_LINE(any));

FUNCOVER(dos_eight_bit_text, EIGHT_BIT_TEXT(dos));
FUNCOVER(uniX_eight_bit_text, EIGHT_BIT_TEXT(uniX));
FUNCOVER(mac_eight_bit_text, EIGHT_BIT_TEXT(mac));
FUNCOVER(any_eight_bit_text, EIGHT_BIT_TEXT(any));

char *text_EIGHT_BIT_surrect()
{
	max_offset = 0;

#ifdef ANYLINE
	if ( MAYBE( any_eight_bit_text() )!=ERROR_VALUE )
		return "text/8-BIT";
#else

#ifdef DOSLINE
	if ( MAYBE( dos_eight_bit_text() )!=ERROR_VALUE )
		return "text/8-BIT/DOS";
#endif
	
#ifdef UNIXLINE
	if ( MAYBE( uniX_eight_bit_text() )!=ERROR_VALUE )
		return "text/8-BIT/UNIX";
#endif
	
#ifdef MACLINE
	if ( MAYBE( mac_eight_bit_text() )!=ERROR_VALUE )
		return "text/8-BIT/MAC";
#endif

#endif /*ANYLINE*/

	fd_seek(max_offset, SEEK_SET);

	return NULL;
}

/*UTF-8*/

#define UTF8_SYMBOL_EL					\
	COND_BYTE("utf8", val>=0x80 && val<=0xBF )

#define UTF8_2BYTE_SYMBOL ({					\
	COND_BYTE("utf8_2", val>=0xC0 && val<=0xDF );		\
	UTF8_SYMBOL_EL;						\
})
	
#define UTF8_3BYTE_SYMBOL ({					\
	COND_BYTE("utf8_3", val>=0xE0 && val<=0xEF );		\
	UTF8_SYMBOL_EL;						\
	UTF8_SYMBOL_EL;						\
})

#define UTF8_4BYTE_SYMBOL ({					\
	COND_BYTE("utf8_4", val>=0xF0 && val<=0xF7 );		\
	UTF8_SYMBOL_EL;						\
	UTF8_SYMBOL_EL;						\
	UTF8_SYMBOL_EL;						\
})

#define UTF8_5BYTE_SYMBOL ({					\
	COND_BYTE("utf8_5", val>=0xF8 && val<=0xFB );		\
	UTF8_SYMBOL_EL;						\
	UTF8_SYMBOL_EL;						\
	UTF8_SYMBOL_EL;						\
	UTF8_SYMBOL_EL;						\
})

#define UTF8_6BYTE_SYMBOL ({					\
	COND_BYTE("utf8_6", val>=0xFC && val<=0xFD );		\
	UTF8_SYMBOL_EL;						\
	UTF8_SYMBOL_EL;						\
	UTF8_SYMBOL_EL;						\
	UTF8_SYMBOL_EL;						\
	UTF8_SYMBOL_EL;						\
})

#define UTF8_SYMBOL ({						\
	if ( 	MAYBE( ascii_symbol() )==ERROR_VALUE &&		\
		MAYBE( utf8_2byte_symbol() )==ERROR_VALUE &&	\
		MAYBE( utf8_3byte_symbol() )==ERROR_VALUE &&	\
		MAYBE( utf8_4byte_symbol() )==ERROR_VALUE &&	\
		MAYBE( utf8_5byte_symbol() )==ERROR_VALUE &&	\
		MAYBE( utf8_6byte_symbol() )==ERROR_VALUE )	\
		return ERROR_VALUE;				\
})

#define UTF8_NOT_ASCII_SYMBOL ({						\
	if ( 	MAYBE( utf8_2byte_symbol() )==ERROR_VALUE &&	\
		MAYBE( utf8_3byte_symbol() )==ERROR_VALUE &&	\
		MAYBE( utf8_4byte_symbol() )==ERROR_VALUE &&	\
		MAYBE( utf8_5byte_symbol() )==ERROR_VALUE &&	\
		MAYBE( utf8_6byte_symbol() )==ERROR_VALUE )	\
		return ERROR_VALUE;				\
})

#define UTF8_WORD ({						\
	while ( MAYBE( white() )!=ERROR_VALUE );		\
	UTF8_SYMBOL;						\
	int i;							\
	for (i=0; i<128 && 					\
		MAYBE( utf8_symbol() )!=ERROR_VALUE; i++ );	\
	if (i>=32) return ERROR_VALUE;				\
})

#define UTF8_LINE(os) ({					\
	while ( MAYBE( os##_delimiter() )!=ERROR_VALUE );	\
	UTF8_WORD;						\
	int i;							\
	for (i=0; i<128 && 					\
		MAYBE( utf8_word() )!=ERROR_VALUE; i++ );	\
	if (i>=128) return ERROR_VALUE;				\
})

#define UTF8_TEXT(os) ({					\
	while ( MAYBE( os##_ascii_line() )!=ERROR_VALUE );	\
	while ( MAYBE( os##_delimiter() )!=ERROR_VALUE );		\
	UTF8_NOT_ASCII_SYMBOL;					\
	while ( MAYBE( utf8_word() )!=ERROR_VALUE );	\
	os##_NEW_LINE;						\
	UTF8_LINE(os);						\
	os##_NEW_LINE;						\
	UTF8_LINE(os);						\
	os##_NEW_LINE;						\
	while ( MAYBE( os##_utf8_line() )!=ERROR_VALUE );	\
	while ( MAYBE( os##_delimiter() )!=ERROR_VALUE );	\
})

FUNCOVER(utf8_2byte_symbol, UTF8_2BYTE_SYMBOL);
FUNCOVER(utf8_3byte_symbol, UTF8_3BYTE_SYMBOL);
FUNCOVER(utf8_4byte_symbol, UTF8_4BYTE_SYMBOL);
FUNCOVER(utf8_5byte_symbol, UTF8_5BYTE_SYMBOL);
FUNCOVER(utf8_6byte_symbol, UTF8_6BYTE_SYMBOL);

FUNCOVER(utf8_symbol, UTF8_SYMBOL);
FUNCOVER(utf8_word, UTF8_WORD);

FUNCOVER(dos_utf8_line, UTF8_LINE(dos));
FUNCOVER(uniX_utf8_line, UTF8_LINE(uniX));
FUNCOVER(mac_utf8_line, UTF8_LINE(mac));
FUNCOVER(any_utf8_line, UTF8_LINE(any));

FUNCOVER(dos_utf8_text, UTF8_TEXT(dos));
FUNCOVER(uniX_utf8_text, UTF8_TEXT(uniX));
FUNCOVER(mac_utf8_text, UTF8_TEXT(mac));
FUNCOVER(any_utf8_text, UTF8_TEXT(any));

char *text_UTF8_surrect()
{
	max_offset = 0;

#ifdef ANYLINE
	if ( MAYBE( any_utf8_text() )!=ERROR_VALUE )
		return "text/UTF-8";
#else

#ifdef DOSLINE
	if ( MAYBE( dos_utf8_text() )!=ERROR_VALUE )
		return "text/UTF-8/DOS";
#endif
	
#ifdef UNIXLINE
	if ( MAYBE( uniX_utf8_text() )!=ERROR_VALUE )
		return "text/UTF-8/UNIX";
#endif
	
#ifdef MACLINE
	if ( MAYBE( mac_utf8_text() )!=ERROR_VALUE )
		return "text/UTF-8/MAC";
#endif

#endif /*ANYLINE*/

	fd_seek(max_offset, SEEK_SET);

	return NULL;
}


/*UTF-16*/

#define dos_UTF16_NEW_LINE(endian)			\
	EX_##endian##LONG("dos_new_line", 0x000D000A)
#define uniX_UTF16_NEW_LINE(endian)			\
	EX_##endian##SHORT("uniX_new_line", 0x000A)
#define mac_UTF16_NEW_LINE(endian)			\
	EX_##endian##SHORT("mac_new_line", 0x000D)

#define any_UTF16_NEW_LINE(endian) ({					\
	uint16_t c = COND_##endian##SHORT("any_new_line",		\
		val==0x000D || val==0x000A);				\
	if (c==0x000D) MAYBE( mac_utf16##endian##_new_line() );		\
})

#define UTF16_SPACE(endian)				\
	EX_##endian##SHORT("space", 0x0020)
#define UTF16_TAB(endian)				\
	EX_##endian##SHORT("space", 0x0009)
#define UTF16_WHITE(endian)				\
	COND_##endian##SHORT("white", val==0x0020 || val==0x0009)

#define UTF16_DELIMITER(endian, os)  ({					\
	if ( MAYBE( utf16##endian##_white() )==ERROR_VALUE &&		\
		MAYBE( os##_utf16##endian##_new_line() )==ERROR_VALUE )	\
		return ERROR_VALUE;					\
})

#define UTF16_SYMBOL(endian)							\
	COND_##endian##SHORT("utf16_symbol", val>=0x0021)

#define UTF16_WORD(endian) ({						\
	while ( MAYBE( utf16##endian##_white() )!=ERROR_VALUE );	\
	UTF16_SYMBOL(endian);						\
	int i;							\
	for (i=0; i<128 && 						\
		MAYBE( utf16##endian##_symbol() )!=ERROR_VALUE; i++ );	\
	if (i>=128) return ERROR_VALUE;				\
})

#define UTF16_LINE(endian, os) ({					\
	while ( MAYBE( os##_utf16##endian##_delimiter() )!=ERROR_VALUE );\
	UTF16_WORD(endian);						\
	int i;							\
	for (i=0; i<128 && 						\
		MAYBE( utf16##endian##_word() )!=ERROR_VALUE; i++ );	\
	if (i>=128) return ERROR_VALUE;				\
})

#define UTF16_TEXT(endian, os) ({					\
	while ( MAYBE( utf16##endian##_word() )!=ERROR_VALUE );		\
	os##_UTF16_NEW_LINE(endian);					\
	UTF16_LINE(endian, os);						\
	os##_UTF16_NEW_LINE(endian);					\
	UTF16_LINE(endian, os);						\
	os##_UTF16_NEW_LINE(endian);					\
	while ( MAYBE( os##_utf16##endian##_line() )!=ERROR_VALUE );	\
	while ( MAYBE( os##_utf16##endian##_delimiter() )!=ERROR_VALUE );\
})

FUNCOVER( dos_utf16BE_new_line, dos_UTF16_NEW_LINE(BE) );
FUNCOVER( uniX_utf16BE_new_line, uniX_UTF16_NEW_LINE(BE) );
FUNCOVER( mac_utf16BE_new_line, mac_UTF16_NEW_LINE(BE) );
FUNCOVER( any_utf16BE_new_line, any_UTF16_NEW_LINE(BE) );

FUNCOVER( utf16BE_space, UTF16_SPACE(BE) );
FUNCOVER( utf16BE_tab, UTF16_TAB(BE) );
FUNCOVER( utf16BE_white, UTF16_WHITE(BE) );

FUNCOVER( dos_utf16BE_delimiter, UTF16_DELIMITER(BE, dos) );
FUNCOVER( uniX_utf16BE_delimiter, UTF16_DELIMITER(BE, uniX) );
FUNCOVER( mac_utf16BE_delimiter, UTF16_DELIMITER(BE, mac) );
FUNCOVER( any_utf16BE_delimiter, UTF16_DELIMITER(BE, any) );

FUNCOVER( utf16BE_symbol, UTF16_SYMBOL(BE) );
FUNCOVER( utf16BE_word, UTF16_WORD(BE) );

FUNCOVER(dos_utf16BE_line, UTF16_LINE(BE, dos));
FUNCOVER(uniX_utf16BE_line, UTF16_LINE(BE, uniX));
FUNCOVER(mac_utf16BE_line, UTF16_LINE(BE, mac));
FUNCOVER(any_utf16BE_line, UTF16_LINE(BE, any));

FUNCOVER(dos_utf16BE_text, UTF16_TEXT(BE, dos));
FUNCOVER(uniX_utf16BE_text, UTF16_TEXT(BE, uniX));
FUNCOVER(mac_utf16BE_text, UTF16_TEXT(BE, mac));
FUNCOVER(any_utf16BE_text, UTF16_TEXT(BE, any));

char *text_UTF16BE_surrect()
{
	max_offset = 0;
#ifdef ANYLINE
	if ( MAYBE( any_utf16BE_text() )!=ERROR_VALUE )
		return "text/UTF16BE";
#else

#ifdef DOSLINE
	if ( MAYBE( any_utf16BE_text() )!=ERROR_VALUE )
		return "text/UTF16BE/DOS";
#endif
	
#ifdef UNIXLINE
	if ( MAYBE( uniX_utf16BE_text() )!=ERROR_VALUE )
		return "text/UTF16BE/UNIX";
#endif
	
#ifdef MACLINE
	if ( MAYBE( mac_utf16BE_text() )!=ERROR_VALUE )
		return "text/UTF16BE/MAC";
#endif

#endif /*ANYLINE*/

	fd_seek(max_offset, SEEK_SET);

	return NULL;
}

	FUNCOVER( dos_utf16LE_new_line, dos_UTF16_NEW_LINE(LE) );
	FUNCOVER( uniX_utf16LE_new_line, uniX_UTF16_NEW_LINE(LE) );
	FUNCOVER( mac_utf16LE_new_line, mac_UTF16_NEW_LINE(LE) );
	FUNCOVER( any_utf16LE_new_line, any_UTF16_NEW_LINE(LE) );
	
	FUNCOVER( utf16LE_space, UTF16_SPACE(LE) );
	FUNCOVER( utf16LE_tab, UTF16_TAB(LE) );
	FUNCOVER( utf16LE_white, UTF16_WHITE(LE) );
	
	FUNCOVER( dos_utf16LE_delimiter, UTF16_DELIMITER(LE, dos) );
	FUNCOVER( uniX_utf16LE_delimiter, UTF16_DELIMITER(LE, uniX) );
	FUNCOVER( mac_utf16LE_delimiter, UTF16_DELIMITER(LE, mac) );
	FUNCOVER( any_utf16LE_delimiter, UTF16_DELIMITER(LE, any) );
	
	FUNCOVER( utf16LE_symbol, UTF16_SYMBOL(LE) );
	FUNCOVER( utf16LE_word, UTF16_WORD(LE) );
	
	FUNCOVER(dos_utf16LE_line, UTF16_LINE(LE, dos));
	FUNCOVER(uniX_utf16LE_line, UTF16_LINE(LE, uniX));
	FUNCOVER(mac_utf16LE_line, UTF16_LINE(LE, mac));
	FUNCOVER(any_utf16LE_line, UTF16_LINE(LE, any));
	
	FUNCOVER(dos_utf16LE_text, UTF16_TEXT(LE, dos));
	FUNCOVER(uniX_utf16LE_text, UTF16_TEXT(LE, uniX));
	FUNCOVER(mac_utf16LE_text, UTF16_TEXT(LE, mac));
	FUNCOVER(any_utf16LE_text, UTF16_TEXT(LE, any));

char *text_UTF16LE_surrect()
{
	max_offset = 0;

#ifdef ANYLINE
	if ( MAYBE( any_utf16LE_text() )!=ERROR_VALUE )
		return "text/UTF16LE";
#else

#ifdef DOSLINE
	if ( MAYBE( dos_utf16LE_text() )!=ERROR_VALUE )
		return "text/UTF16LE/DOS";
#endif
	
#ifdef UNIXLINE
	if ( MAYBE( uniX_utf16LE_text() )!=ERROR_VALUE )
		return "text/UTF16LE/UNIX";
#endif
	
#ifdef MACLINE
	if ( MAYBE( mac_utf16LE_text() )!=ERROR_VALUE )
		return "text/UTF16LE/MAC";
#endif

#endif /*ANYLINE*/

	fd_seek(max_offset, SEEK_SET);

	return NULL;
}
