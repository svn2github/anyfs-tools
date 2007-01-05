#ifndef _ANY_PROGRESS_H
#define _ANY_PROGRESS_H

struct progress_struct {
        float           pr;
	uint32_t        next_update;
	uint32_t	updated;
	uint32_t	max;
	int		len;
	int		progress;
};

void progress_init(struct progress_struct *progress,
			  const char *label,uint32_t max);

void progress_update(struct progress_struct *progress, uint32_t val);

#include<stdio.h>

static inline 
int if_progress_update(struct progress_struct *progress, uint32_t val)
{
	return (progress->progress && val >= progress->next_update);
}

static inline 
int if_progress_updated(struct progress_struct *progress, uint32_t val)
{
	return (progress->progress && val == 
			progress->updated);
}

void progress_close(struct progress_struct *progress);

#endif /*_ANY_PROGRESS_H*/
