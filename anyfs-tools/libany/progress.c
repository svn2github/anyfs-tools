/*
 * From mke2fs from e2fsprogs, Copyright (C) 1994-2005 by Theodore Ts'o.
 */

#include "any.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/fd.h>

int quiet;

static int int_log10(unsigned int arg)
{
	int     l;

	for (l=0; arg ; l++)
		arg = arg / 10;
	return l;
}

/*
 * These functions implement a generalized progress meter.
 */
struct progress_struct {
	char		format[20];
	char		backup[80];
	uint32_t	max;
	int		skip_progress;
};

void progress_init(struct progress_struct *progress,
			  const char *label,uint32_t max)
{
	int	i;

	memset(progress, 0, sizeof(struct progress_struct));
	if (quiet)
		return;

	/*
	 * Figure out how many digits we need
	 */
	if (max)
	{
		i = int_log10(max);
		sprintf(progress->format, "%%%dd/%%%dld", i, i);
	} else
	{
		i = 10;
		sprintf(progress->format, "%%%dd", i);
	}

	memset(progress->backup, '\b', sizeof(progress->backup)-1);
	progress->backup[sizeof(progress->backup)-1] = 0;
	if ( ((max)?(2*i)+1:i) < (int) sizeof(progress->backup))
		progress->backup[ (max)?(2*i)+1:i ] = 0;
	progress->max = max;

	progress->skip_progress = 0;
	if (getenv("ANYFSTOOLS_SKIP_PROGRESS"))
		progress->skip_progress++;

	fputs(label, stdout);
	fflush(stdout);
}

void progress_update(struct progress_struct *progress, uint32_t val)
{
	if ((progress->format[0] == 0) || progress->skip_progress)
		return;
	if (progress->max)
		printf(progress->format, val, progress->max);
	else
		printf(progress->format, val);
	fputs(progress->backup, stdout);
	if ( progress->max < 100000 || (val%100)==0 )
		fflush (stdout);
}

void progress_update_non_backup(struct progress_struct *progress, uint32_t val)
{
	if ((progress->format[0] == 0) || progress->skip_progress)
		return;
	if (progress->max)
		printf(progress->format, val, progress->max);
	else
		printf(progress->format, val);
}

void progress_backup(struct progress_struct *progress)
{
	fputs(progress->backup, stdout);
	fflush (stdout);
}

void progress_close(struct progress_struct *progress)
{
	if (progress->format[0] == 0)
		return;
	if (progress->max)
		fputs(_("done                            \n"), stdout);
	else fputs("\n", stdout);
}


