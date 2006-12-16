/*
 *  Copyright (C) 2006 Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 *  
 *  From 
 *  FUSE: Filesystem in Userspace
 *  Copyright (C) 2001-2006  Miklos Szeredi <miklos@szeredi.hu>
 *
 *  This program can be distributed under the terms of the GNU GPL.
 *  See the file COPYING.
 */

#include <config.h>

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#define _LARGEFILE64_SOURCE

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>

#include <sys/ioctl.h>

#include "any.h"
#include "bitops.h"
#include "super.h"
#include "new_inode.h"
#include "block_map.h"
#include "version.h"

#define BLKGETSIZE _IO(0x12,96) /* return device size /512 (long *arg) */

mode_t dir_umask = 0002;
mode_t file_umask = 0002;

int device_fd;
const char *device_name;
const char *inode_table;

struct any_sb_info *info;

any_size_t device_blocks = 0;

static int anyfusegetattr(const char *path, struct stat *stbuf)
{
	uint32_t ino;
	int r;

	r = getpathino((char*) path, 1, info, &ino);
	if (r) return -ENOENT;

	stbuf->st_ino 	   = ino;
	stbuf->st_mode     = info->si_inode_table[ino].i_mode;
	stbuf->st_nlink    = info->si_inode_table[ino].i_links_count;
	stbuf->st_uid 	   = info->si_inode_table[ino].i_uid;
	stbuf->st_gid      = info->si_inode_table[ino].i_gid;
	stbuf->st_size     = info->si_inode_table[ino].i_size;
	stbuf->st_atime    = info->si_inode_table[ino].i_atime;
	stbuf->st_ctime    = info->si_inode_table[ino].i_ctime;
	stbuf->st_mtime    = info->si_inode_table[ino].i_mtime;
	stbuf->st_blksize  = info->si_blocksize;
	stbuf->st_blocks   = (stbuf->st_size + info->si_blocksize - 1)/info->si_blocksize;

	return 0;
}

static int anyfuseaccess(const char *path, int mask)
{
    int res;

    res = access(path, mask);
    if (res == -1)
        return -errno;

    return 0;
}

static int anyfusereadlink(const char *path, char *buf, size_t size)
{
	uint32_t ino;
	int r;

	r = getpathino((char*) path, 1, info, &ino);
	if (r) return -ENOENT;

	strncpy(buf, info->si_inode_table[ino].i_info.symlink, size);

	return 0;
}

static int get_name_ino_parino(const char *path, 
		char **pname, uint32_t *pino, uint32_t *pparino)
{
	uint32_t ino, parino;
	int r;

	char *s = strrchr(path, '/');
	if (s == path)
		parino = 1;
	else
	{
		s[0]='\0';

		r = getpathino((char*) path, 1, info, &parino);
		if (r) return -ENOENT;

		s[0]='/';
	}
	s++;

	r = getpathino((char*) s, parino, info, &ino);
	if (pino &&  r) return -ENOENT;
	if (!pino && !r) return -EEXIST;

	if (pname) *pname = s;
	*pparino = parino;
	if (pino) *pino = ino;

	return 0;
}

static int anyfusereaddir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
	uint32_t ino, parino;
	struct any_dirent *dentry;
	int r;

	r = get_name_ino_parino(path, NULL, &ino, &parino);
	if (r) return r;

	{
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = ino;
		st.st_mode = info->si_inode_table[ino].i_mode;

		if (filler(buf, ".", &st, 0))
			return 0;

		memset(&st, 0, sizeof(st));
		st.st_ino = parino;
		st.st_mode = info->si_inode_table[parino].i_mode;

		if (filler(buf, "..", &st, 0))
			return 0;
	}

	dentry = info->si_inode_table[ino].i_info.dir->d_dirent;

	(void) offset;
	(void) fi;

	while (dentry) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = dentry->d_inode;
		st.st_mode = info->si_inode_table[dentry->d_inode].i_mode;
		if (filler(buf, dentry->d_name, &st, 0))
			break;
		dentry = dentry->d_next;
	}

	return 0;
}

static int anyfusemknod(const char *path, mode_t mode, dev_t rdev)
{
	uint32_t parino;
	uint32_t newino;
	struct any_inode *dir_inode;
	struct any_dirent *newdirent;
	int r;
	char *s;

	r = get_name_ino_parino(path, &s, NULL, &parino);
	if (r) return r;

	r = any_new_inode(info, ~file_umask & mode,
			&rdev, parino, &newino);
	if (r) return r;

	dir_inode = info->si_inode_table + parino;
	newdirent = calloc(sizeof(struct any_dirent), 1);
	if (!newdirent)
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}
	newdirent->d_next = dir_inode->i_info.dir->d_dirent;

	dir_inode->i_info.dir->d_ndirents++;
	dir_inode->i_info.dir->d_dirent = newdirent;
	newdirent->d_name = strdup(s);
	newdirent->d_inode = newino;

	return 0;
}

static int anyfusemkdir(const char *path, mode_t mode)
{
	uint32_t parino;
	uint32_t newino;
	struct any_inode *dir_inode;
	struct any_dirent *newdirent;
	int r;
	char *s;

	r = get_name_ino_parino(path, &s, NULL, &parino);
	if (r) return r;

	r = any_new_inode(info, S_IFDIR | (~dir_umask & 0777 & mode),
			NULL, parino, &newino);
	if (r) return r;

	dir_inode = info->si_inode_table + parino;
	newdirent = calloc(sizeof(struct any_dirent), 1);
	if (!newdirent)
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}
	newdirent->d_next = dir_inode->i_info.dir->d_dirent;

	dir_inode->i_info.dir->d_ndirents++;
	dir_inode->i_info.dir->d_dirent = newdirent;
	newdirent->d_name = strdup(s);
	newdirent->d_inode = newino;

	return 0;
}

static int anyfuseunlink(const char *path)
{
	uint32_t ino, parino;
	struct any_dirent **pdentry;
	int r;
	char *s;

	r = get_name_ino_parino(path, &s, &ino, &parino);
	if (r) return r;

	if ( S_ISDIR(info->si_inode_table[ino].i_mode) )
		return -EISDIR;

	pdentry = &info->si_inode_table[parino].i_info.dir->d_dirent;

	if ( --info->si_inode_table[ino].i_links_count <= 0 )
	{
		info->si_inode_table[ino].i_links_count = 0;

		if ( S_ISLNK(info->si_inode_table[ino].i_mode) )
			free( info->si_inode_table[ino].i_info.symlink );

		if ( S_ISREG(info->si_inode_table[ino].i_mode) )
		{
			if (!info->si_inode_table[ino].i_it_file_offset)
			{
				free( info->si_inode_table[ino].i_info.file_frags->fr_frags );
				free( info->si_inode_table[ino].i_info.file_frags );
				clear_bit(ino, info->si_inode_bitmap);
			}
			else
				info->si_inode_table[ino].i_links_count = -1;
		}
	}

	while (*pdentry) {
		if (strcmp( (*pdentry)->d_name, s ) == 0)
		{
			*pdentry = (*pdentry)->d_next;
			break;
		}
		pdentry = &(*pdentry)->d_next;
	}

	info->si_inode_table[parino].i_info.dir->d_ndirents--;

	return 0;
}

static int anyfusermdir(const char *path)
{
	uint32_t ino, parino;
	struct any_dirent **pdentry;
	int r;
	char *s;

	r = get_name_ino_parino(path, &s, &ino, &parino);
	if (r) return r;

	if ( !S_ISDIR(info->si_inode_table[ino].i_mode) )
		return -ENOTDIR;

	if( info->si_inode_table[ino].i_info.dir->d_ndirents )
		return -ENOTEMPTY;

	pdentry = &info->si_inode_table[parino].i_info.dir->d_dirent;

	info->si_inode_table[ino].i_links_count = 0;
	free( info->si_inode_table[ino].i_info.dir );
	clear_bit(ino, info->si_inode_bitmap);

	while (*pdentry) {
		if (strcmp( (*pdentry)->d_name, s ) == 0)
		{
			*pdentry = (*pdentry)->d_next;
			break;
		}
		pdentry = &(*pdentry)->d_next;
	}

	info->si_inode_table[parino].i_info.dir->d_ndirents--;

	return 0;
}

static int anyfusesymlink(const char *from, const char *to)
{
	const char *path = to;
	uint32_t parino;
	uint32_t newino;
	struct any_inode *dir_inode;
	struct any_dirent *newdirent;
	int r;
	char *s;

	r = get_name_ino_parino(path, &s, NULL, &parino);
	if (r) return r;

	r = any_new_inode(info, S_IFLNK | 0777,
			(char*) from, parino, &newino);
	if (r) return r;

	dir_inode = info->si_inode_table + parino;
	newdirent = calloc(sizeof(struct any_dirent), 1);
	if (!newdirent)
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}
	newdirent->d_next = dir_inode->i_info.dir->d_dirent;

	dir_inode->i_info.dir->d_ndirents++;
	dir_inode->i_info.dir->d_dirent = newdirent;
	newdirent->d_name = strdup(s);
	newdirent->d_inode = newino;

	return 0;
}

static int anyfuserename(const char *from, const char *to)
{
    int res;

    res = rename(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int anyfuselink(const char *from, const char *to)
{
	const char *path = to;
	uint32_t fromino;
	uint32_t parino;
	struct any_inode *dir_inode;
	struct any_dirent *newdirent;
	int r;
	char *s;

	r = get_name_ino_parino(path, &s, NULL, &parino);
	if (r) return r;

	r = getpathino((char*) from, 1, info, &fromino);
	if (r) return -ENOENT;

	if ( S_ISDIR(info->si_inode_table[fromino].i_mode) )
		return -EPERM;

	dir_inode = info->si_inode_table + parino;
	newdirent = calloc(sizeof(struct any_dirent), 1);
	if (!newdirent)
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}
	newdirent->d_next = dir_inode->i_info.dir->d_dirent;

	dir_inode->i_info.dir->d_ndirents++;
	dir_inode->i_info.dir->d_dirent = newdirent;
	newdirent->d_name = strdup(s);
	newdirent->d_inode = fromino;

	info->si_inode_table[fromino].i_links_count++;

	return 0;
}

static int anyfusechmod(const char *path, mode_t mode)
{
	uint32_t ino;
	int r;

	r = getpathino((char*) path, 1, info, &ino);
	if (r) return -ENOENT;

	info->si_inode_table[ino].i_mode &= ~0777;
	info->si_inode_table[ino].i_mode |= mode & 0777;

	return 0;
}

static int anyfusechown(const char *path, uid_t uid, gid_t gid)
{
	uint32_t ino;
	int r;

	r = getpathino((char*) path, 1, info, &ino);
	if (r) return -ENOENT;

	info->si_inode_table[ino].i_uid = uid;
	info->si_inode_table[ino].i_gid = gid;

	return 0;
}

static int anyfusetruncate(const char *path, off_t size)
{
    return -EOPNOTSUPP;
}

static int anyfuseutime(const char *path, struct utimbuf *buf)
{
	uint32_t ino;
	int r;

	r = getpathino((char*) path, 1, info, &ino);
	if (r) return -ENOENT;

	info->si_inode_table[ino].i_atime = buf->actime;
	info->si_inode_table[ino].i_mtime = buf->modtime;

	return 0;
}

static int anyfuseopen(const char *path, struct fuse_file_info *fi)
{
	uint32_t ino;
	int r;

	r = getpathino((char*) path, 1, info, &ino);
	if (r) return -ENOENT;

	if ( S_ISDIR(info->si_inode_table[ino].i_mode) )
		return -EISDIR;

	if ( (fi->flags & O_RDWR) ||
			(fi->flags & O_WRONLY) )
		return -EPERM;

	fi->fh = ino;

	info->si_inode_table[ino].i_it_file_offset++;

	return 0;
}

static int anyfuserelease(const char *path, struct fuse_file_info *fi)
{
	uint32_t ino = (uint32_t) fi->fh;

	info->si_inode_table[ino].i_it_file_offset--;

	if ( !info->si_inode_table[ino].i_it_file_offset )
	{
		if ( info->si_inode_table[ino].i_links_count == ( (uint16_t) -1 ) )
		{
			free( info->si_inode_table[ino].i_info.file_frags->fr_frags );
			free( info->si_inode_table[ino].i_info.file_frags );
			info->si_inode_table[ino].i_links_count = 0;
			clear_bit(ino, info->si_inode_bitmap);
		}
	}

	return 0;
}

static int anyfusebmap(uint32_t ino, uint32_t bl, uint32_t *phys)
{
	int i;

	unsigned long block = 0;
	unsigned long fr_length;
	unsigned long fr_start;

	for (i=0; i<info->si_inode_table[ino].
			i_info.file_frags->fr_nfrags; i++)
	{
		fr_length = info->si_inode_table[ino].
			i_info.file_frags->fr_frags[i].fr_length;
		if ( (block + fr_length)>bl ) {
			fr_start = info->si_inode_table[ino].
				i_info.file_frags->fr_frags[i].fr_start;

			*phys = (fr_start) ? fr_start + bl - block : 0;
			return 0;
		}

		block += fr_length;
	}

	return -1;
}

static int anyfuseread(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
	off_t off;
	size_t sz;
	uint32_t bl;
	uint32_t phys;

	uint32_t ino = (uint32_t) fi->fh;

	off_t endoff = offset + size;
	uint32_t startblock = offset / info->si_blocksize;
	uint32_t endblock = endoff / info->si_blocksize;

	uint32_t offinstart = offset - startblock * info->si_blocksize;
	uint32_t offinend   = endoff - endblock * info->si_blocksize;

	int r, res = 0;

	for ( bl = startblock; bl <= endblock; bl++ )
	{
		sz = info->si_blocksize;
		if ( bl == startblock ) sz -= offinstart;
		if ( bl == endblock ) sz -= info->si_blocksize - offinend;

		if (!sz) break;

		r = anyfusebmap(ino, bl, &phys);
		if (r) return res;

		if (!phys)
			memset(buf, 0, sz);
		else
		{
			off = (off_t) phys * info->si_blocksize + offinstart;
			if ( bl == startblock ) off += offinstart;

			r = pread(device_fd, buf, sz, off);
			if (r == -1) return -errno;

			res+=r;
			if (r < sz) return res;

			buf+=r;
		}
	}

	return res;
}

static int anyfusewrite(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    return -EOPNOTSUPP;
}

static int anyfusestatfs(const char *path, struct statvfs *stbuf)
{
	uint32_t ino;
	uint32_t i;
	int r;

	r = getpathino((char*) path, 1, info, &ino);
	if (r) return -ENOENT;

	stbuf->f_bsize  = info->si_blocksize;
	stbuf->f_frsize = info->si_blocksize;
	stbuf->f_blocks = device_blocks;
	stbuf->f_bfree  = stbuf->f_bavail = 0;
	stbuf->f_files  = info->si_inodes;

	uint32_t ffree = 0;
	for (i=0; i<info->si_inodes; i++)
	{
		if ( !test_bit(i, info->si_inode_bitmap) )
			ffree++;
	}

	stbuf->f_ffree  = ffree;
	stbuf->f_favail = stbuf->f_ffree;
	//FIXME: define stbuf->f_fsid
	//FIXME: define stbuf->f_flag
	stbuf->f_namemax = ANY_NAME_LENGTH;

	return 0;
}

static void anyfusedestroy(void *s)
{
	write_it (info, (void*)inode_table);
	free_it (info);
}

static struct fuse_operations anyfuseoper = {
    .getattr	= anyfusegetattr,
    .access	= anyfuseaccess,
    .readlink	= anyfusereadlink,
    .readdir	= anyfusereaddir,
    .mknod	= anyfusemknod,
    .mkdir	= anyfusemkdir,
    .symlink	= anyfusesymlink,
    .unlink	= anyfuseunlink,
    .rmdir	= anyfusermdir,
    .rename	= anyfuserename,
    .link	= anyfuselink,
    .chmod	= anyfusechmod,
    .chown	= anyfusechown,
    .truncate	= anyfusetruncate,
    .utime	= anyfuseutime,
    .open	= anyfuseopen,
    .release    = anyfuserelease,
    .read	= anyfuseread,
    .write	= anyfusewrite,
    .statfs	= anyfusestatfs,
    .destroy	= anyfusedestroy,
};

void usage(char *appName)
{
	char **argv = (char **)malloc(2*sizeof(char *));
	argv[0] = appName;
	argv[1] = (char *)malloc(3*sizeof(char));
	memcpy(argv[1], "-h\0", 3);
	fuse_main(2, argv, &anyfuseoper);

	printf("\n"
			"-------------------------------------------------------\n"
			"anyfuse %s(%s)\n"
			"\n"
			"Usage: %s <inode_table> <device> <mount_point> <FUSE OPTIONS>\n"
			"-------------------------------------------------------\n",
			ANYFSTOOLS_VERSION, ANYFSTOOLS_DATE, appName);
}


int main(int argc, char *argv[])
{
	uint32_t i;
	int r;

	umask(0);
	if((argc > 1)&&(strcmp(argv[1], "-h") == 0))
	{
		usage(argv[0]);
		return 0;
	}

	if(argc < 4)
	{
		usage(argv[0]);
		return -1;
	}

	inode_table = argv[optind++];
	device_name = argv[optind++];

	device_fd = open(device_name, O_RDONLY | O_LARGEFILE);
	if (device_fd==-1) {
		fprintf(stderr, _("Error opening file\n"));
		exit(1);
	}       

	struct stat64 st;
	fstat64(device_fd, &st);
	if (st.st_mode & S_IFBLK)
	{
		if (ioctl(device_fd, BLKGETSIZE, &st.st_size) < 0) {
			perror("BLKGETSIZE");
			close(device_fd);
			return -EIO;
		}
		st.st_size*=512;
	}

	r = read_it (&info, (void*)inode_table);
	if (r)
	{
		fprintf(stderr,
				_("Error while reading inode table: %s\n"),
				(errno)?strerror(errno):_("bad format"));
		exit (1);
	}

	for (i=0; i<info->si_inodes; i++)
		info->si_inode_table[i].i_it_file_offset = 0;

	any_size_t blocks = (st.st_size + info->si_blocksize - 1)/info->si_blocksize;
	device_blocks = blocks;

	if (!blocks)
	{
		fprintf(stderr, _("It is not funny at all! I can do nothing with empty file.\n"));
		exit(1);
	}

	return fuse_main(argc-2, argv+2, &anyfuseoper);
}
