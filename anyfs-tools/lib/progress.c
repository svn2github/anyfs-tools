/*
 * From mke2fs from e2fsprogs, Copyright (C) 1994-2005 by Theodore Ts'o.
 */

#include "any.h"
#include "progress.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int quiet;
/*
 * These functions implement a generalized progress meter.
 */
void progress_init(struct progress_struct *progress,
			  const char *label,uint32_t max)
{
	memset(progress, 0, sizeof(struct progress_struct));
	if (quiet)
		return;

	if (max)
		progress->pr = (double)max / 100;
	else
		progress->pr = 10000;

	progress->max = max;
	progress->next_update = 0;
	progress->updated = 0xFFFFFFFF;
	progress->progress = 1;
	progress->len = 0;
	if (getenv("ANYFSTOOLS_SKIP_PROGRESS"))
		progress->progress = 0;

	fputs(label, stderr);
	fflush(stderr);
}

void progress_update(struct progress_struct *progress, uint32_t val)
{
	int i;

	if (!progress->progress)
		return;

	if (val < progress->next_update)
		return;

	for (i = 0; i < progress->len; i++)
		fputc('\b', stderr);

	if (progress->max)
		progress->len = fprintf(stderr, "%.2f%% (%i/%i)", (float)val / progress->pr, val, progress->max);
	else
		progress->len = fprintf(stderr, "%i", val);

	progress->updated = val;
	progress->next_update += progress->pr / 100;
	fflush (stderr);
}

void progress_close(struct progress_struct *progress)
{
	if (progress->max)
		fputs(_(" done   \n"), stderr);
	else fputs("\n", stdout);
}


