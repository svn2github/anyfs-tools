/*
 * release_fssys.h
 * Copyright (C) 2006 Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */
#ifndef _ANY_RELEASE_FSSYS_H
#define _ANY_RELEASE_FSSYS_H

typedef int any_rwblk_t(unsigned long from, unsigned long n, char *buffer);
extern any_rwblk_t *any_readblk;
extern any_rwblk_t *any_writeblk;

typedef int any_testblk_t(unsigned long bitno);
extern any_testblk_t *any_testblk;

typedef unsigned long any_getblkcount_t();
extern any_getblkcount_t *any_getblkcount;

/*release space for system information of FS*/
int any_release(struct any_sb_info *info, 
		unsigned long *block_bitmap,
		unsigned long start, unsigned long length);

int any_release_sysinfo(struct any_sb_info *info, 
		unsigned long *block_bitmap,
		any_rwblk_t *readblk,
		any_rwblk_t *writeblk,
		any_testblk_t *testblk,
		any_getblkcount_t *getblkcount);

/*add dot (".") and dotdot ("..") entries to directories*/
int any_adddadd(struct any_sb_info *info);

#endif /*_ANY_RELEASE_FSSYS_H*/
