/*
 *  new_inode.c
 *  Copyright (C) 2005-2006 Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "new_inode.h"
#include "bitops.h"
#include "super.h"

extern mode_t dir_umask;

int any_new_inode(struct any_sb_info *info, int mode, void* data,
		uint32_t dirino, uint32_t *newino)
{
	struct any_inode *inode;
	struct any_inode *dirinode;
	unsigned long ino;
	dev_t rdev = 0;

	ino = find_first_zero_bit(info->si_inode_bitmap, info->si_inodes);
	while (ino >= info->si_inodes) {
		realloc_it(info, info->si_inodes + 10240);
		ino = find_first_zero_bit(info->si_inode_bitmap, info->si_inodes);
	}

	*newino = ino;

	dirinode = info->si_inode_table + dirino;
	inode = info->si_inode_table + ino;
	
	set_bit(ino, info->si_inode_bitmap);
	
	inode->i_uid = getuid();
	inode->i_gid = (dirinode->i_mode & S_ISGID) ? dirinode->i_gid : getgid();
	inode->i_size = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = time(NULL);
	inode->i_mode = mode;
	inode->i_links_count = 1;
	
	if ( S_ISLNK(inode->i_mode) )
	{
		info->si_inode_table[ino].i_info.symlink =
			malloc(strlen(data)+1);
		if (!info->si_inode_table[ino].i_info.symlink)
		{
			fprintf(stderr, _("Not enough memory\n"));
			exit(1);
		}

		strcpy ( info->si_inode_table[ino].i_info.symlink,
				data );
	}

	if ( S_ISDIR(inode->i_mode) )
	{
		inode->i_links_count++;
		dirinode->i_links_count++;
		info->si_inode_table[ino].i_info.dir =
			calloc(sizeof(struct any_dir), 1);
		if (!info->si_inode_table[ino].i_info.dir)
		{
			fprintf(stderr, _("Not enough memory\n"));
			exit(1);
		}
	}

	if ( ! ( S_ISLNK(inode->i_mode) || 
				S_ISREG(inode->i_mode) || 
				S_ISDIR(inode->i_mode) ) )
	{
		info->si_inode_table[ino].i_info.device = 
			rdev = *((dev_t*)data);
	}

	return 0;
}

int getpathino(char *path, uint32_t root, struct any_sb_info *info,
		uint32_t *ino)
{
	struct any_inode *root_inode;
	struct any_dirent *dirent;
	char *slash;
	
	if ( !test_bit(root, info->si_inode_bitmap) ) return -1;
		
        root_inode = info->si_inode_table + root;
	
	if ( !S_ISDIR(root_inode->i_mode) ) return -1;

	while ( (*path)=='/' ) path++;

	slash = strchr(path, '/');
	if (slash) slash[0] = '\0';
	
	dirent = root_inode->i_info.dir->d_dirent;

	for (; dirent; dirent=dirent->d_next)
	{
		if (strcmp((void*)dirent, path)==0)
		{
			if (slash)
			{
				slash[0] = '/';
				return getpathino(slash,
						dirent->d_inode, info, ino);
			}
			else
				*ino = dirent->d_inode;
				return 0;
		}
	}

	if (slash) slash[0] = '/';
	return 1;
}

int mkpathino(char *path, uint32_t root, struct any_sb_info *info,
		uint32_t *ino)
{
	int res;
	struct any_inode *root_inode;
	struct any_dirent **pdirent;
	struct any_dirent *newdirent;
	uint32_t newino;
	char *slash;
	
	if ( !test_bit(root, info->si_inode_bitmap) ) return -1;
		
        root_inode = info->si_inode_table + root;
	
	if ( !S_ISDIR(root_inode->i_mode) ) return -1;

	while ( (*path)=='/' ) path++;

	slash = strchr(path, '/');
	if (slash) slash[0] = '\0';
	
	pdirent = &root_inode->i_info.dir->d_dirent;

	for (; *pdirent; pdirent=&(*pdirent)->d_next)
	{
		if (strcmp((*pdirent)->d_name, path)==0)
			goto created;
	}
	
	res = any_new_inode(info, S_IFDIR | (~dir_umask & 0777), 
			NULL, root, &newino);
	if (res) return res;

	/*
	 * We need to update the inode link, so as it maybe
	 * changed with realloc in any_new_inode
	 */
        root_inode = info->si_inode_table + root;

	newdirent = calloc(sizeof(struct any_dirent), 1);
	if (!newdirent)
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}

	root_inode->i_info.dir->d_ndirents++;
	(*pdirent) = newdirent;
	(*pdirent)->d_name = strdup(path);
	(*pdirent)->d_inode = newino;

created:
	if (slash)
	{
		slash[0] = '/';
		return mkpathino(slash, 
				(*pdirent)->d_inode, 
				info, ino);
	}
	else 
		if (ino) *ino = (*pdirent)->d_inode;

	return 0;
}
