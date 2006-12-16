/*
 *	fs/any/inode.c
 *	Copyright (C) 2005-2006 
 *		Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 *
 *      From fs/minix, Copyright (C) 1991, 1992 Linus Torvalds.
 *	From fs/bfs, Copyright (C) 1999,2000 Tigran Aivazian.
 *	From fs/ntfs, Copyright (C) 2001-2004 Anton Altaparmakov.
 *	From fs/proc, Copyright (C) 1991, 1992  Linus Torvalds.
 *	From fs/fat, fs/msdos Copyright (C) 1992,1993 by Werner Almesberger
 *
 *	From fs/ext2
 *	Copyright (C) 1992, 1993, 1994, 1995
 *	Remy Card (card@masi.ibp.fr)
 *	Laboratoire MASI - Institut Blaise Pascal
 *	Universite Pierre et Marie Curie (Paris VI)
 *
 */

#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/genhd.h>
#include <linux/vmalloc.h>
#include <linux/a.out.h>

#include "any.h"

#include <asm/uaccess.h>

#define SEEK_SET	0
#define SEEK_CUR	1
#define SEEK_END	2

MODULE_AUTHOR("Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>");
MODULE_DESCRIPTION("ANYFS filesystem with external inode table");
MODULE_LICENSE("GPL");

void *any_malloc(size_t size)
{
	void *vm = NULL;

	if ( (size+1)>PAGE_SIZE )
	{
		vm = vmalloc( size + 1 );
		*(char*) vm = 1;
		return (char*)vm + 1;
	}
	else
	{
		vm = kmalloc( size + 1, GFP_KERNEL );
		*(char*) vm = 0;
		return (char*)vm + 1;
	}
}

void any_free(void *addr)
{
	void *link = (char*)addr - 1;
	if ( *(char*) link )
		vfree(link);
	else	kfree(link);
}

static int any_sbmap(struct inode *inode, sector_t sector, sector_t *phys)
{
	struct any_sb_info * info = inode->i_sb->s_fs_info;
	int ibs = (info->si_blocksize > 4096)? info->si_blocksize/4096 :1;
	unsigned long d = 0;
	int i;
	unsigned long block = 0;
	unsigned long fr_length;
	unsigned long fr_start;
	int osc;

	while (ibs>1) { d++; ibs>>=1; }

	osc = sector&( (1<<d) - 1);
	sector>>=d;
	
	for (i=0; i<info->si_inode_table[inode->i_ino].
			i_info.file_frags->fr_nfrags; i++)
	{
		fr_length = info->si_inode_table[inode->i_ino].
			i_info.file_frags->fr_frags[i].fr_length;
		if ( (block + fr_length)>sector ) {
			fr_start = info->si_inode_table[inode->i_ino].
				i_info.file_frags->fr_frags[i].fr_start;
			
			*phys = (fr_start) ? (fr_start + sector - block)<<d | osc : 0;
			return 0;
		}
		
		block += fr_length;
	}
	
	return -1;
}

static int any_get_block(struct inode *inode, sector_t iblock,
		struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	sector_t phys;
	int err;

	if ( !S_ISREG(inode->i_mode) ) return -EPERM;

	err = any_sbmap(inode, iblock, &phys);
	if (err)
		return err;
	if (phys>=0) {
		/*sparse blocks not need to map*/
		if (phys) map_bh(bh_result, sb, phys);
		return 0;
	}
	if (!create)
		return 0;
	return -EPERM;
}


static int any_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, any_get_block);
}

static sector_t any_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, any_get_block);
}

struct address_space_operations any_aops = {
	.readpage       = any_readpage,        /* Fill page with data. */
	.sync_page      = block_sync_page,      /* Currently, just unplugs the
						   disk request queue. */
	.bmap           = any_bmap,
};

struct address_space_operations any_empty_aops;

void any_set_inode(struct inode *inode, dev_t rdev)
{
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &any_file_inode_operations;
		inode->i_fop = &any_file_operations;
		inode->i_mapping->a_ops = &any_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &any_dir_inode_operations;
		inode->i_fop = &any_dir_operations;
		inode->i_mapping->a_ops = &any_empty_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &any_symlink_inode_operations;
		inode->i_fop = &any_file_operations;
		inode->i_mapping->a_ops = &any_empty_aops;
	} else
	    init_special_inode(inode, inode->i_mode, rdev);
}

static void any_read_inode(struct inode * inode)
{
	struct any_sb_info * info = inode->i_sb->s_fs_info;
	int i;
	dev_t rdev;
	/*заполняет структуру inode по inode->i_ino
	  в том числе заполняет inode->i_op
	  для определения допустимых операций*/
	
#ifdef	ANYFS_DEBUG
	printk("anyfs: read inode %ld\n", inode->i_ino);
#endif	/*ANYFS_DEBUG*/

	inode->i_mode  = info->si_inode_table[inode->i_ino].i_mode;
	inode->i_uid   = info->si_inode_table[inode->i_ino].i_uid;
	inode->i_gid   = info->si_inode_table[inode->i_ino].i_gid;
	inode->i_size  = info->si_inode_table[inode->i_ino].i_size;

	inode->i_atime.tv_sec = info->si_inode_table[inode->i_ino].i_atime;
	inode->i_ctime.tv_sec = info->si_inode_table[inode->i_ino].i_ctime;
	inode->i_mtime.tv_sec = info->si_inode_table[inode->i_ino].i_mtime;

	inode->i_nlink = info->si_inode_table[inode->i_ino].i_links_count;

	inode->i_blksize = info->si_blocksize;
	inode->i_blocks = 0;
	
	if ( S_ISREG(inode->i_mode) )
	{
		unsigned long d = 0;
		typeof(info->si_blocksize) bs = info->si_blocksize;
		
		for (i=0; i<info->si_inode_table[inode->i_ino].
				i_info.file_frags->fr_nfrags; i++)
			inode->i_blocks += info->si_inode_table[inode->i_ino].
				i_info.file_frags->fr_frags[i].fr_length;

		while (bs>512) { d++; bs>>=1; }
		inode->i_blocks = inode->i_blocks << d;
	}

	if ( S_ISLNK(inode->i_mode) || 
			S_ISREG(inode->i_mode) ||
			S_ISDIR(inode->i_mode) ) 
		rdev = 0;
	else
		rdev = info->si_inode_table[inode->i_ino].i_info.device;

	any_set_inode(inode, rdev);
}

static int any_write_inode(struct inode * inode, int unused)
{
	struct any_sb_info * info = inode->i_sb->s_fs_info;
	
#ifdef	ANYFS_DEBUG
	printk("anyfs: write inode %ld\n", inode->i_ino);
#endif	/*ANYFS_DEBUG*/

	info->si_inode_table[inode->i_ino].i_mode  = inode->i_mode;
	info->si_inode_table[inode->i_ino].i_uid   = inode->i_uid;
	info->si_inode_table[inode->i_ino].i_gid   = inode->i_gid;
	info->si_inode_table[inode->i_ino].i_size  = inode->i_size;

	info->si_inode_table[inode->i_ino].i_atime  = inode->i_atime.tv_sec;
	info->si_inode_table[inode->i_ino].i_ctime  = inode->i_ctime.tv_sec;
	info->si_inode_table[inode->i_ino].i_mtime  = inode->i_mtime.tv_sec;

	info->si_inode_table[inode->i_ino].i_links_count  = inode->i_nlink;

	return 0;
}

static void any_delete_inode (struct inode * inode)
{       
	struct any_sb_info * info = inode->i_sb->s_fs_info;
	unsigned long ino = inode->i_ino;

	if (is_bad_inode(inode))
		goto no_delete;

#ifdef	ANYFS_DEBUG
	printk("anyfs: delete_inode %ld with count %d\n", 
			inode->i_ino, info->si_inode_table[ino].i_links_count);
#endif	/*ANYFS_DEBUG*/

#ifdef KERNEL_2_6_13_PLUS
	truncate_inode_pages(&inode->i_data, 0);
#endif

	inode->i_size = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;
	lock_kernel();
	mark_inode_dirty(inode);

	if ( S_ISLNK(info->si_inode_table[ino].i_mode) ) {
		if (!info->si_inode_table[ino].i_info.symlink) goto out;
		any_free( info->si_inode_table[ino].i_info.symlink );
		info->si_inode_table[ino].i_info.symlink = NULL;
	}

	if ( S_ISREG(info->si_inode_table[ino].i_mode) ) {
		if (!info->si_inode_table[ino].i_info.file_frags) goto out;
		if (info->si_inode_table[ino].i_info.
				file_frags->fr_frags) {
			any_free( info->si_inode_table[ino].i_info.
					file_frags->fr_frags );
			info->si_inode_table[ino].i_info.
				file_frags->fr_frags = NULL;
		}
		any_free( info->si_inode_table[ino].i_info.file_frags );
		info->si_inode_table[ino].i_info.file_frags = NULL;
	}

	if ( S_ISDIR(info->si_inode_table[ino].i_mode) ) {
		struct any_dirent       *dirent;
		struct any_dirent       *nextdirent;

		if (!info->si_inode_table[ino].i_info.dir) goto out;

		dirent = info->si_inode_table[ino].i_info.
			dir->d_dirent;
		for (; dirent;
				dirent = nextdirent) {
			if (dirent->d_name)
				any_free( dirent->d_name );
			nextdirent = dirent->d_next;
			any_free( dirent );
		}

		any_free( info->si_inode_table[ino].i_info.dir );
		info->si_inode_table[ino].i_info.dir = NULL;
	}

out:
	info->si_inode_table[ino].i_links_count = 0;

	clear_bit(ino, info->si_inode_bitmap);
no_delete:
	unlock_kernel();
	clear_inode(inode);     /* We must guarantee clearing of inode... */
}

int any_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *vi = dentry->d_inode;
	int err;
	unsigned int ia_valid = attr->ia_valid;
	
#ifdef	ANYFS_DEBUG
	printk("anyfs: setattr\n");
#endif	/*ANYFS_DEBUG*/

	err = inode_change_ok(vi, attr);
	if (err)
		return err;

	if (ia_valid & ATTR_MODE)
		vi->i_mode  = attr->ia_mode;
	
	if (ia_valid & ATTR_UID)
		vi->i_uid  = attr->ia_uid;
	
	if (ia_valid & ATTR_GID)
		vi->i_gid  = attr->ia_gid;
	
	if (ia_valid & ATTR_SIZE) {
		if (attr->ia_size != i_size_read(vi)) {
			err = -EOPNOTSUPP;
			if (err || ia_valid == ATTR_SIZE)
				goto out;
		} else {
			/*
			 * We skipped the truncate but must still update
			 * timestamps.
			 */
			ia_valid |= ATTR_MTIME|ATTR_CTIME;
		}
	}
	
	if (ia_valid & ATTR_ATIME)
		vi->i_atime = attr->ia_atime;
	if (ia_valid & ATTR_MTIME)
		vi->i_mtime = attr->ia_mtime;
	if (ia_valid & ATTR_CTIME)
		vi->i_ctime = attr->ia_ctime;
	mark_inode_dirty(vi);
out:
	return err;
}

static void any_write_inodetable (struct super_block * sb)
{
	struct any_sb_info * info = sb->s_fs_info;
	struct inode * inode;
	struct file * file = NULL;
	mm_segment_t orig_fs;
	char buffer[ANY_BUFFER_SIZE+1];
	size_t len;
	int rb;
	unsigned long mpos;

	lock_kernel();

	/*open inode table file for write only*/
	file = filp_open(info->si_itfilename, O_WRONLY | O_CREAT | 
			O_NOFOLLOW | O_TRUNC, 0);
	if (IS_ERR(file))
		goto out;
	inode = file->f_dentry->d_inode;
	if (!S_ISREG(inode->i_mode))
		goto close_fail;
	if (!file->f_op)
		goto close_fail;
	if (!file->f_op->write)
		goto close_fail;

	file->f_pos = 0;

	/*Is it necessary?*/
	orig_fs = get_fs();
	set_fs(KERNEL_DS);

	file->f_pos = ANY_DATA_HEAD_OFFSET(info->si_inodes);
	
	len = strlen(ANY_DATA_HEAD);
	rb = file->f_op->write(file, ANY_DATA_HEAD,
			len, &file->f_pos);
	if ( rb < len ) goto close_fail2;

	for (mpos=0; mpos < info->si_inodes; mpos++) {
		if ( !info->si_inode_table[mpos].i_links_count ) continue;
		
		info->si_inode_table[mpos].i_it_file_offset = 
			file->f_pos - ANY_DATA_OFFSET(info->si_inodes);

		if ( S_ISLNK(info->si_inode_table[mpos].i_mode) ) {
			len = strlen(ANY_LNK_HEAD);
			rb = file->f_op->write(file, ANY_LNK_HEAD,
					len, &file->f_pos);
			if ( rb < len ) goto close_fail2;

			len = strlen(info->si_inode_table[mpos].
					i_info.symlink) + 1;
			rb = file->f_op->write(file, 
					info->si_inode_table[mpos].
					i_info.symlink,
					len, &file->f_pos);
			if ( rb < len ) goto close_fail2;

			len = 1;
			rb = file->f_op->write(file, "\n", len, &file->f_pos);
			if ( rb < len ) goto close_fail2;
		}

		if ( S_ISREG(info->si_inode_table[mpos].i_mode) ) {
			unsigned long i;

			len = snprintf (buffer, ANY_BUFFER_SIZE, "%s%08x\n",
					ANY_REG_HEAD,
					info->si_inode_table[mpos].i_info.
					file_frags->fr_nfrags );
			rb = file->f_op->write(file, buffer,
					len, &file->f_pos);
			if ( rb < len ) goto close_fail2;

			for (i=0; i < info->si_inode_table[mpos].i_info.
					file_frags->fr_nfrags; i++)
			{
				len = snprintf (buffer, ANY_BUFFER_SIZE, 
						"%08x %08x\n",
						info->si_inode_table[mpos].
						i_info.file_frags->fr_frags[i].
						fr_start,

						info->si_inode_table[mpos].
						i_info.file_frags->fr_frags[i].
						fr_length );
				rb = file->f_op->write(file, 
						buffer,
						len, &file->f_pos);
				if ( rb < len ) goto close_fail2;
			}

		}

		if ( S_ISDIR(info->si_inode_table[mpos].i_mode) ) {
			struct any_dirent	*dirent;
			
			len = snprintf (buffer, ANY_BUFFER_SIZE, "%s%08x\n",
					ANY_DIR_HEAD,
					info->si_inode_table[mpos].i_info.
					dir->d_ndirents );
			rb = file->f_op->write(file, buffer,
					len, &file->f_pos);
			if ( rb < len ) goto close_fail2;

			dirent = info->si_inode_table[mpos].i_info.
				dir->d_dirent;
			for (; dirent; dirent = dirent->d_next) {
				len = strlen(dirent->d_name) + 1;
				rb = file->f_op->write(file, 
						dirent->d_name,
						len, &file->f_pos);
				if ( rb < len ) goto close_fail2;
				
				len = snprintf (buffer, ANY_BUFFER_SIZE, 
						"%08x\n",
						dirent->d_inode );
				rb = file->f_op->write(file, 
						buffer,
						len, &file->f_pos);
				if ( rb < len ) goto close_fail2;
			}
		}
	}

	file->f_pos = 0;
	
	len = snprintf (buffer, ANY_BUFFER_SIZE, "%s%08lx\n",
			ANY_BLOCK_SIZE_HEAD, info->si_blocksize);
	rb = file->f_op->write(file, buffer,
			len, &file->f_pos);
	if ( rb < len ) goto close_fail2;
	
	len = snprintf (buffer, ANY_BUFFER_SIZE, "%s%08lx\n",
			ANY_INODES_HEAD, info->si_inodes);
	rb = file->f_op->write(file, buffer,
			len, &file->f_pos);
	if ( rb < len ) goto close_fail2;
	
	len = strlen(ANY_INODE_TABLE_HEAD);
	rb = file->f_op->write(file, ANY_INODE_TABLE_HEAD,
			len, &file->f_pos);
	if ( rb < len ) goto close_fail2;
	
	for (mpos=0; mpos < info->si_inodes; mpos++) {
		uint16_t i_mode;
		i_mode = info->si_inode_table[mpos].i_mode;
		len = snprintf (buffer, ANY_BUFFER_SIZE, 
				"%04x %04x %04x %016llx %08x %08x %08x %04x %08x\n",
				info->si_inode_table[mpos].i_mode,
				info->si_inode_table[mpos].i_uid,
				info->si_inode_table[mpos].i_gid,
				info->si_inode_table[mpos].i_size,
				info->si_inode_table[mpos].i_atime,
				info->si_inode_table[mpos].i_ctime,
				info->si_inode_table[mpos].i_mtime,
				info->si_inode_table[mpos].i_links_count,
				(S_ISLNK(i_mode) ||
				 S_ISREG(i_mode) ||
				 S_ISDIR(i_mode))?
				info->si_inode_table[mpos].i_it_file_offset:
				info->si_inode_table[mpos].i_info.device );
		rb = file->f_op->write(file, buffer,
				len, &file->f_pos);
		if ( rb < len ) goto close_fail2;
	}
	
	file->f_pos = 0;
	
close_fail2:
	set_fs(orig_fs);

close_fail:
	filp_close(file, NULL);
	
out:
	unlock_kernel();
}

static void any_put_super(struct super_block *sb)
{
	struct any_sb_info * info = sb->s_fs_info;
	unsigned long mpos;

#ifdef	ANYFS_DEBUG
	printk("anyfs: put_super\n");
#endif	/*ANYFS_DEBUG*/

	if (!(sb->s_flags & MS_RDONLY)) {
		any_write_inodetable (sb);
	}

	for (mpos=0; mpos < info->si_inodes; mpos++) {
		if ( !info->si_inode_table[mpos].i_links_count ) continue;

		if ( S_ISLNK(info->si_inode_table[mpos].i_mode) ) {
			if (!info->si_inode_table[mpos].i_info.symlink) continue;
			any_free( info->si_inode_table[mpos].i_info.symlink );
		}

		if ( S_ISREG(info->si_inode_table[mpos].i_mode) ) {
			if (!info->si_inode_table[mpos].i_info.file_frags) continue;
			if (info->si_inode_table[mpos].i_info.
					file_frags->fr_frags)
				any_free( info->si_inode_table[mpos].i_info.
						file_frags->fr_frags );
			any_free( info->si_inode_table[mpos].i_info.file_frags );
		}

		if ( S_ISDIR(info->si_inode_table[mpos].i_mode) ) {
			struct any_dirent	*dirent;
			struct any_dirent       *nextdirent;

			if (!info->si_inode_table[mpos].i_info.dir) continue;

			dirent = info->si_inode_table[mpos].i_info.
				dir->d_dirent;
			for (; dirent; 
					dirent = nextdirent) {
				if (dirent->d_name)
					any_free( dirent->d_name );
				nextdirent = dirent->d_next;
				any_free( dirent );
			}

			any_free( info->si_inode_table[mpos].i_info.dir );
		}
	}

	any_free(info->si_inode_table);

	any_free(info->si_inode_bitmap);

	any_free(info->si_itfilename);

	any_free(info);    

	sb->s_fs_info = NULL;

	return;
}

#ifdef KERNEL_2_6_18_PLUS
static int any_statfs(struct dentry *dentry, struct kstatfs *buf)
#else
static int any_statfs(struct super_block *sb, struct kstatfs *buf)
#endif
{               
#ifdef KERNEL_2_6_18_PLUS
	struct super_block *sb = dentry->d_sb;
#endif
	struct any_sb_info * info = sb->s_fs_info;
	unsigned long d = 0;
	typeof(info->si_blocksize) bs = info->si_blocksize;
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	while (bs>512) { d++; bs>>=1; }

	buf->f_type = ANY_SUPER_MAGIC;
	buf->f_bsize = info->si_blocksize;
	if (sb->s_bdev->bd_part)
		buf->f_blocks = sb->s_bdev->bd_part->nr_sects >> d;
	else if ( sb->s_bdev->bd_disk)
		buf->f_blocks = sb->s_bdev->bd_disk->capacity >> d;
	else buf->f_blocks = 0;
	buf->f_bfree = buf->f_bavail = 0;
	buf->f_files = info->si_inodes;
	buf->f_ffree = info->si_inodes - 
		bitmap_weight(info->si_inode_bitmap, info->si_inodes);
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);
	buf->f_namelen = 255;
	return 0;
}

static struct super_operations any_sops = {
	.read_inode     = any_read_inode,
	.write_inode    = any_write_inode,
	.delete_inode   = any_delete_inode,
	.statfs         = any_statfs,
	.put_super      = any_put_super,
	//.write_super	= any_write_super,
	//.remount_fs     = any_remount,
};                                           

static int any_fill_super(struct super_block *s, void *data, int silent)
{       
	struct any_sb_info * info;
	struct inode *root_inode;
	const char *options = (const char *) data;
	char *itfile;
	char *itfilename_end;
	int itfilename_len;
	struct inode * inode;
	struct file * file = NULL;
	mm_segment_t orig_fs;
	char buffer[ANY_BUFFER_SIZE+1];
	int rb;
	unsigned long bitmap_l;
	size_t leavbyte;
	size_t bufpos;
	unsigned long mpos;
	int ret=0;
	char *fullpath;

	/*get name of inode table file from option string*/
	if (!options) return -EINVAL;
	itfile = strstr ( options, ANY_ITOPTIONS);
	if (!itfile) return -EINVAL;
	itfile += strlen(ANY_ITOPTIONS);
	itfilename_end = strchr (itfile, ',');

	if (itfilename_end) itfilename_len = itfilename_end - itfile;
	else itfilename_len = strlen(itfile);

	strncpy( buffer, itfile, 
			min_t(int, itfilename_len, ANY_BUFFER_SIZE) );
	buffer[itfilename_len] = '\0';

	info = any_malloc(sizeof(*info));
	if (!info)
		return -ENOMEM;
	memset(info, 0, sizeof(*info));

	/*open inode table file for read only*/
	file = filp_open(buffer, O_RDONLY, 0);
	if (IS_ERR(file))
		goto free_info;
	inode = file->f_dentry->d_inode;
	if (!S_ISREG(inode->i_mode))
		goto close_fail;
	if (!file->f_op)
		goto close_fail;
	if (!file->f_op->read)
		goto close_fail;

	fullpath = d_path(file->f_dentry, file->f_vfsmnt, buffer, ANY_BUFFER_SIZE);
	if (IS_ERR(fullpath))
	{
		ret = PTR_ERR(inode);
		goto close_fail;
	}

	itfilename_len = strlen(fullpath);

	info->si_itfilename = 
		any_malloc(itfilename_len+1);
	if (!info->si_itfilename) 
	{ ret = -ENOMEM; goto close_fail; }

	strcpy ( info->si_itfilename, fullpath );

	file->f_pos = 0;

	/*Is it necessary?*/
	orig_fs = get_fs();
	set_fs(KERNEL_DS);

	/*
	 *	read block size, number of inodes and
	 *	inode bitmap head
	 */
	bufpos = file->f_pos;
	rb = file->f_op->read(file, buffer,
			ANY_BLOCK_SIZE_LEN + ANY_INODES_LEN
			+ strlen(ANY_INODE_TABLE_HEAD),
			&file->f_pos);
	if ( rb == -1 ) goto close_fail2;

	buffer[rb] = '\0';

	/*check block size, number of inodes and inode bitmap heads*/
	if (strncmp(buffer + ANY_BLOCK_SIZE_HEAD_OFFSET, 
				ANY_BLOCK_SIZE_HEAD, 
				strlen(ANY_BLOCK_SIZE_HEAD))!=0)
		goto close_fail2;
	if (strncmp(buffer + ANY_INODES_HEAD_OFFSET, 
				ANY_INODES_HEAD, 
				strlen(ANY_INODES_HEAD))!=0)
		goto close_fail2;
	if (strncmp(buffer + ANY_INODE_TABLE_HEAD_OFFSET, 
				ANY_INODE_TABLE_HEAD, 
				strlen(ANY_INODE_TABLE_HEAD))!=0)
		goto close_fail2;

	info->si_blocksize = simple_strtoul (buffer + ANY_BLOCK_SIZE_OFFSET,
			NULL, 16);
	info->si_inodes = simple_strtoul (buffer + ANY_INODES_OFFSET,
			NULL, 16);

	if ( (info->si_blocksize>4096 && info->si_blocksize%4096) || 
			!sb_set_blocksize(s, (info->si_blocksize > 4096)?
				4096:info->si_blocksize ) )
	{
		printk("AnyFS: bad blocksize.\n");
		goto close_fail2;
	}

	bitmap_l = ( info->si_inodes + 
			sizeof(unsigned long) - 1 )/sizeof(unsigned long);

	/*allocate memory for inode bitmap*/
	info->si_inode_bitmap = 
		any_malloc(bitmap_l*sizeof(unsigned long));
	if (!info->si_inode_bitmap) 
	{ ret = -ENOMEM; goto close_fail2; }
	memset(info->si_inode_bitmap, 0,
			bitmap_l*sizeof(unsigned long));

	set_bit(0, info->si_inode_bitmap);

	/*read inode table*/
	info->si_inode_table = 
		any_malloc(info->si_inodes*sizeof(struct any_inode));
	if (!info->si_inode_table) 
	{ ret = -ENOMEM; goto free2_fail; }
	memset (info->si_inode_table, 0,
			info->si_inodes*sizeof(struct any_inode));

	leavbyte = ANY_INODE_TABLE_INFOLEN(info->si_inodes);

	mpos = 0;

	while (leavbyte) {
		unsigned long bpos;

		bufpos = file->f_pos;
		rb = file->f_op->read(file, buffer,
				min_t(size_t, leavbyte, ANY_BUFFER_SIZE),
				&file->f_pos);
		if ( rb == -1 ) goto free3_fail;
		buffer[rb] = '\0';

		leavbyte -= rb;

		if (rb<73) goto free3_fail;

		for (bpos = 0; (bpos+73)<=rb; bpos+=73)
		{
			uint16_t i_mode;

			i_mode = info->si_inode_table[mpos].i_mode =
				simple_strtoul (buffer + bpos + 0,
						NULL, 16);

			info->si_inode_table[mpos].i_uid =
				simple_strtoul (buffer + bpos + 5,
						NULL, 16);

			info->si_inode_table[mpos].i_gid =
				simple_strtoul (buffer + bpos + 10,
						NULL, 16);

			info->si_inode_table[mpos].i_size =
				simple_strtoull (buffer + bpos + 15,
						NULL, 16);

			info->si_inode_table[mpos].i_atime =
				simple_strtoul (buffer + bpos + 32,
						NULL, 16);

			info->si_inode_table[mpos].i_ctime =
				simple_strtoul (buffer + bpos + 41,
						NULL, 16);

			info->si_inode_table[mpos].i_mtime =
				simple_strtoul (buffer + bpos + 50,
						NULL, 16);

			info->si_inode_table[mpos].i_links_count =
				simple_strtoul (buffer + bpos + 59,
						NULL, 16);

			if ( S_ISLNK(i_mode) || 
					S_ISREG(i_mode) ||
					S_ISDIR(i_mode) ) {
				info->si_inode_table[mpos].i_it_file_offset =
					simple_strtoul (buffer + bpos + 64,
							NULL, 16);
			}
			else {
				info->si_inode_table[mpos].i_info.
					device =
					simple_strtoul (buffer + bpos + 64,
							NULL, 16);
			}

			mpos++;
		}
		leavbyte += rb-bpos;

		file->f_pos-=rb-bpos;
	}

	/*read data head*/
	bufpos = file->f_pos;
	rb = file->f_op->read(file, buffer,
			strlen(ANY_DATA_HEAD),
			&file->f_pos);
	if ( rb == -1 ) goto free3_fail;

	if (strncmp(buffer, 
				ANY_DATA_HEAD, 
				strlen(ANY_DATA_HEAD))!=0)
		goto free3_fail;

	/*read data*/
	for (mpos=1; mpos<info->si_inodes; mpos++) {
		if (!info->si_inode_table[mpos].i_links_count) continue;
		set_bit(mpos, info->si_inode_bitmap);

		if ( S_ISLNK(info->si_inode_table[mpos].i_mode) ) {
			unsigned long bpos;

			file->f_pos = ANY_DATA_OFFSET(info->si_inodes) +
				info->si_inode_table[mpos].i_it_file_offset;

			bufpos = file->f_pos;
			rb = file->f_op->read(file, buffer,
					ANY_BUFFER_SIZE,
					&file->f_pos);
			if ( rb == -1 ) goto free4_fail;
			buffer[rb] = '\0';

			if (strncmp(buffer, 
						ANY_LNK_HEAD, 
						strlen(ANY_LNK_HEAD))!=0)
				goto free4_fail;

			bpos = strlen(buffer+strlen(ANY_LNK_HEAD)) + 1;

			info->si_inode_table[mpos].i_info.symlink = 
				any_malloc(bpos);
			if (!info->si_inode_table[mpos].i_info.symlink) 
			{ ret = -ENOMEM; goto free4_fail; }

			strcpy ( info->si_inode_table[mpos].i_info.symlink,
					buffer+strlen(ANY_LNK_HEAD) );
		}

		if ( S_ISREG(info->si_inode_table[mpos].i_mode) ) {
			unsigned long bpos;
			unsigned long i;

			file->f_pos = ANY_DATA_OFFSET(info->si_inodes) +
				info->si_inode_table[mpos].i_it_file_offset;

			bufpos = file->f_pos;
			rb = file->f_op->read(file, buffer,
					ANY_BUFFER_SIZE,
					&file->f_pos);
			if ( rb == -1 ) goto free4_fail;
			buffer[rb] = '\0';

			if (strncmp(buffer, 
						ANY_REG_HEAD, 
						strlen(ANY_REG_HEAD))!=0)
				goto free4_fail;

			info->si_inode_table[mpos].i_info.file_frags = 
				any_malloc(sizeof(struct any_file_frags));
			if (!info->si_inode_table[mpos].i_info.file_frags) 
			{ ret = -ENOMEM; goto free4_fail; }
			memset(info->si_inode_table[mpos].i_info.file_frags, 0,
					sizeof(struct any_file_frags));

			bpos = strlen(ANY_REG_HEAD);

			info->si_inode_table[mpos].i_info.file_frags->fr_nfrags = 
				simple_strtoul (buffer + bpos,
						NULL, 16);

			info->si_inode_table[mpos].i_info.file_frags->fr_frags = 
				any_malloc(info->si_inode_table[mpos].i_info.
						file_frags->fr_nfrags*
						sizeof(struct 
							any_file_fragment));
			if (!info->si_inode_table[mpos].
					i_info.file_frags->fr_frags) 
			{ ret = -ENOMEM; goto free4_fail; }
			memset(info->si_inode_table[mpos].i_info.
					file_frags->fr_frags, 0,
					info->si_inode_table[mpos].i_info.
					file_frags->fr_nfrags*
					sizeof(struct any_file_fragment));

			bpos += 9;

			for (i=0; i < info->si_inode_table[mpos].i_info.
					file_frags->fr_nfrags; i++)
			{
				if ( (bpos+18) > rb ) {
					file->f_pos -= rb-bpos;

					bufpos = file->f_pos;
					rb = file->f_op->read(file, buffer,
							ANY_BUFFER_SIZE,
							&file->f_pos);
					if ( rb == -1 ) goto free4_fail;
					buffer[rb] = '\0';

					bpos=0;
					if (rb<18) goto free4_fail;
				}

				info->si_inode_table[mpos].i_info.
					file_frags->fr_frags[i].fr_start = 
					simple_strtoul (buffer + bpos,
							NULL, 16);

				bpos+=9;

				info->si_inode_table[mpos].i_info.
					file_frags->fr_frags[i].fr_length = 
					simple_strtoul (buffer + bpos,
							NULL, 16);

				bpos+=9;
			}
		}

		if ( S_ISDIR(info->si_inode_table[mpos].i_mode) ) {
			unsigned long bpos;
			unsigned long i;
			struct any_dirent *dirent;
			struct any_dirent **pnext;

			file->f_pos = ANY_DATA_OFFSET(info->si_inodes) +
				info->si_inode_table[mpos].i_it_file_offset;

			bufpos = file->f_pos;
			rb = file->f_op->read(file, buffer,
					ANY_BUFFER_SIZE,
					&file->f_pos);
			if ( rb == -1 ) goto free4_fail;
			buffer[rb] = '\0';

			if (strncmp(buffer, 
						ANY_DIR_HEAD, 
						strlen(ANY_DIR_HEAD))!=0)
				goto free4_fail;

			info->si_inode_table[mpos].i_info.dir = 
				any_malloc(sizeof(struct any_dir));
			if (!info->si_inode_table[mpos].i_info.dir) 
			{ ret = -ENOMEM; goto free4_fail; }
			memset(info->si_inode_table[mpos].i_info.dir, 0,
					sizeof(struct any_dir));

			bpos = strlen(ANY_DIR_HEAD);

			info->si_inode_table[mpos].i_info.dir->d_ndirents = 
				simple_strtoul (buffer + bpos,
						NULL, 16);

			bpos+=9;

			pnext = &info->si_inode_table[mpos].i_info.dir->
				d_dirent;

			for (i=0; i < info->si_inode_table[mpos].i_info.
					dir->d_ndirents; i++)
			{
				dirent = any_malloc( sizeof(struct any_dirent));
				if (!dirent) 
				{ ret = -ENOMEM; goto free4_fail; }
				memset(dirent, 0, sizeof(struct any_dirent));

				*pnext = dirent;

				pnext = &dirent->d_next;

				info->si_inode_table[mpos].i_info.dir->
					d_last = dirent;
				dirent->d_pos = i+2;

				if ( (bpos+256) > rb ) {
					file->f_pos -= rb-bpos;

					bufpos = file->f_pos;
					rb = file->f_op->read(file, buffer,
							ANY_BUFFER_SIZE,
							&file->f_pos);
					if ( rb == -1 ) goto free4_fail;
					buffer[rb] = '\0';

					bpos=0;
					if (rb<9) goto free4_fail;
				}

				if ( strlen(buffer+bpos) > ANY_NAME_LENGTH )
				{ ret = -ENAMETOOLONG; goto free4_fail; }

				dirent->d_name = 
					any_malloc(strlen(buffer+bpos)+1);
				if (!dirent->d_name) 
				{ ret = -ENOMEM; goto free4_fail; }

				strcpy ( dirent->d_name,
						buffer+bpos );

				bpos += strlen(buffer+bpos)+1;

				dirent->d_inode =
					simple_strtoul (buffer + bpos,
							NULL, 16);

				bpos+=9;
			}
		}
	}

	set_fs(orig_fs);

	filp_close(file, NULL);

	s->s_fs_info = info;
	s->s_magic = ANY_SUPER_MAGIC;
	s->s_op = &any_sops;

	root_inode = iget(s, 1);

	s->s_root = d_alloc_root(root_inode);
	
	return 0;

free4_fail:
	for (mpos=0; mpos < info->si_inodes; mpos++) {

		if ( S_ISLNK(info->si_inode_table[mpos].i_mode) ) {
			if (!info->si_inode_table[mpos].i_info.symlink) continue;
			any_free( info->si_inode_table[mpos].i_info.symlink );
		}
		
		if ( S_ISREG(info->si_inode_table[mpos].i_mode) ) {
			if (!info->si_inode_table[mpos].i_info.file_frags) continue;
			if (info->si_inode_table[mpos].i_info.
					file_frags->fr_frags)
				any_free( info->si_inode_table[mpos].i_info.
						file_frags->fr_frags );
			any_free( info->si_inode_table[mpos].i_info.file_frags );
		}
		
		if ( S_ISDIR(info->si_inode_table[mpos].i_mode) ) {
			struct any_dirent	*dirent;
			struct any_dirent       *nextdirent;
			
			if (!info->si_inode_table[mpos].i_info.dir) continue;

			dirent = info->si_inode_table[mpos].i_info.
				dir->d_dirent;
			for (; dirent; 
					dirent = nextdirent) {
				if (dirent->d_name)
					any_free( dirent->d_name );
				nextdirent = dirent->d_next;
				any_free( dirent );
			}
				
			any_free( info->si_inode_table[mpos].i_info.dir );
		}
	}
	
free3_fail:
	any_free(info->si_inode_table);
	
free2_fail:
	any_free(info->si_inode_bitmap);

close_fail2:
	set_fs(orig_fs);

	any_free(info->si_itfilename);

close_fail:
	filp_close(file, NULL);
	
free_info:
	any_free(info);    
	s->s_fs_info = NULL;

	return (ret)?ret:-EINVAL;
}

#ifdef KERNEL_2_6_18_PLUS
static int any_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, any_fill_super, mnt);
}
#else
static struct super_block *any_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, any_fill_super);
}
#endif

static struct file_system_type any_fs_type = {
	.owner          = THIS_MODULE,
	.name           = "any",
	.get_sb         = any_get_sb,
	.kill_sb        = kill_block_super,
	.fs_flags       = FS_REQUIRES_DEV,
	//struct list_head fs_supers;
};

static int __init init_any_fs(void)
{
	return register_filesystem(&any_fs_type);
}

static void __exit exit_any_fs(void)
{
	unregister_filesystem(&any_fs_type);
}

module_init(init_any_fs)
module_exit(exit_any_fs)
