/*
 *	anysurrect.h
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#include <stdint.h>
#include <stdlib.h>
#include "any.h"

#define COPY_FRAGS_MALLOC_BUFFER	0
#define COND_STRING_MALLOC_BUFFER	1
#define LIST_STRING_MALLOC_BUFFER	2
#define NUM_MALLOC_BUFFERS		3

extern void *	anysurrect_allocbuf[NUM_MALLOC_BUFFERS];
extern size_t 	anysurrect_allocbuf_size[NUM_MALLOC_BUFFERS];
extern int 	anysurrect_allocbuf_busy[NUM_MALLOC_BUFFERS];

static inline void *anysurrect_malloc(size_t size, int num)
{
	if (anysurrect_allocbuf_busy[num])
		return malloc(size);

	if (!anysurrect_allocbuf[num])
	{
		anysurrect_allocbuf[num] = malloc(size);
		anysurrect_allocbuf_size[num] = size;
	}

	if (size > anysurrect_allocbuf_size[num])
	{
		anysurrect_allocbuf[num] = realloc (anysurrect_allocbuf[num], size);
		anysurrect_allocbuf_size[num] = size;
	}

	anysurrect_allocbuf_busy[num] = 1;

	return anysurrect_allocbuf[num];
}

static inline void anysurrect_free(void *ptr, int num)
{
	if (ptr == anysurrect_allocbuf[num])
		anysurrect_allocbuf_busy[num]= 0;
	else
		free(ptr);
}

void anysurrect_malloc_clean();
void anysurrect_free_clean();

