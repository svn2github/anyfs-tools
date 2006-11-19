#include "any.h"
#include <linux/namei.h>

#ifdef KERNEL_2_6_13_PLUS
static void *any_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	struct any_sb_info * info = inode->i_sb->s_fs_info;
	nd_set_link(nd, info->si_inode_table[inode->i_ino].i_info.symlink);
	return NULL;
}
#else
static int any_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	struct any_sb_info * info = inode->i_sb->s_fs_info;
	nd_set_link(nd, info->si_inode_table[inode->i_ino].i_info.symlink);
	return 0;
}
#endif

struct inode_operations any_symlink_inode_operations = {
	.readlink       = generic_readlink,
	.follow_link    = any_follow_link,
};

