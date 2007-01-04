/*
 *	direct_io.h
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#include <stdint.h>
#include "any.h"
#include "anysurrect.h"

#define SKIP_STRING_DR(name, len) 				\
	if ( fd_seek_dr(len, SEEK_CUR) > fd_size_dr() )	\
		RETURN (ERROR_VALUE)				

#define SKIP_BYTE_DR(name) \
	SKIP_STRING_DR(name, 1)

#define SKIP_SHORT_DR(name) \
	SKIP_STRING_DR(name, 2)

#define SKIP_BESHORT_DR(name) SKIP_SHORT_DR(name)
#define SKIP_LESHORT_DR(name) SKIP_SHORT_DR(name)

#define SKIP_LONG_DR(name) \
	SKIP_STRING_DR(name, 4)

#define SKIP_BELONG_DR(name) SKIP_LONG_DR(name)
#define SKIP_LELONG_DR(name) SKIP_LONG_DR(name)

#define COND_STRING_DR(name, len, CONDITION) ({ 		\
	char *val=MALLOC(len+1);			\
	res = fd_read_dr(val, len);			\
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

#define EX_STRING_DR(name, string) ({ 			\
	COND_STRING_DR(name, strlen(string), 		\
		strcmp(val, string)==0);		\
})

#define LIST_STRING_DR(name, len, list_strings...) ({	\
	char *val=MALLOC(len+1);			\
	res = fd_read_dr(val, len);			\
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

#define READ_BELONG_DR(name) ({				\
	uint32_t	val;				\
	res = read_belong_dr(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	val;						\
})

#define COND_BELONG_DR(name, CONDITION) ({		\
	uint32_t	val;				\
	res = read_belong_dr(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	if (!(CONDITION)) RETURN (ERROR_VALUE);		\
	val;						\
})

#define EX_BELONG_DR(name, value)			\
	COND_BELONG_DR(name, val==value)

#define READ_BESHORT_DR(name) ({			\
	uint16_t	val;				\
	res = read_beshort_dr(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	val;						\
})

#define COND_BESHORT_DR(name, CONDITION) ({		\
	uint16_t	val;				\
	res = read_beshort_dr(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	if (!(CONDITION)) RETURN (ERROR_VALUE);		\
	val;						\
})

#define EX_BESHORT_DR(name, value)				\
	COND_BESHORT_DR(name, val==value)

#define READ_LELONG_DR(name) ({				\
	uint32_t	val;					\
	res = read_lelong_dr(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	val;						\
})

#define COND_LELONG_DR(name, CONDITION) ({			\
	uint32_t	val;					\
	res = read_lelong_dr(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	if (!(CONDITION)) RETURN (ERROR_VALUE);		\
	val;						\
})

#define EX_LELONG_DR(name, value)				\
	COND_LELONG_DR(name, val==value)

#define READ_LESHORT_DR(name) ({				\
	uint16_t	val;					\
	res = read_leshort_dr(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	val;						\
})

#define COND_LESHORT_DR(name, CONDITION) ({		\
	uint16_t	val;				\
	res = read_leshort_dr(&val);			\
	if (res) RETURN (ERROR_VALUE);			\
	if (!(CONDITION)) RETURN (ERROR_VALUE);		\
	val;						\
})

#define EX_LESHORT_DR(name, value)				\
	COND_LESHORT_DR(name, val==value)

#define READ_BYTE_DR(name) ({				\
	uint8_t	val;					\
	res = read_byte_dr(&val);				\
	if (res) RETURN (ERROR_VALUE);			\
	val;						\
})

#define COND_BYTE_DR(name, CONDITION) ({			\
	uint8_t	val;					\
	res = fd_read_dr(&val, 1);				\
	if (!res) RETURN (ERROR_VALUE);			\
	if (!(CONDITION)) RETURN (ERROR_VALUE);		\
	val;						\
})

#define EX_BYTE_DR(name, value)				\
	COND_BYTE_DR(name, val==value)

int read_byte_dr(uint8_t *value);

int read_beshort_dr(uint16_t *value);
int read_belong_dr(uint32_t *value);

int read_leshort_dr(uint16_t *value);
int read_lelong_dr(uint32_t *value);

void fd_set_direct_start(any_off_t offset);
any_off_t fd_get_direct_start();

any_off_t fd_seek_dr(any_off_t offset, int whence);
any_size_t fd_size_dr();
any_ssize_t fd_read_dr(void *buf, any_size_t count);
