#ifndef _ANY_PROGRESS_H
#define _ANY_PROGRESS_H

struct progress_struct {
	char		format[20];
	char		backup[80];
	uint32_t	max;
	int		skip_progress;
};

void progress_init(struct progress_struct *progress,
			  const char *label,uint32_t max);

void progress_update(struct progress_struct *progress, uint32_t val);

void progress_close(struct progress_struct *progress);

void progress_update_non_backup(struct progress_struct *progress, uint32_t val);

void progress_backup(struct progress_struct *progress);

#endif /*_ANY_PROGRESS_H*/
