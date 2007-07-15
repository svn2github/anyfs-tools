#ifndef ANYFSLIB_SUPER_H
#define ANYFSLIB_SUPER_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int write_it(struct any_sb_info *it, char itfilename[]);
int read_it(struct any_sb_info ** it, char itfilename[]);
int realloc_it(struct any_sb_info *info, unsigned long inodes);
int alloc_it(struct any_sb_info ** it, unsigned long blocksize, 
		unsigned long inodes);
void free_it(struct any_sb_info * info);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /*ANYFSLIB_SUPER_H*/
