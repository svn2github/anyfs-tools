/*
 *	build_it/super.h
 *	Copyright (C) 2005-2006 
 *		Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 *
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "any.h"
#include "bitops.h"

int write_it(struct any_sb_info *info, char itfilename[])
{       
	int file = -1;
	char buffer[ANY_BUFFER_SIZE+1];
	ssize_t len;
	ssize_t rb;
	unsigned long mpos;

	/*open inode table file for write only*/
	file = open((itfilename)?itfilename:info->si_itfilename, 
			O_WRONLY | O_CREAT | O_TRUNC, 
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (file==-1)
		goto out;

	lseek(file, ANY_DATA_HEAD_OFFSET(info->si_inodes), SEEK_SET);
	
	len = strlen(ANY_DATA_HEAD);
	rb = write(file, ANY_DATA_HEAD, len);
	if ( rb < len ) goto close_fail;

	for (mpos=0; mpos < info->si_inodes; mpos++) {
		if ( !info->si_inode_table[mpos].i_links_count ) continue;
		
		info->si_inode_table[mpos].i_it_file_offset = 
			lseek(file, 0, SEEK_CUR) - ANY_DATA_OFFSET(info->si_inodes);

		if ( S_ISLNK(info->si_inode_table[mpos].i_mode) ) {
			len = strlen(ANY_LNK_HEAD);
			rb = write(file, ANY_LNK_HEAD, len);
			if ( rb < len ) goto close_fail;

			len = strlen(info->si_inode_table[mpos].
					i_info.symlink) + 1;
			rb = write(file, 
					info->si_inode_table[mpos].
					i_info.symlink, len);
			if ( rb < len ) goto close_fail;

			len = 1;
			rb = write(file, "\n", len);
			if ( rb < len ) goto close_fail;
		}

		if ( S_ISREG(info->si_inode_table[mpos].i_mode) ) {
			unsigned long i;

			len = snprintf (buffer, ANY_BUFFER_SIZE, "%s%08x\n",
					ANY_REG_HEAD,
					info->si_inode_table[mpos].i_info.
					file_frags->fr_nfrags );
			rb = write(file, buffer, len);
			if ( rb < len ) goto close_fail;

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
				rb = write(file, buffer, len);
				if ( rb < len ) goto close_fail;
			}

		}

		if ( S_ISDIR(info->si_inode_table[mpos].i_mode) ) {
			struct any_dirent	*dirent;
			
			len = snprintf (buffer, ANY_BUFFER_SIZE, "%s%08x\n",
					ANY_DIR_HEAD,
					info->si_inode_table[mpos].i_info.
					dir->d_ndirents );
			rb = write(file, buffer, len);
			if ( rb < len ) goto close_fail;

			dirent = info->si_inode_table[mpos].i_info.
				dir->d_dirent;
			for (; dirent; dirent = dirent->d_next) {
				len = strlen(dirent->d_name) + 1;
				rb = write(file, dirent->d_name, len);
				if ( rb < len ) goto close_fail;
				
				len = snprintf (buffer, ANY_BUFFER_SIZE, 
						"%08x\n",
						dirent->d_inode );
				rb = write(file, buffer, len);
				if ( rb < len ) goto close_fail;
			}
		}
	}

	lseek(file, 0, SEEK_SET);
	
	len = snprintf (buffer, ANY_BUFFER_SIZE, "%s%08lx\n",
			ANY_BLOCK_SIZE_HEAD, info->si_blocksize);
	rb = write(file, buffer, len);
	if ( rb < len ) goto close_fail;
	
	len = snprintf (buffer, ANY_BUFFER_SIZE, "%s%08lx\n",
			ANY_INODES_HEAD, info->si_inodes);
	rb = write(file, buffer, len);
	if ( rb < len ) goto close_fail;
	
	len = strlen(ANY_INODE_TABLE_HEAD);
	rb = write(file, ANY_INODE_TABLE_HEAD, len);
	if ( rb < len ) goto close_fail;
	
	for (mpos=0; mpos < info->si_inodes; mpos++) {
		uint16_t i_mode;
		i_mode = info->si_inode_table[mpos].i_mode;
		len = snprintf (buffer, ANY_BUFFER_SIZE, 
				"%04x %04x %04x %016" LL "x %08x %08x %08x %04x %08lx\n",
				info->si_inode_table[mpos].i_mode,
				info->si_inode_table[mpos].i_uid,
				info->si_inode_table[mpos].i_gid,
				info->si_inode_table[mpos].i_size,
				info->si_inode_table[mpos].i_atime,
				info->si_inode_table[mpos].i_ctime,
				info->si_inode_table[mpos].i_mtime,
				info->si_inode_table[mpos].i_links_count,
				(unsigned long)( (S_ISLNK(i_mode) ||
				 S_ISREG(i_mode) ||
				 S_ISDIR(i_mode))?
				info->si_inode_table[mpos].i_it_file_offset:
				info->si_inode_table[mpos].i_info.device) );
		rb = write(file, buffer, len);
		if ( rb < len ) goto close_fail;
	}
	
	close(file);
	return 0;
	
close_fail:
	close(file);
out:
	return 1;
}

void free_it(struct any_sb_info * info)
{
	unsigned long mpos;

	for (mpos=0; mpos < info->si_inodes; mpos++) {
		if ( !info->si_inode_table[mpos].i_links_count ) continue;

		if ( S_ISLNK(info->si_inode_table[mpos].i_mode) ) {
			if (!info->si_inode_table[mpos].i_info.symlink) continue;
			free( info->si_inode_table[mpos].i_info.symlink );
		}

		if ( S_ISREG(info->si_inode_table[mpos].i_mode) ) {
			if (!info->si_inode_table[mpos].i_info.file_frags) continue;
			if (info->si_inode_table[mpos].i_info.
					file_frags->fr_frags)
				free( info->si_inode_table[mpos].i_info.
						file_frags->fr_frags );
			free( info->si_inode_table[mpos].i_info.file_frags );
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
					free( dirent->d_name );
				nextdirent = dirent->d_next;
				free( dirent );
			}

			free( info->si_inode_table[mpos].i_info.dir );
		}
	}

	free(info->si_inode_table);

	free(info->si_inode_bitmap);
	
	free(info->si_itfilename);

	free(info);    

	return;
}

int realloc_it(struct any_sb_info *info, unsigned long inodes)
{       
	int i;
	int ret = 0;
	unsigned long bitmap_l, old_bitmap_l;

	bitmap_l = ( inodes +
			sizeof(unsigned long) - 1 )/sizeof(unsigned long);
	old_bitmap_l = ( info->si_inodes +
			sizeof(unsigned long) - 1 )/sizeof(unsigned long);
	
	/*reallocate memory for inode bitmap*/
	info->si_inode_bitmap =  (typeof(info->si_inode_bitmap))
		realloc(info->si_inode_bitmap, bitmap_l*sizeof(unsigned long));
	if (!info->si_inode_bitmap) 
	{ ret = -ENOMEM; goto out1; }

	memset(info->si_inode_bitmap + old_bitmap_l, 
			0, (bitmap_l-old_bitmap_l)*sizeof(unsigned long));

	for ( i = info->si_inodes; i < old_bitmap_l*sizeof(unsigned long); i++ )
		clear_bit(i, info->si_inode_bitmap);

	if ( inodes*sizeof(struct any_inode)/
			sizeof(struct any_inode) < inodes )
	{
		fprintf (stderr, "Oops! %d-bit Overflow. I want too much memory\n",
				sizeof(inodes)*8);
		exit(1);
	}
	
	info->si_inode_table = (typeof(info->si_inode_table))
		realloc(info->si_inode_table, inodes*sizeof(struct any_inode));
	if (!info->si_inode_table) 
	{ ret = -ENOMEM; goto out2; }
	memset(info->si_inode_table + info->si_inodes, 0, (inodes - info->si_inodes)*sizeof(struct any_inode) );

	info->si_inodes = inodes;

	return ret;

out2:
out1:
	exit(-ret);
	return ret;
}

int alloc_it(struct any_sb_info ** it, unsigned long blocksize, 
		unsigned long inodes)
{       
	int ret = 0;
	struct any_sb_info *info;
	unsigned long bitmap_l;

	info = (typeof(info)) malloc(sizeof(*info));
	if (!info)
		return -ENOMEM;
	memset(info, 0, sizeof(*info));

	info->si_blocksize = blocksize;
	info->si_inodes = inodes;

	bitmap_l = ( info->si_inodes +
			sizeof(unsigned long) - 1 )/sizeof(unsigned long);
	
	/*allocate memory for inode bitmap*/
	info->si_inode_bitmap =  (typeof(info->si_inode_bitmap))
		malloc(bitmap_l*sizeof(unsigned long));
	if (!info->si_inode_bitmap) 
	{ ret = -ENOMEM; goto out1; }
	memset(info->si_inode_bitmap, 0, bitmap_l*sizeof(unsigned long));

	set_bit(0, info->si_inode_bitmap);

	if ( info->si_inodes*sizeof(struct any_inode)/
			sizeof(struct any_inode) < info->si_inodes )
	{
		fprintf (stderr, "Oops! %d-bit Overflow. I want too much memory\n",
				sizeof(info->si_inodes)*8);
		exit(1);
	}

	info->si_inode_table = (typeof(info->si_inode_table))
		malloc(info->si_inodes*sizeof(struct any_inode));
	if (!info->si_inode_table) 
	{ ret = -ENOMEM; goto out2; }
	memset(info->si_inode_table, 0, info->si_inodes*sizeof(struct any_inode));
	
	*it = info;
	
	return ret;

//out3:
	free(info->si_inode_table);
out2:
	free(info->si_inode_bitmap);
out1:
	free(info);
	return ret;
}

char *concat_strings(int n, ...)
{
	va_list ap;
	int length=1;
	int i;
	va_start (ap, n);
	for (i=0; i<n; i++) length+=strlen(va_arg(ap,char *));
	va_end (ap);
	char *concat = (char*) malloc(sizeof(char)*length);
	if (!concat) return NULL;

	concat[0]='\0';
	va_start (ap, n);
	for (i=0; i<n; i++) strcat(concat,va_arg(ap,char *));
	va_end (ap);
	return concat;
}


int read_it(struct any_sb_info ** it, char itfilename[])
{       
	struct any_sb_info *info;
	int file = -1;
	char buffer[ANY_BUFFER_SIZE+1];
	any_ssize_t rb;
	unsigned long bitmap_l;
	any_ssize_t leavbyte;
	unsigned long mpos;
	int ret=0;
	
	info = (typeof(info)) malloc(sizeof(*info));
	if (!info)
		return -ENOMEM;
	memset(info, 0, sizeof(*info));

	if (itfilename[0]=='/')
	{
		info->si_itfilename = (typeof(info->si_itfilename))
			malloc(strlen(itfilename)+1);
		if (!info->si_itfilename) {
			ret = -ENOMEM;
			goto info_fail;
		}
		
		strcpy (info->si_itfilename, itfilename);
	}
	else
	{
		char wd[1024];
		getcwd(wd, 1024);
		info->si_itfilename =
			concat_strings(3, wd, "/", itfilename);
	}


	/*open inode table file for read only*/
	file = open(itfilename, O_RDONLY | O_BINARY, 0);
	if (file==-1)
		goto free_fail;

	lseek(file, 0, SEEK_SET);

	/*
	 *	read block size, number of inodes and
	 *	inode table head
	 */
	rb = read(file, buffer,
			ANY_BLOCK_SIZE_LEN + ANY_INODES_LEN
			+ strlen(ANY_INODE_TABLE_HEAD));
	if ( rb == -1 ) goto close_fail;

	buffer[rb] = '\0';

	/*check block size, number of inodes and inode bitmap heads*/
	if (strncmp(buffer + ANY_BLOCK_SIZE_HEAD_OFFSET, 
				ANY_BLOCK_SIZE_HEAD, 
				strlen(ANY_BLOCK_SIZE_HEAD))!=0)
		goto close_fail;
	if (strncmp(buffer + ANY_INODES_HEAD_OFFSET, 
				ANY_INODES_HEAD, 
				strlen(ANY_INODES_HEAD))!=0)
		goto close_fail;
	if (strncmp(buffer + ANY_INODE_TABLE_HEAD_OFFSET, 
				ANY_INODE_TABLE_HEAD, 
				strlen(ANY_INODE_TABLE_HEAD))!=0)
	{
		printf (_("bad inode table head: %s\n"), buffer);
		goto close_fail;
	}

	info->si_blocksize = strtoul (buffer + ANY_BLOCK_SIZE_OFFSET,
			NULL, 16);
	info->si_inodes = strtoul (buffer + ANY_INODES_OFFSET,
			NULL, 16);

	bitmap_l = ( info->si_inodes + 
			sizeof(unsigned long) - 1 )/sizeof(unsigned long);
	
	/*allocate memory for inode bitmap*/
	info->si_inode_bitmap =  (typeof(info->si_inode_bitmap))
		malloc(bitmap_l*sizeof(unsigned long));
	if (!info->si_inode_bitmap) 
	{ ret = -ENOMEM; goto close_fail; }
	memset(info->si_inode_bitmap, 0, bitmap_l*sizeof(unsigned long));

	set_bit(0, info->si_inode_bitmap);
	
	/*read inode table*/
	info->si_inode_table = (typeof(info->si_inode_table))
		malloc(info->si_inodes*sizeof(struct any_inode));
	if (!info->si_inode_table) 
	{ ret = -ENOMEM; goto free2_fail; }
	memset(info->si_inode_table, 0, info->si_inodes*sizeof(struct any_inode));

	leavbyte = ANY_INODE_TABLE_INFOLEN(info->si_inodes);
	
#ifdef DEBUG	
	printf ("inodes=%d, inode_table_len=%d\n",
			info->si_inodes, leavbyte);
#endif

	mpos = 0;
	
	while (leavbyte) {
		unsigned long bpos;
	
		rb = read(file, buffer,
				min_t(size_t, leavbyte, ANY_BUFFER_SIZE));
		if ( rb == -1 )
		{
			printf (_("fail read inode table\n"));
			goto free3_fail;
		}
		buffer[rb] = '\0';

		leavbyte -= rb;

		if (rb<73) goto free3_fail;

		for (bpos = 0; (bpos+73)<=rb; bpos+=73)
		{
			uint16_t i_mode;
			
			i_mode = info->si_inode_table[mpos].i_mode =
				strtoul (buffer + bpos + 0,
						NULL, 16);
			
			info->si_inode_table[mpos].i_uid =
				strtoul (buffer + bpos + 5,
						NULL, 16);
			
			info->si_inode_table[mpos].i_gid =
				strtoul (buffer + bpos + 10,
						NULL, 16);
			
			info->si_inode_table[mpos].i_size =
				strtoull (buffer + bpos + 15,
						NULL, 16);
			
			info->si_inode_table[mpos].i_atime =
				strtoul (buffer + bpos + 32,
						NULL, 16);
			
			info->si_inode_table[mpos].i_ctime =
				strtoul (buffer + bpos + 41,
						NULL, 16);
			
			info->si_inode_table[mpos].i_mtime =
				strtoul (buffer + bpos + 50,
						NULL, 16);
			
			info->si_inode_table[mpos].i_links_count =
				strtoul (buffer + bpos + 59,
						NULL, 16);
			
			if ( S_ISLNK(i_mode) ||
					S_ISREG(i_mode) ||
					S_ISDIR(i_mode) ) {
				info->si_inode_table[mpos].i_it_file_offset =
					strtoul (buffer + bpos + 64,
							NULL, 16);
			}
			else {
				info->si_inode_table[mpos].i_info.
					device =
					strtoul (buffer + bpos + 64,
							NULL, 16);
			}
			
			mpos++;
		}
		leavbyte += rb-bpos;
		
		lseek(file, 
				-(rb-bpos), 
				SEEK_CUR);
	}

#ifdef DEBUG	
	printf ("mpos=%d, leavbyte=%d\n",
			mpos, leavbyte);
#endif
	
	/*read data head*/
	rb = read(file, buffer,
			strlen(ANY_DATA_HEAD));
	if ( rb == -1 ) goto free3_fail;
	
	if (strncmp(buffer, 
				ANY_DATA_HEAD, 
				strlen(ANY_DATA_HEAD))!=0)
	{
		printf (_("bad data head: %s\n"), buffer);
		goto free3_fail;
	}
	
	/*read data*/
	for (mpos=1; mpos<info->si_inodes; mpos++) {
		if (!info->si_inode_table[mpos].i_links_count) continue;
		set_bit(mpos, info->si_inode_bitmap);
		
		if ( S_ISLNK(info->si_inode_table[mpos].i_mode) ) {
			unsigned long bpos;

			lseek(file, 
					ANY_DATA_OFFSET(info->si_inodes) +
					info->si_inode_table[mpos].i_it_file_offset, 
					SEEK_SET);

			rb = read(file, buffer,
					ANY_BUFFER_SIZE);
			if ( rb == -1 ) goto free4_fail;
			buffer[rb] = '\0';

			if (strncmp(buffer, 
						ANY_LNK_HEAD, 
						strlen(ANY_LNK_HEAD))!=0)
				goto free4_fail;

			bpos = strlen(buffer+strlen(ANY_LNK_HEAD)) + 1;

			info->si_inode_table[mpos].i_info.symlink = 
				(typeof(info->si_inode_table[mpos].i_info.symlink))
				malloc(bpos);
			if (!info->si_inode_table[mpos].i_info.symlink) 
			{ ret = -ENOMEM; goto free4_fail; }
			memset(info->si_inode_table[mpos].i_info.symlink,
				 0,	bpos);

			strcpy ( info->si_inode_table[mpos].i_info.symlink,
					buffer+strlen(ANY_LNK_HEAD) );
		}

		if ( S_ISREG(info->si_inode_table[mpos].i_mode) ) {
			unsigned long bpos;
			unsigned long i;

			lseek(file, 
					ANY_DATA_OFFSET(info->si_inodes) +
					info->si_inode_table[mpos].i_it_file_offset, 
					SEEK_SET);

			rb = read(file, buffer,
					ANY_BUFFER_SIZE);
			if ( rb == -1 )
			{
				printf (_("fail read reg info\n"));
				goto free4_fail;
			}
			buffer[rb] = '\0';

			if (strncmp(buffer, 
						ANY_REG_HEAD, 
						strlen(ANY_REG_HEAD))!=0)
			{
				printf (_("bad reg head: %s\n"),
						buffer);
				goto free4_fail;
			}

			info->si_inode_table[mpos].i_info.file_frags = 
				(typeof(info->si_inode_table[mpos].i_info.file_frags))
				malloc(sizeof(struct any_file_frags));
			if (!info->si_inode_table[mpos].i_info.file_frags) 
			{ 
				printf (_("fail malloc\n"));
				ret = -ENOMEM; goto free4_fail; }
			memset(info->si_inode_table[mpos].i_info.file_frags,
				 0,	sizeof(struct any_file_frags));

			bpos = strlen(ANY_REG_HEAD);
			
			info->si_inode_table[mpos].i_info.file_frags->fr_nfrags = 
				strtoul (buffer + bpos,
					NULL, 16);
			
			info->si_inode_table[mpos].i_info.file_frags->fr_frags = 
				(typeof(info->si_inode_table[mpos].i_info.file_frags->fr_frags))
				malloc(info->si_inode_table[mpos].i_info.
						file_frags->fr_nfrags*
						sizeof(struct 
							any_file_fragment));
			if (!info->si_inode_table[mpos].
					i_info.file_frags->fr_frags) 
			{ ret = -ENOMEM; goto free4_fail; }
			memset(info->si_inode_table[mpos].i_info.file_frags->fr_frags,
					0,info->si_inode_table[mpos].i_info.
					file_frags->fr_nfrags*
					sizeof(struct
						any_file_fragment));

			bpos += 9;

			for (i=0; i < info->si_inode_table[mpos].i_info.
					file_frags->fr_nfrags; i++)
			{
				if ( (bpos+18) > rb ) {
					lseek(file, 
							-(rb-bpos), 
							SEEK_CUR);
					
					rb = read(file, buffer,
							ANY_BUFFER_SIZE);
					if ( rb == -1 ) goto free4_fail;
					buffer[rb] = '\0';

					bpos=0;
					if (rb<18)
					{	
						printf (_("unexpected end of file\n"));
						goto free4_fail;
					}
				}
				
				info->si_inode_table[mpos].i_info.
					file_frags->fr_frags[i].fr_start = 
					strtoul (buffer + bpos,
							NULL, 16);

				bpos+=9;
				
				info->si_inode_table[mpos].i_info.
					file_frags->fr_frags[i].fr_length = 
					strtoul (buffer + bpos,
							NULL, 16);

				bpos+=9;

				if (!info->si_inode_table[mpos].i_size)
				{
					info->si_inode_table[mpos].i_info.
						file_frags->fr_frags[i].fr_start = 0;
					info->si_inode_table[mpos].i_info.
						file_frags->fr_frags[i].fr_length = 0;
				}
			}
		}

		if ( S_ISDIR(info->si_inode_table[mpos].i_mode) ) {
			unsigned long bpos;
			unsigned long i;
			struct any_dirent *dirent;
			struct any_dirent **pnext;

			lseek(file, 
					ANY_DATA_OFFSET(info->si_inodes) +
					info->si_inode_table[mpos].i_it_file_offset, 
					SEEK_SET);

			rb = read(file, buffer,
					ANY_BUFFER_SIZE);
			if ( rb == -1 ) goto free4_fail;
			buffer[rb] = '\0';

			if (strncmp(buffer, 
						ANY_DIR_HEAD, 
						strlen(ANY_DIR_HEAD))!=0)
				goto free4_fail;

			info->si_inode_table[mpos].i_info.dir = 
				(typeof(info->si_inode_table[mpos].i_info.dir))
				malloc(sizeof(struct any_dir));
			if (!info->si_inode_table[mpos].i_info.dir) 
			{ ret = -ENOMEM; goto free4_fail; }
			memset (info->si_inode_table[mpos].i_info.dir,
				 0,	sizeof(struct any_dir));

			bpos = strlen(ANY_DIR_HEAD);
			
			info->si_inode_table[mpos].i_info.dir->d_ndirents = 
				strtoul (buffer + bpos,
					NULL, 16);

			bpos+=9;

			pnext = &info->si_inode_table[mpos].i_info.dir->
				d_dirent;

			for (i=0; i < info->si_inode_table[mpos].i_info.
					dir->d_ndirents; i++)
			{
				dirent = (typeof(dirent))
					malloc( sizeof(struct any_dirent));
				if (!dirent) 
				{ ret = -ENOMEM; goto free4_fail; }
				memset (dirent, 0, sizeof(struct any_dirent));

				*pnext = dirent;

				pnext = &dirent->d_next;
				
				if ( (bpos+256) > rb ) {
					lseek(file, 
							-(rb-bpos), 
							SEEK_CUR);
					
					rb = read(file, buffer,
							ANY_BUFFER_SIZE);
					if ( rb == -1 ) goto free4_fail;
					buffer[rb] = '\0';

					bpos=0;
					if (rb<9) goto free4_fail;
				}

				if ( strlen(buffer+bpos) > ANY_NAME_LENGTH )
				{ ret = -ENAMETOOLONG; goto free4_fail; }

				dirent->d_name = (typeof(dirent->d_name))
					malloc(strlen(buffer+bpos)+1);
				if (!dirent->d_name) 
				{ ret = -ENOMEM; goto free4_fail; }
				memset (dirent->d_name,
					 0,	strlen(buffer+bpos));

				strcpy ( dirent->d_name,
						buffer+bpos );

				bpos += strlen(buffer+bpos)+1;
				
				dirent->d_inode =
					strtoul (buffer + bpos,
							NULL, 16);

				bpos+=9;
			}
		}
	}
	
	close(file);

	*it = info;
	
#ifdef DEBUG	
	printf ("read OK\n");
#endif /*DEBUG*/

	return 0;

free4_fail:
#ifdef DEBUG	
	printf ("fail 4\n");
#endif /*DEBUG*/
	for (mpos=0; mpos < info->si_inodes; mpos++) {

		if ( S_ISLNK(info->si_inode_table[mpos].i_mode) ) {
			if (!info->si_inode_table[mpos].i_info.symlink) break;
			free( info->si_inode_table[mpos].i_info.symlink );
		}
		
		if ( S_ISREG(info->si_inode_table[mpos].i_mode) ) {
			if (!info->si_inode_table[mpos].i_info.file_frags) break;
			if (info->si_inode_table[mpos].i_info.
					file_frags->fr_frags)
				free( info->si_inode_table[mpos].i_info.
						file_frags->fr_frags );
			free( info->si_inode_table[mpos].i_info.file_frags );
		}
		
		if ( S_ISDIR(info->si_inode_table[mpos].i_mode) ) {
			struct any_dirent	*dirent;
			struct any_dirent       *nextdirent;
			
			if (!info->si_inode_table[mpos].i_info.dir) break;

			dirent = info->si_inode_table[mpos].i_info.
				dir->d_dirent;
			for (; dirent; 
					dirent = nextdirent) {
				if (dirent->d_name)
					free( dirent->d_name );
				nextdirent = dirent->d_next;
				free( dirent );
			}
				
			free( info->si_inode_table[mpos].i_info.dir );
		}
	}
	
free3_fail:
#ifdef DEBUG	
	printf ("fail 3\n");
#endif /*DEBUG*/
	free(info->si_inode_table);
	
free2_fail:
#ifdef DEBUG	
	printf ("fail 2\n");
#endif /*DEBUG*/
	free(info->si_inode_bitmap);

close_fail:
#ifdef DEBUG	
	printf ("close fail\n");
#endif /*DEBUG*/
	close(file);

free_fail:
	free(info->si_itfilename);

info_fail:
#ifdef DEBUG	
	printf ("free_info\n");
#endif /*DEBUG*/
	free(info);    

//out:
#ifdef DEBUG	
	printf ("out\n");
#endif /*DEBUG*/
	return (ret)?ret:-EINVAL;
}
