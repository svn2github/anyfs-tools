/*
 *	fs/any/namei.c
 *	Copyright (C) 2005-2006 
 *		Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#include <linux/smp_lock.h>
#include "any.h"

#ifndef	KERNEL_2_6_19_PLUS

#define	inode_dec_link_count(inode) 	\
	inode->i_nlink--; 		\
	mark_inode_dirty(inode);

#define drop_nlink(inode)		\
	inode->i_nlink--;

#define inc_nlink(inode)		\
	inode->i_nlink++;

#endif

extern void *any_malloc(size_t size);
extern void any_free(void *addr);

static struct dentry *any_lookup(struct inode * dir, struct dentry *dentry, 
		struct nameidata *nd)
{
	struct any_sb_info * info = dir->i_sb->s_fs_info;
	struct any_dirent *dirent;
	struct inode * inode = NULL;
	ino_t ino = 0;

	dentry->d_op = dir->i_sb->s_root->d_op;

	/*Проверка слишком длинного имени*/
	if (dentry->d_name.len > ANY_NAME_LENGTH)
		return ERR_PTR(-ENAMETOOLONG);

	/*Поиск inode по имени*/
	
	dirent = info->si_inode_table[dir->i_ino].i_info.dir->d_dirent;
	while (dirent) {
		if (strcmp(dirent->d_name, dentry->d_name.name)==0) {
			ino = dirent->d_inode;
			break;
		}
		dirent = dirent->d_next;
	}
	
	if (dirent) {
#ifdef KERNEL_2_6_25_PLUS
		inode = any_iget(dir->i_sb, ino);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
#else
		inode = iget(dir->i_sb, ino);
		if (!inode)
			return ERR_PTR(-EACCES);
#endif

	}

	d_add(dentry, inode);
	return NULL;
}

static int any_add_entry(struct inode * dir, const char * name, int ino)
{
	struct any_sb_info * info = dir->i_sb->s_fs_info;
	struct any_dirent *new_dirent;
	struct any_dirent *after_dirent;
	int	new_pos;
	struct any_dir *to_dir;
	int ret = 0;

	if (strlen(name) > ANY_NAME_LENGTH)
	{ ret = -ENAMETOOLONG; goto out; }

	new_dirent = any_malloc( sizeof(struct any_dirent));
	if (!new_dirent)
	{ ret = -ENOMEM; goto out; }
	memset(new_dirent, 0, sizeof(struct any_dirent));

	new_dirent->d_name =
		any_malloc(strlen(name)+1);
	if (!new_dirent->d_name)
	{ ret = -ENOMEM; goto free1_fail; }

	strcpy ( new_dirent->d_name, name );
	new_dirent->d_inode = ino;
	
	to_dir = info->si_inode_table[dir->i_ino].i_info.dir;

	if ( (2 + to_dir->d_ndirents) > 0xFFFFFFF0 )
	{ ret = -ENOSPC; goto free2_fail; }

	to_dir->d_ndirents++;

	if ( !to_dir->d_last )
	{
		to_dir->d_dirent =
			to_dir->d_last =
			new_dirent;
		new_pos = 2;
	}
	else
	{
		after_dirent = to_dir->d_last;
		new_pos = to_dir->d_last->d_pos+1;

		if ( new_pos > 0xFFFFFFF0 )
		{
			new_pos = 2;
			if ( to_dir->d_dirent->d_pos > new_pos )
			{
				new_dirent->d_next = to_dir->d_dirent;
				to_dir->d_dirent = new_dirent;
			}
			else
			{
				after_dirent = to_dir->d_dirent;
				new_pos = after_dirent->d_pos + 1;

				while ( after_dirent->d_next->d_pos == new_pos )
				{
					after_dirent = after_dirent->d_next;
					new_pos++;
				}

				new_dirent->d_next = after_dirent->d_next;
				after_dirent->d_next = new_dirent;
			}
		}
		else
		{
			new_dirent->d_next = NULL;
			after_dirent->d_next = new_dirent;
			to_dir->d_last = new_dirent;
		}
	}

	new_dirent->d_pos = new_pos;

	dir->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);

	return 0;

free2_fail:
	any_free(new_dirent->d_name);

free1_fail:
	any_free(new_dirent);
out:
	return (ret)?ret:-EINVAL;
}

static int any_del_entry(struct inode * dir, struct dentry * dentry)
{
	int error = -ENOENT;
	struct any_sb_info * info = dir->i_sb->s_fs_info;
	struct any_dirent **pdirent;
	struct any_dirent* free_de;
	struct inode * inode = dentry->d_inode;
	ino_t ino = 0;

#ifdef	ANYFS_DEBUG
	printk("anyfs: del_entry %s\n", dentry->d_name.name);
#endif
	
	pdirent = &info->si_inode_table[dir->i_ino].i_info.dir->d_dirent;
	while (*pdirent) {
		if (strcmp((*pdirent)->d_name, dentry->d_name.name)==0) {
			ino = (*pdirent)->d_inode;
			break;
		}
		pdirent = &(*pdirent)->d_next;
	}
	
	if ( !(*pdirent) || (ino != inode->i_ino) ) {
		return error;
	}

	info->si_inode_table[dir->i_ino].i_info.dir->d_ndirents--;
	any_free( (*pdirent)->d_name );
	
	free_de = *pdirent;
	(*pdirent) = (*pdirent)->d_next;
	
	if ( free_de == info->si_inode_table[dir->i_ino].i_info.dir->d_last )
	{
		info->si_inode_table[dir->i_ino].i_info.dir->d_last =
			info->si_inode_table[dir->i_ino].i_info.dir->d_dirent;
		if ( info->si_inode_table[dir->i_ino].i_info.dir->d_last )
		{
			while ( info->si_inode_table[dir->i_ino].i_info.dir->d_last->d_next )
				info->si_inode_table[dir->i_ino].i_info.dir->d_last =
					info->si_inode_table[dir->i_ino].i_info.dir->d_last->d_next;
		}
	}

	any_free (free_de);

	dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);
	
	return 0;
}

static int any_link (struct dentry * old, struct inode * dir,
		struct dentry *new)
{       
	struct inode *inode = old->d_inode;
	int err;

	if (inode->i_nlink >= ANY_LINK_MAX)
		return -EMLINK;

	lock_kernel();
	err = any_add_entry(dir, new->d_name.name, inode->i_ino);
	if (err) {
		unlock_kernel();
		return err;
	}
	inc_nlink(inode);
	inode->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
	atomic_inc(&inode->i_count);

	d_instantiate(new, inode);
	unlock_kernel();
	return 0;
}

static int any_unlink(struct inode * dir, struct dentry * dentry)
{
	int err = 0;
	struct inode * inode = dentry->d_inode;
	
#ifdef	ANYFS_DEBUG
	printk("anyfs: unlink. call any_del_entry\n");
#endif

	lock_kernel();
	err = any_del_entry(dir, dentry);
	if (err) {
		unlock_kernel();
		return err;
	}
	
	if (!inode->i_nlink) {
		inode->i_nlink = 1;
	}
	
	inode->i_ctime = dir->i_ctime;
	inode_dec_link_count(inode);
	err = 0;
	
#ifdef	ANYFS_DEBUG
	printk("anyfs: nlink = %d (ino=%ld)\n",
			inode->i_nlink, inode->i_ino);
#endif

	unlock_kernel();
	return err;
}

struct inode *any_new_inode(struct inode *dir, int mode, void* data)
{
	struct inode * inode;
	struct super_block * s = dir->i_sb;
	struct any_sb_info * info = s->s_fs_info;
	unsigned long ino;
	dev_t rdev = 0;

	inode = new_inode(s);
	if (!inode)
	{
		return ERR_PTR(-ENOSPC);
	}
	
	lock_kernel();
	ino = find_first_zero_bit(info->si_inode_bitmap, info->si_inodes);
	if (ino >= info->si_inodes) {
		unlock_kernel();
		iput(inode);
		return ERR_PTR(-ENOSPC);
	}
	
	set_bit(ino, info->si_inode_bitmap);
	
	inode->i_uid = current->fsuid;
	inode->i_gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
#ifdef KERNEL_2_6_19_PLUS
	inode->i_blocks = 0;
#else
	inode->i_blocks = inode->i_blksize = 0;
#endif
	inode->i_mode = mode;
	inode->i_ino = ino;
	
	if ( S_ISLNK(inode->i_mode) )
	{
		info->si_inode_table[ino].i_info.symlink =
			any_malloc(strlen(data)+1);
		if (!info->si_inode_table[ino].i_info.symlink)
		{
			unlock_kernel();
			iput(inode);
			return ERR_PTR(-ENOMEM);
		}

		strcpy ( info->si_inode_table[ino].i_info.symlink,
				data );
	}

	if ( S_ISDIR(inode->i_mode) )
	{
		info->si_inode_table[ino].i_info.dir =
			any_malloc(sizeof(struct any_dir));
		if (!info->si_inode_table[ino].i_info.dir)
		{
			unlock_kernel();
			iput(inode);
			return ERR_PTR(-ENOMEM);	       
		}
		memset(info->si_inode_table[ino].i_info.dir, 0,
				sizeof(struct any_dir));
	}

	if ( ! ( S_ISLNK(inode->i_mode) || 
				S_ISREG(inode->i_mode) || 
				S_ISDIR(inode->i_mode) ) )
	{
		info->si_inode_table[ino].i_info.device = 
			rdev = *((dev_t*)data);
	}

	any_set_inode(inode, rdev);

	insert_inode_hash(inode);
	mark_inode_dirty(inode);

	unlock_kernel();
	
	return inode;
}

static int any_symlink (struct inode * dir, struct dentry * dentry,
		        const char * symname)
{
	int err;
	struct inode *inode;

	if (strlen(symname) > (ANY_BUFFER_SIZE-4))
	{ err = -ENAMETOOLONG; return err; }
	
       	inode = any_new_inode(dir, S_IFLNK | S_IRWXUGO, (void*) symname);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	lock_kernel();
	err = any_add_entry(dir, dentry->d_name.name, inode->i_ino);
	if (err) {
		unlock_kernel();
		return err;
	}
	
	dir->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);
	
	unlock_kernel();
	d_instantiate(dentry, inode);
	return 0;
}

static int any_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	int err;
	struct inode *inode;

	if (dir->i_nlink >= ANY_LINK_MAX)
		return -EMLINK;
	
#ifdef	ANYFS_DEBUG
	printk("anyfs: mkdir?\n");
#endif

	inc_nlink(dir);
	dir->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);
	
#ifdef	ANYFS_DEBUG
	printk("anyfs: dir->i_nlink = %d (ino=%ld)\n", 
			dir->i_nlink, dir->i_ino);
#endif
	
       	inode = any_new_inode(dir, S_IFDIR | mode, NULL);
	if (IS_ERR(inode))
	{
		err = PTR_ERR(inode);
		goto dir_fail;
	}
	
	inc_nlink(inode);
	mark_inode_dirty(inode);
	
#ifdef	ANYFS_DEBUG
	printk("anyfs: inode->i_nlink = %d (ino=%ld)\n", 
			inode->i_nlink, inode->i_ino);
#endif
	
	lock_kernel();
	err = any_add_entry(dir, dentry->d_name.name, inode->i_ino);
	if (err) {
		goto dir_fail2;
	}

	unlock_kernel();
	d_instantiate(dentry, inode);
	return 0;

dir_fail2:
	unlock_kernel();

dir_fail:
	inode_dec_link_count(dir);
	
	return err;
}

static int any_rmdir (struct inode * dir, struct dentry *dentry)
{
	struct inode * inode = dentry->d_inode;
	struct any_sb_info * info = dir->i_sb->s_fs_info;
	int err = -ENOTEMPTY;

#ifdef	ANYFS_DEBUG
	printk("anyfs: unlink dir?\n");
#endif
	if (!info->si_inode_table[inode->i_ino].i_info.dir->d_ndirents) {
#ifdef	ANYFS_DEBUG
		printk("anyfs: unlink dir!\n");
#endif
		err = any_unlink(dir, dentry);
		if (!err) {
			inode_dec_link_count(inode);
			inode_dec_link_count(dir);
		}
	}
	return err;
}

static int any_mknod (struct inode * dir, struct dentry *dentry, int mode, dev_t rdev)
{
	struct inode * inode;
	int err;

	if (!new_valid_dev(rdev))
		return -EINVAL;

       	inode = any_new_inode(dir, mode, &rdev);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	lock_kernel();
	err = any_add_entry(dir, dentry->d_name.name, inode->i_ino);
	if (err) {
		unlock_kernel();
		return err;
	}
	
	dir->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);
	
	unlock_kernel();
	d_instantiate(dentry, inode);
	return 0;
}

static int any_rename(struct inode * old_dir, struct dentry * old_dentry,
		struct inode * new_dir, struct dentry * new_dentry)
{
	struct inode *old_inode = old_dentry->d_inode;
	struct inode *new_inode = new_dentry->d_inode;
	int is_dir;
	int err = 0;
	
	is_dir = S_ISDIR(old_inode->i_mode);

	lock_kernel();
	if (new_inode)
	{
		struct any_sb_info * info = old_dir->i_sb->s_fs_info;

		if ( is_dir && info->si_inode_table[new_inode->i_ino].
				i_info.dir->d_ndirents ) {
			err = -ENOTEMPTY;
			goto out;
		}
		
		err = any_unlink(new_dir, new_dentry);
		if (!err) {
			if (is_dir)
			{
				inode_dec_link_count(new_inode);
				inode_dec_link_count(new_dir);
			}
		}
		else goto out;
	}

	err = any_add_entry(new_dir, new_dentry->d_name.name, old_inode->i_ino);
	if (err) {
		unlock_kernel();
		return err;
	}
	
	err = any_del_entry(old_dir, old_dentry);
	if (err) {
		unlock_kernel();
		return err;
	}

	if (is_dir)
	{
		inode_dec_link_count(old_dir);
		inc_nlink(new_dir);
		mark_inode_dirty(new_dir);
	}
	
	old_inode->i_ctime = old_dir->i_ctime;
	mark_inode_dirty(old_inode);
	
	err = 0;
	
out:
	unlock_kernel();
	
	return err;
}

/*
 * directories can hadle most operations...
 */
struct inode_operations any_dir_inode_operations = {
	//.create         = any_create,
	.lookup         = any_lookup,
	.link           = any_link,
	.unlink         = any_unlink,
	.symlink        = any_symlink,
	.mkdir          = any_mkdir,
	.rmdir          = any_rmdir,
	.mknod          = any_mknod,
	.rename         = any_rename,
};

