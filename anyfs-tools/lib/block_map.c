/*
 *	block_map.c
 *	(C) 2005-2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 */

#include <sys/ioctl.h>
#include <linux/types.h>

#include <stdio.h>

#include "bitops.h"
#include "block_map.h"
#include "any.h"
#include "super.h"

extern int verbose;

int search_intersect_inodes(struct any_sb_info *info, unsigned long *block_bitmap,
		any_blk_t dev_size, unsigned long mpos, unsigned long frag,
		unsigned long blk, unsigned long len)
{
	unsigned long fr_start =
		info->si_inode_table[mpos].i_info.
		file_frags->fr_frags[frag].fr_start;

	unsigned long fr_length =
		info->si_inode_table[mpos].i_info.
		file_frags->fr_frags[frag].fr_length;

	fprintf (stderr, _("We have inode #%lu with fragment #%lu (blocks from %lu to %lu)\n"
			   "The inode intersects by blocks from %lu to %lu with the next inodes:\n\n"),
			mpos, frag, fr_start, fr_start+fr_length-1,
			fr_start+blk, fr_start+blk+len-1);

	if (verbose<2) 
	{
		fprintf (stderr, _("verbose < 2.. Skip list..\n\n") );
		return 0;
	}

	unsigned long mpos2;
	unsigned long frag2;

	for (mpos2=0; mpos2 < mpos; mpos2++) {
		if ( !info->si_inode_table[mpos2].i_links_count ) continue;

		if ( S_ISREG(info->si_inode_table[mpos2].i_mode) ) {
			unsigned long fr_nfrags2 = info->si_inode_table[mpos2].i_info.
				file_frags->fr_nfrags;

			for ( frag2=0; frag2 < fr_nfrags2; frag2++ )
		       	{
				unsigned long fr_start2 =
					info->si_inode_table[mpos2].i_info.
					file_frags->fr_frags[frag2].fr_start;

				unsigned long fr_length2 =
					info->si_inode_table[mpos2].i_info.
					file_frags->fr_frags[frag2].fr_length;

				/*skip sparse blocks*/
				if ( fr_start2 )
				{
					unsigned long left = max_t(unsigned long, fr_start+blk, fr_start2);
					unsigned long right = min_t(unsigned long, 
							fr_start + blk + len - 1, 
							fr_start2 + fr_length2 - 1);
					if (right>=left)
					{
						fprintf (stderr, _("\tinode #%lu with fragment #%lu (blocks from %lu to %lu)\n"
								   "\tby blocks from %lu to %lu\n\n"), 
								mpos2, frag2, fr_start2, fr_start2 + fr_length2 - 1,
								left, right);
					}
				}
			}
		}
	}

	return 0;
}

int fill_block_bitmap (struct any_sb_info *info, unsigned long *block_bitmap, 
		any_blk_t dev_size, int check_intersects) 
{
	unsigned long mpos;
	unsigned long frag;
	unsigned long blk;
	int err = 0;
	
	test_and_set_bit ( 0, block_bitmap );
	
	for (mpos=0; mpos < info->si_inodes; mpos++) {
		if ( !info->si_inode_table[mpos].i_links_count ) continue;

		if ( S_ISREG(info->si_inode_table[mpos].i_mode) ) {
			unsigned long fr_nfrags = info->si_inode_table[mpos].i_info.
				file_frags->fr_nfrags;

			for ( frag=0; frag < fr_nfrags; frag++ )
		       	{
				unsigned long fr_start =
					info->si_inode_table[mpos].i_info.
					file_frags->fr_frags[frag].fr_start;

				unsigned long fr_length =
					info->si_inode_table[mpos].i_info.
					file_frags->fr_frags[frag].fr_length;

				/*skip sparse blocks*/
				if ( fr_start )
					for (blk = 0; blk < fr_length; blk++ )
					{
						if ( fr_start + blk < dev_size &&
								test_and_set_bit ( fr_start + blk, block_bitmap ) )
						{
							if (!check_intersects) continue;

							unsigned long blk0 = blk;
							unsigned long len = 0;

							for ( ; blk < fr_length; blk++ )
							{
								if ( test_and_set_bit ( fr_start + blk, block_bitmap ) )
									len++;
								else break;
							}

							if (!err)
							{
								fprintf (stderr, _("Shared blocks was founded. Read the next information attentively,\n"
										   "mount anyfs filesystem and delete not needed inodes.\n"
										   "Then restart the utility\n\n"));
							}

							search_intersect_inodes(info, block_bitmap, dev_size,
									mpos, frag, blk0, len);

							err = -1;
						}
					}
			}
		}
	}

	if (err)
	{
		fprintf (stderr, _("Shared blocks was founded. Read information above attentively,\n"
				   "mount anyfs filesystem and delete not needed inodes.\n"
				   "Then restart the utility\n"));
	}
	return err;
}
