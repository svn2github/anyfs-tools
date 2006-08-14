/*
 * util.h --- header file defining prototypes for helper functions
 * used by tune2fs and mke2fs
 * 
 * Copyright 2000 by Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */
#ifndef _ANY_UTIL_H
#define _ANY_UTIL_H

extern int	 journal_size;
extern int	 journal_flags;
extern char	*journal_device;

extern char *get_progname(char *argv_zero);
extern void proceed_question(void);
extern void check_plausibility(const char *device);
extern void parse_journal_opts(const char *opts);
extern void check_mount(const char *device, int force, const char *type);
extern int figure_journal_size(int size, ext2_filsys fs);
extern void print_check_message(ext2_filsys fs);

#endif /*_ANY_UTIL_H*/
