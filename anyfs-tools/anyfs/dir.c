/*
 *	fs/any/dir.c
 *	Copyright (C) 2005-2006 
 *		Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#include "any.h"
#include <linux/highmem.h>
#include <linux/smp_lock.h>

//typedef struct minix_dir_entry minix_dirent;

static int any_readdir(struct file *, void *, filldir_t);

struct file_operations any_dir_operations = {
	.read           = generic_read_dir, 
	.readdir        = any_readdir,
	//.fsync          = minix_sync_file,
};

static int any_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	unsigned int ino;
	struct inode *inode = filp->f_dentry->d_inode;
	struct any_sb_info * info = inode->i_sb->s_fs_info;
	struct any_dirent *dentry;
	int ret = 0;

	lock_kernel();

	ino = inode->i_ino;

	switch (filp->f_pos) {
		case 0:
			if (filldir(dirent, ".", 1, 0, ino, DT_DIR) < 0)
				goto out;
			filp->f_pos++;
			/* fall through */
		case 1:
			if (filldir(dirent, "..", 2 /*Длина имени*/, 
						1 /*смещение в директории*/,
						parent_ino(filp->f_dentry)/*номер узла*/,
						DT_DIR/*тип*/) < 0)
				goto out;
			filp->f_pos++;
			/* fall through */
		default:
			dentry = info->si_inode_table[ino].i_info.dir->d_dirent;
			
			while (dentry && filp->f_pos > dentry->d_pos) {
				dentry = dentry->d_next;
			}

			while (dentry) {
				if (filldir(dirent, dentry->d_name, 
							strlen(dentry->d_name), 
							dentry->d_pos,
							dentry->d_inode, 
							info->si_inode_table
							[dentry->d_inode].i_mode
						       	>> 12) < 0)
					goto out;
				filp->f_pos = dentry->d_pos + 1;
				dentry = dentry->d_next;
			}
	}
	ret = 1;
out:    
	unlock_kernel();
	return ret;
}
