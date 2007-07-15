/*
 *  new_inode.h
 */

#include "any.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int any_new_inode(struct any_sb_info *info, int mode, void* data,
		uint32_t dirino, uint32_t *newino);

int getpathino(char *path, uint32_t root, struct any_sb_info *info,
		uint32_t *ino);

int mkpathino(char *path, uint32_t root, struct any_sb_info *info,
		uint32_t *ino);

#ifdef __cplusplus
}
#endif /* __cplusplus */
