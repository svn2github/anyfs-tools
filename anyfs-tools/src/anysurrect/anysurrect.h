/*
 *	anysurrect.h
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#ifndef	_ANYSURRECT_H
#define _ANYSURRECT_H

#include <stdint.h>
#include "any.h"

#define RETURN(r) ({							\
	/*printf("anysurrect: in %s() at %s:%d, offset=%x\n", 		\
		__FUNCTION__ ,__FILE__, __LINE__,			\
		fd_seek(0, SEEK_CUR) );*/					\
	return r;							\
})

#define SKIP_STRING(name, len) 				\
	if ( fd_seek(len, SEEK_CUR) > fd_size() )	\
		RETURN (ERROR_VALUE)				

#define SKIP_BYTE(name) \
	SKIP_STRING(name, 1)

#define SKIP_SHORT(name) \
	SKIP_STRING(name, 2)

#define SKIP_BESHORT(name) SKIP_SHORT(name)
#define SKIP_LESHORT(name) SKIP_SHORT(name)

#define SKIP_LONG(name) \
	SKIP_STRING(name, 4)

#define SKIP_BELONG(name) SKIP_LONG(name)
#define SKIP_LELONG(name) SKIP_LONG(name)

#define MALLOC(len) ({ 					\
	char* m=malloc(len);				\
	if (!m) {					\
		fprintf (stderr, "Not enough memory\n");\
		exit(1);				\
	} m; })

#define COND_STRING(name, len, CONDITION) ({ 		\
	char *val=MALLOC(len+1);			\
	res = fd_read(val, len);			\
	if (!res) {					\
		free(val);				\
		RETURN (ERROR_VALUE);			\
	}						\
	val[len] = '\0';				\
	if (!(CONDITION)) {				\
		free (val);				\
		RETURN (ERROR_VALUE);			\
	}						\
	free(val);					\
})

#define EX_STRING(name, string) ({ 			\
	COND_STRING(name, strlen(string), 		\
		strcmp(val, string)==0);		\
})

#define LIST_STRING(name, len, list_strings...) ({		\
	char *val=MALLOC(len+1);			\
	res = fd_read(val, len);			\
	if (!res) {					\
		free(val);				\
		RETURN (ERROR_VALUE);			\
	}						\
	val[len] = '\0';				\
	int eq=0;					\
	char *LIST[]=list_strings;			\
	for (int i=0; LIST[i]; i++)			\
		if (strcmp(LIST[i], val)==0)		\
		{					\
			eq=1;				\
			break;				\
		}					\
	if (!eq) {					\
		free(val);				\
		RETURN (ERROR_VALUE);			\
	}						\
	free(val);					\
})

#define READ_BELONG(name) ({				\
	uint32_t	val;					\
	res = read_belong(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	val;						\
})

#define COND_BELONG(name, CONDITION) ({			\
	uint32_t	val;					\
	res = read_belong(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	if (!(CONDITION)) RETURN (ERROR_VALUE);		\
	val;						\
})

#define EX_BELONG(name, value)				\
	COND_BELONG(name, val==value)

#define READ_BESHORT(name) ({				\
	uint16_t	val;				\
	res = read_beshort(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	val;						\
})

#define COND_BESHORT(name, CONDITION) ({		\
	uint16_t	val;					\
	res = read_beshort(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	if (!(CONDITION)) RETURN (ERROR_VALUE);		\
	val;						\
})

#define EX_BESHORT(name, value)				\
	COND_BESHORT(name, val==value)

#define READ_LELONG(name) ({				\
	uint32_t	val;					\
	res = read_lelong(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	val;						\
})

#define COND_LELONG(name, CONDITION) ({			\
	uint32_t	val;					\
	res = read_lelong(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	if (!(CONDITION)) RETURN (ERROR_VALUE);		\
	val;						\
})

#define EX_LELONG(name, value)				\
	COND_LELONG(name, val==value)

#define READ_LESHORT(name) ({				\
	uint16_t	val;					\
	res = read_leshort(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	val;						\
})

#define COND_LESHORT(name, CONDITION) ({		\
	uint16_t	val;				\
	res = read_leshort(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	if (!(CONDITION)) RETURN (ERROR_VALUE);		\
	val;						\
})

#define EX_LESHORT(name, value)				\
	COND_LESHORT(name, val==value)

#define READ_BYTE(name) ({				\
	uint8_t	val;					\
	res = read_byte(&val);				\
	if (res) RETURN (ERROR_VALUE);			\
	val;						\
})

#define COND_BYTE(name, CONDITION) ({			\
	uint8_t	val;					\
	res = fd_read(&val, 1);				\
	if (!res) RETURN (ERROR_VALUE);			\
	if (!(CONDITION)) RETURN (ERROR_VALUE);		\
	val;						\
})

#define EX_BYTE(name, value)				\
	COND_BYTE(name, val==value)

#define FUNCOVER(name, operation)			\
	int name() { operation; return !ERROR_VALUE; }	

#define MAYBE(operation) ({				\
	off_t	offset = fd_seek(0, SEEK_CUR);		\
	int ret=(int) operation;			\
	if ( ret == ERROR_VALUE )			\
		fd_seek(offset, SEEK_SET);		\
	ret;						\
})

int read_byte(uint8_t *value);

int read_beshort(uint16_t *value);
int read_belong(uint32_t *value);

int read_leshort(uint16_t *value);
int read_lelong(uint32_t *value);

extern any_off_t	cur_offset;

any_size_t fd_size();
any_ssize_t fd_read(void *buf, any_size_t count);

static inline any_off_t fd_seek(any_off_t offset, int whence)
{
	if (whence==SEEK_CUR)
		offset += cur_offset;

	if (whence==SEEK_END)
		offset += fd_size();

	cur_offset = offset;

	return cur_offset;
}

#endif	/*_ANYSURRECT_H*/
