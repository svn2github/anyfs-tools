/*
 *	anysurrect_malloc.c
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#include "anysurrect_malloc.h"

void *	anysurrect_allocbuf[NUM_MALLOC_BUFFERS];
size_t 	anysurrect_allocbuf_size[NUM_MALLOC_BUFFERS];
int 	anysurrect_allocbuf_busy[NUM_MALLOC_BUFFERS];

void anysurrect_malloc_clean()
{
	int num = 0;
	for (num=0; num<NUM_MALLOC_BUFFERS; num++)
	{
		anysurrect_allocbuf[num] = NULL;
		anysurrect_allocbuf_size[num] = 0;
		anysurrect_allocbuf_busy[num] = 0;
	}
}

void anysurrect_free_clean()
{
	int num = 0;
	for (num=0; num<NUM_MALLOC_BUFFERS; num++)
	{
		if (anysurrect_allocbuf[num])
		{
			free(anysurrect_allocbuf[num]);
			anysurrect_allocbuf[num] = NULL;
			anysurrect_allocbuf_size[num] = 0;
			anysurrect_allocbuf_busy[num] = 0;
		}
	}
}
