/*
 * build_it.c
 * Copyright (C) 2005 Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 *
 * From filefrag from e2fsprogs, Copyright 2003 by Theodore Ts'o.
 * 
 * From geometry.c from  lilo-22.7.2, 
 * 	Copyright 1992-1998 Werner Almesberger.
 * 	Copyright 1999-2005 John Coffman.
 *
 */

#define _LARGEFILE64_SOURCE

#include "any.h"
#include "super.h"

#include <sys/types.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif
#include <sys/ioctl.h>
#include <linux/fd.h>
#include <linux/iso_fs.h>

#include "bitops.h"
#include "progress.h"
#include "version.h"

const char * program_name = "build_it";

struct progress_struct progress;

int verbose = 0;
int quiet = 0;
int unpack = 1;

long fs_type = 0;

const char *path;
const char *inode_table;

static void usage(void)
{
	fprintf(stderr, _("Usage: %s [-qvVn] directory inode_table\n"),
			program_name);
	exit(1);
}

struct hard_link {
	ino_t		hl_ino;
	ino_t		hl_any_ino;
	nlink_t		hl_nlink_founded;
	struct hard_link *hl_next;
};

char *concat_strings(int n, ...)
{
	va_list ap;
	int length=1;
	int i;
	va_start (ap, n);
	for (i=0; i<n; i++) length+=strlen(va_arg(ap,char *));
	va_end (ap);
	char *concat=malloc(sizeof(char)*length);
	if (!concat) return NULL;

	concat[0]='\0';
	va_start (ap, n);
	for (i=0; i<n; i++) strcat(concat,va_arg(ap,char *));
	va_end (ap);
	return concat;
}

void fill_anyinode_by_statinfo(struct any_inode *any_inode, 
		struct stat64 *stat_info) {
	any_inode->i_mode	 = stat_info->st_mode;
	any_inode->i_uid	 = stat_info->st_uid;
	any_inode->i_gid	 = stat_info->st_gid;
	any_inode->i_size	 = stat_info->st_size;
	any_inode->i_atime	 = stat_info->st_atime;
	any_inode->i_ctime	 = stat_info->st_ctime;
	any_inode->i_mtime	 = stat_info->st_mtime;
	any_inode->i_links_count = stat_info->st_nlink;
}

#define FIBMAP     _IO(0x00,1)  /* bmap access */
#define FIGETBSZ   _IO(0x00,2)  /* get the block size used for bmap */

#ifndef REISERFS_SUPER_MAGIC
#define REISERFS_SUPER_MAGIC 0x52654973
#endif

#if 0
#ifndef REISERFS_SUPER_MAGIC_STRING
#define REISERFS_SUPER_MAGIC_STRING "ReIsErFs"
#endif
#endif

#ifndef REISERFS_IOC_UNPACK
#define REISERFS_IOC_UNPACK             _IOW(0xCD,1,long)
#endif

#ifndef REISER4_SUPER_MAGIC
#define REISER4_SUPER_MAGIC  0x52345362
 /* (*(__u32 *)"R4Sb"); */
#endif
#ifndef REISER4_IOC_UNPACK
#define REISER4_IOC_UNPACK              _IOW(0xCD,1,long)
#endif

static unsigned long get_bmap(int fd, unsigned long block, int *err)
{
	int     ret;
	unsigned int b;
	*err = 0;

	b = block;
	ret = ioctl(fd, FIBMAP, &b); /* FIBMAP takes a pointer to an integer */
	if (ret < 0) {
		if (errno == EPERM) {
			fprintf(stderr, _("No permission to use FIBMAP ioctl; must have root privileges\n"));
			exit(1);
		}
		perror("FIBMAP");
		*err = -errno;
	}
	return b;
}

int BS;

int fill_anyinodeinfo(struct any_inode *inode, const char *path, 
		struct stat64 *stat_info) {
	
	int r = 0;
	int err;

	if (S_ISLNK(inode->i_mode)) {
		inode->i_info.symlink = malloc (1025);
		if (!inode->i_info.symlink) {
			return -ENOMEM;
		}

		err = readlink(path, inode->i_info.symlink, 1024);
		if (err<0) {
			free (inode->i_info.symlink);
			return -errno;
		}

		inode->i_info.symlink[err] = '\0';
	} else
	if (S_ISREG(inode->i_mode)) {
		long fd;
		int bs;
		unsigned long   i, block, fr_start=0, fr_length = 0, 
				numfrags = 1, numblocks;

		fd = open(path, O_RDONLY | O_LARGEFILE);
		if (fd < 0) {
			perror("open");
			return -EIO;
		}

		if ( geteuid()!=0 && getuid()==0 )
			setreuid( geteuid(), getuid() );

		if (unpack)
		{
			if (fs_type == REISERFS_SUPER_MAGIC) {
				if ( ( r = ioctl (fd, REISERFS_IOC_UNPACK, 1) ) )
				{
					fprintf (stderr, _("Cannot unpack ReiserFS file\n"));
					close (fd);
					return -abs(r);
				}
			}
			/* Forcing reiser4 to perform tail2extent converstion */
			if (fs_type == REISER4_SUPER_MAGIC) {
				if ( ( r = ioctl (fd, REISER4_IOC_UNPACK, 1) ) )
				{
					fprintf (stderr, _("Cannot unpack Reiser4 file\n"));
					close (fd);
					return -abs(r);
				}

				/* 
				 * As we may have the situation when extent will be included
				 * into transaction, and its item(s) will not be have the real block
				 * numbers assigned, we should perform fsync() in order to guarantee,
				 * that current atom is flushed and real block numbers assigned to 
				 * the extent(s) file was converted in.
				 */
				if ( (r = fdatasync(fd) ) )
				{
					fprintf (stderr, _("Cannot perform fdatasync\n"));
					close (fd);
					return -abs(r);
				}
			}
		}

		if (ioctl(fd, FIGETBSZ, &bs) < 0) { /* FIGETBSZ takes an int */
			perror("FIGETBSZ");
			close(fd);
			return -EIO;
		}

		numblocks = (stat_info->st_size + (bs-1)) / bs;

		BS = bs;

		/* Count number of fragments */
		for (i=0; i < numblocks; i++) {
			block = get_bmap(fd, i, &r);
			if (r<0) {
				close (fd);
				return r;
			}
			
			if (!i) fr_start = block;
			
			if ( (fr_start) ? block != (fr_start+fr_length)
					: block ) {
				fr_start = block;
				fr_length = 0;
				numfrags++;
			}
			fr_length++;
		}

		inode->i_info.file_frags = malloc 
			(sizeof(struct any_file_frags));
		if (!inode->i_info.file_frags) {
			close (fd);
			return -ENOMEM;
		}

		inode->i_info.file_frags->fr_nfrags = numfrags;
		inode->i_info.file_frags->fr_frags =
			malloc(numfrags*sizeof(struct any_file_fragment));
		if (!inode->i_info.file_frags->fr_frags) {
			free (inode->i_info.file_frags);
			close (fd);
			return -ENOMEM;
		}

		fr_length = 0;
		numfrags = 0;
		for (i=0; i < numblocks; i++) {
			block = get_bmap(fd, i, &r);
			if (r<0) {
				close (fd);
				return r;
			}
			if (!i) fr_start = block;
			
			if ( (fr_start) ? block != (fr_start+fr_length)
					: block ) {
				inode->i_info.file_frags->fr_frags[numfrags].
					fr_start = fr_start;
				inode->i_info.file_frags->fr_frags[numfrags].
					fr_length = fr_length;
					
				fr_start = block;
				fr_length = 0;
				numfrags++;
			}
			fr_length++;
		}
		inode->i_info.file_frags->fr_frags[numfrags].
			fr_start = fr_start;
		inode->i_info.file_frags->fr_frags[numfrags].
			fr_length = fr_length;

		if ( geteuid()==0 && getuid()!=0 )
			setreuid( geteuid(), getuid() );

		close(fd);
		
	} else {
		unsigned long major = major(stat_info->st_rdev);
		unsigned long minor = minor(stat_info->st_rdev);

		inode->i_info.device = major<<20 | minor;
	}

	return r;
}

int count_inodes(const char *path, struct hard_link **phl, dev_t st_dev, 
		struct any_sb_info *info, uint32_t dirino, int root,
		int gress)
{
	struct hard_link *hl;
	int err;
	int r = 1;
	struct any_dir *anydir=NULL;
	struct any_dirent **panydirent=NULL;
	DIR *dir;
	struct dirent *dirent;
	char *name;
	struct stat64 stat_info;
	int count_dirs = 0;

	if (info) {
		anydir = malloc (sizeof(struct any_dir));
		if (!anydir) {
			r = -ENOMEM;
			goto out;
		}

		memset (anydir, 0, sizeof(struct any_dir));
		panydirent = &anydir->d_dirent;
	}

	dir = opendir(path);
	if (!dir) {
		r = -EIO;
		goto fail1;
	}

	err = lstat64(path, &stat_info);
	if (err<0) {
		r = -errno;
		goto fail;
	}
	
	if ( stat_info.st_dev != st_dev  ) {
		goto fail;
	}

	while ( ( dirent = readdir(dir) ) ) {
		ino_t any_ino=0;

		progress_update(&progress, gress+r);

		if ( (strcmp(dirent->d_name, ".")==0) ||
		  (strcmp(dirent->d_name, "..")==0) ) continue;
		
		name = concat_strings(3, path, "/", dirent->d_name);
		if (!name) {
			r = -ENOMEM;
			break;
		}
		
		err = lstat64(name, &stat_info);
		if (err<0) {
			r = -errno;
			free(name);
			break;
		}

		if (S_ISDIR(stat_info.st_mode)) {
			if (info) {
				any_ino = find_first_zero_bit(
						info->si_inode_bitmap, 
						info->si_inodes);

				while ( any_ino >= info->si_inodes ) {
					realloc_it(info, info->si_inodes + 10240);

					any_ino = find_first_zero_bit(
							info->si_inode_bitmap, 
							info->si_inodes);
				}

				set_bit(any_ino, info->si_inode_bitmap);

				fill_anyinode_by_statinfo (
						info->si_inode_table+any_ino, 
						&stat_info);
			}
			
			err = count_inodes(name, phl, st_dev, info, any_ino, 0,
					gress+r);
			if (err<0) {
				r = err;
				free(name);
				break;
			}

			count_dirs++;
			r += err;
		}
		else {
			if ( stat_info.st_nlink>1 ) {
				struct hard_link **pnhl;
				
				int w = 0;
				if (*phl) {
					for (pnhl = phl; *pnhl; 
							pnhl = &(*pnhl)->hl_next)
				       	{
						if ( (*pnhl)->hl_ino == stat_info.st_ino)
						{
							(*pnhl)->hl_nlink_founded++;
							any_ino = (*pnhl)->hl_any_ino;
							w = 1;
							if ( (*pnhl)->hl_nlink_founded == stat_info.st_nlink ) {
								hl = (*pnhl);
								(*pnhl) = (*pnhl)->hl_next;
								free (hl);
							}
							break;
						}
					}
				}
				
				if (!w) {
					pnhl = phl;
					while (*pnhl) {
						pnhl = &(*pnhl)->hl_next;
					}
					
					hl = malloc (sizeof (struct hard_link));
					if (!hl) {
						r = -ENOMEM;
						free(name);
						break;
					}
					memset (hl, 0, sizeof (struct hard_link));

					hl->hl_ino = stat_info.st_ino;
					hl->hl_nlink_founded = 1;

					(*pnhl) = hl;

					r++;

					if (info) {
						hl->hl_any_ino = 
							any_ino = find_first_zero_bit(
									info->si_inode_bitmap, 
									info->si_inodes);

						while ( any_ino >= info->si_inodes ) {
							realloc_it(info, info->si_inodes + 10240);

							any_ino = find_first_zero_bit(
									info->si_inode_bitmap, 
									info->si_inodes);
						}

						set_bit(any_ino, info->si_inode_bitmap);

						fill_anyinode_by_statinfo (
								info->si_inode_table+any_ino, 
								&stat_info);

						err = fill_anyinodeinfo( 
								info->si_inode_table+any_ino, 
								name, &stat_info);

						if (err<0) {
							r = err;
							free(name);
							break;
						}
					}
				}
			}
			else {
				r++;
				
				if (info) {
					any_ino = find_first_zero_bit(
							info->si_inode_bitmap, 
							info->si_inodes);
					
					while ( any_ino >= info->si_inodes ) {
						realloc_it(info, info->si_inodes + 10240);

						any_ino = find_first_zero_bit(
								info->si_inode_bitmap, 
								info->si_inodes);
					}

					set_bit(any_ino, info->si_inode_bitmap);

					fill_anyinode_by_statinfo (
							info->si_inode_table+any_ino, 
							&stat_info);

					err = fill_anyinodeinfo( 
							info->si_inode_table+any_ino, 
							name, &stat_info);

					if (err<0) {
						r = err;
						free(name);
						break;
					}
				}
			}
		}

		if (info) {
			(*panydirent) = malloc (sizeof(struct any_dirent));
			if (!(*panydirent)) {
				r = -ENOMEM;
				free(name);
				break;
			}
			memset (*panydirent, 0, sizeof(struct any_dirent));

			(*panydirent)->d_name = 
				malloc (strlen(dirent->d_name)+1);
			if (!(*panydirent)->d_name) {
				r = -ENOMEM;
				free(name);
				break;
			}

			strcpy((*panydirent)->d_name, dirent->d_name);

			(*panydirent)->d_inode = any_ino;

			anydir->d_ndirents++;

			panydirent = &(*panydirent)->d_next;
		}
		
		free(name);
	}

fail:
	closedir(dir);

fail1:
	if (root)
	{
		struct hard_link *hl_next;
		hl = *phl;
		if (hl) hl_next = hl->hl_next;
		for (; hl; hl = hl_next) {
			if (info) {
				info->si_inode_table[hl->hl_any_ino].
					i_links_count =
					hl->hl_nlink_founded;
			}
			hl_next = hl->hl_next;
			free(hl);
		}
		*phl = NULL;
	}
	
	if ( info && (r<=0) ) {
		struct any_dirent *dirent;
		struct any_dirent *nextdirent;
		
		dirent = anydir->d_dirent;
		if (dirent) nextdirent = dirent->d_next;
		
		for (; dirent; dirent=nextdirent) {
			nextdirent = dirent->d_next;
			free(dirent->d_name);
			free(dirent);
		}
		
		free(anydir);
	}

	if ( info && (r>0) )
	{
		info->si_inode_table[dirino].i_info.dir = anydir;
		info->si_inode_table[dirino].i_links_count = count_dirs + 2;
	}
	
out:
	return r;
}

static void PRS(int argc, const char *argv[])
{
	int             c;
	int show_version_only = 0;

	while ((c = getopt (argc, (char**) argv,
		     "qvVn")) != EOF) {
		switch (c) {
		case 'q':
			quiet = 1;
			break;
		case 'v':
			verbose++;
			break;
		case 'V':
			show_version_only++;
			break;
		case 'n':
			unpack = 0;
			break;
		default:
			usage();
		}
	}

	if ( ( optind >= (argc-1) ) && !show_version_only)
		usage();

	if (!quiet || show_version_only)
		fprintf (stderr, "build_it %s (%s)\n", ANYFSTOOLS_VERSION,
				ANYFSTOOLS_DATE);

	if (show_version_only) {
		exit(0);
	}
	
	path = argv[optind++];
	inode_table = argv[optind++];
}

int main(int argc, const char *argv[])
{
	int r;
	int err;
	int c, ac;
	struct any_sb_info *info;
	
	struct stat64 stat_info;
	struct statfs statfs_info;
	
	struct hard_link *hl = NULL;
	dev_t st_dev;

#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
#endif

	PRS(argc, argv);
	
	err = lstat64(path, &stat_info);
	if (err<0) {
		r = -errno;
		perror("lstat");
		goto out;
	}

	st_dev = stat_info.st_dev;
	
	err = statfs(path, &statfs_info);
	if (err<0) {
		r = -errno;
		perror("statfs");
		goto out;
	}

	fs_type = statfs_info.f_type;

	if (!S_ISDIR(stat_info.st_mode)) {
		r = -1;
		fprintf (stderr, _("\"%s\" -- Is not directory\n"),
				path);
		goto out;
	}

#if 0
	if (verbose)
		printf (_("start count inodes\n"));

	if (quiet)
		memset(&progress, 0, sizeof(progress));
	else
		progress_init(&progress, _("count inodes: "), 0);

	c = count_inodes(path, &hl, st_dev, NULL, 1, 1, 0);
	if (c<0) {
		r = c;
		goto out;
	}

	progress_update(&progress, c);
	progress_close(&progress);

	if (verbose)
	{
		printf (_("founded %d inodes\n"), c);
		printf (_("creating inode table\n"));
	}
	
	ac = c+1024;
#else
	if (verbose)
		printf (_("creating inode table\n"));

	c = statfs_info.f_files - statfs_info.f_ffree;
	ac = statfs_info.f_files - statfs_info.f_ffree
		+ statfs_info.f_bfree/1024 + 256;
	if ( !statfs_info.f_files )
		ac = statfs_info.f_blocks/1024 + 256;
#endif

	BS = 512;
	
	r = alloc_it(&info, BS, ac);
	if (r<0) goto out;

	set_bit(1, info->si_inode_bitmap);
	fill_anyinode_by_statinfo (info->si_inode_table+1, &stat_info);
	
	if (quiet)
		memset(&progress, 0, sizeof(progress));
	else
		progress_init(&progress, _("creating inode table: "),
				c);
	
	c = count_inodes(path, &hl, st_dev, info, 1, 1, 0);
	if (c<0) {
		r = c;
		goto free_out;
	}
	
	progress_close(&progress);

	info->si_blocksize = BS;
	
	if (verbose)
		printf (_("writing inode table\n"));
	
	r = write_it (info, (char*)inode_table);	
	if (r)
	{
		fprintf(stderr,
				_("Error while writing inode table: %s\n"),
				(errno)?strerror(errno):_("format error"));
		goto free_out;
	}

free_out:
	free_it (info);	

out:
	return r;
}
	
