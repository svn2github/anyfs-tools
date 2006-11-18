#ifndef _ANY_PROGRESS_H
#define _ANY_PROGRESS_H

struct progress_struct {
        float           pr;
	uint32_t        next_update;
	uint32_t	max;
	int		len;
	int		skip_progress;
};

void progress_init(struct progress_struct *progress,
			  const char *label,uint32_t max);

void progress_update(struct progress_struct *progress, uint32_t val);

void progress_close(struct progress_struct *progress);

#endif /*_ANY_PROGRESS_H*/
