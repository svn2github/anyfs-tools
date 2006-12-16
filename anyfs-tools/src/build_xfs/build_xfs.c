/*
 * build_xfs.c - Build a XFS filesystem using external anyfs inode table
 * Copyright (C) 2006 Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 *
 * BASED ON mkfs.xfs from xfsprogs,
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <xfs/libxfs.h>
#include <disk/fstyp.h>
#include <disk/volume.h>
#include <ctype.h>
#include "xfs_mkfs.h"

#include <math.h>

#undef _
#undef bindtextdomain
#undef textdomain
#include "any.h"
#define _EXT2_TYPES_H
#include "bitops.h"

#include "block_map.h"
#include "super.h"
#include "progress.h"
#include "release_fssys.h"
#include "version.h"

/*
 * Prototypes for internal functions.
 */
static void conflict(char opt, char *tab[], int oldidx, int newidx);
static void illegal(char *value, char *opt);
static void reqval(char opt, char *tab[], int idx);
static void respec(char opt, char *tab[], int idx);
static void unknown(char opt, char *s);
static int  ispow2(unsigned int i);

int	verbose;
int	noaction;
int	quiet;

/*
 * option tables for getsubopt calls
 */
char *bopts[] = {
#define	B_LOG		0
	"log",
#define	B_SIZE		1
	"size",
	NULL
};

char	*dopts[] = {
#define	D_AGCOUNT	0
	"agcount",
#define	D_FILE		1
	"file",
#define	D_NAME		2
	"name",
#define	D_SIZE		3
	"size",
#define D_SUNIT		4
	"sunit",
#define D_SWIDTH	5
	"swidth",
#define D_UNWRITTEN	6
	"unwritten",
#define D_AGSIZE	7
	"agsize",
#define D_SU		8
	"su",
#define D_SW		9
	"sw",
#define D_SECTLOG	10
	"sectlog",
#define D_SECTSIZE	11
	"sectsize",
#define D_NOALIGN	12
	"noalign",
#define D_RTINHERIT	13
	"rtinherit",
#define D_PROJINHERIT	14
	"projinherit",
#define D_EXTSZINHERIT	15
	"extszinherit",
	NULL
};

char	*iopts[] = {
#define	I_ALIGN		0
	"align",
#define	I_LOG		1
	"log",
#define	I_MAXPCT	2
	"maxpct",
#define	I_PERBLOCK	3
	"perblock",
#define	I_SIZE		4
	"size",
#define	I_ATTR		5
	"attr",
	NULL
};

char	*lopts[] = {
#define	L_AGNUM		0
	"agnum",
#define	L_INTERNAL	1
	"internal",
#define	L_SIZE		2
	"size",
#define L_VERSION	3
	"version",
#define L_SUNIT		4
	"sunit",
#define L_SU		5
	"su",
#define L_DEV		6
	"logdev",
#define	L_SECTLOG	7
	"sectlog",
#define	L_SECTSIZE	8
	"sectsize",
#define	L_FILE		9
	"file",
#define	L_NAME		10
	"name",
	NULL
};

char	*nopts[] = {
#define	N_LOG		0
	"log",
#define	N_SIZE		1
	"size",
#define	N_VERSION	2
	"version",
	NULL,
};

char	*ropts[] = {
#define	R_EXTSIZE	0
	"extsize",
#define	R_SIZE		1
	"size",
#define	R_DEV		2
	"rtdev",
#define	R_FILE		3
	"file",
#define	R_NAME		4
	"name",
#define R_NOALIGN	5
	"noalign",
	NULL
};

char	*sopts[] = {
#define	S_LOG		0
	"log",
#define	S_SECTLOG	1
	"sectlog",
#define	S_SIZE		2
	"size",
#define	S_SECTSIZE	3
	"sectsize",
	NULL
};

#define TERABYTES(count, blog)	((__uint64_t)(count) << (40 - (blog)))
#define GIGABYTES(count, blog)	((__uint64_t)(count) << (30 - (blog)))
#define MEGABYTES(count, blog)	((__uint64_t)(count) << (20 - (blog)))

/*
 * Use this macro before we have superblock and mount structure
 */
#define	DTOBT(d)	((xfs_drfsbno_t)((d) >> (blocklog - BBSHIFT)))

/*
 * Use this for block reservations needed for mkfs's conditions
 * (basically no fragmentation).
 */
#define	MKFS_BLOCKRES_INODE	\
	((uint)(XFS_IALLOC_BLOCKS(mp) + (XFS_IN_MAXLEVELS(mp) - 1)))
#define	MKFS_BLOCKRES(rb)	\
	((uint)(MKFS_BLOCKRES_INODE + XFS_DA_NODE_MAXDEPTH + \
	(XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) - 1) + (rb)))

/* amount (in bytes) we zero at the beginning and end of the device to
 * remove traces of other filesystems, raid superblocks, etc.
 */
#define WHACK_SIZE (128 * 1024)

static void
fail(
	char	*msg,
	int	i)
{
	fprintf(stderr, "%s: %s [%d - %s]\n", progname, msg, i, strerror(i));
	exit(1);
}

void
res_failed(
	int	i)
{
	fail(_("cannot reserve space"), i);
}

static void
calc_stripe_factors(
	int		dsu,
	int		dsw,
	int		dsectsz,
	int		lsu,
	int		lsectsz,
	int		*dsunit,
	int		*dswidth,
	int		*lsunit)
{
	/* Handle data sunit/swidth options */
	if (*dsunit || *dswidth) {
		if (dsu || dsw) {
			fprintf(stderr,
				_("data su/sw must not be used in "
				"conjunction with data sunit/swidth\n"));
			usage();
		}

		if ((*dsunit && !*dswidth) || (!*dsunit && *dswidth)) {
			fprintf(stderr,
				_("both data sunit and data swidth options "
				"must be specified\n"));
			usage();
		}
	}

	if (dsu || dsw) {
		if (*dsunit || *dswidth) {
			fprintf(stderr,
				_("data sunit/swidth must not be used in "
				"conjunction with data su/sw\n"));
			usage();
		}

		if ((dsu && !dsw) || (!dsu && dsw)) {
			fprintf(stderr,
				_("both data su and data sw options "
				"must be specified\n"));
			usage();
		}

		if (dsu % dsectsz) {
			fprintf(stderr,
				_("data su must be a multiple of the "
				"sector size (%d)\n"), dsectsz);
			usage();
		}

		*dsunit  = (int)BTOBBT(dsu);
		*dswidth = *dsunit * dsw;
	}

	if (*dsunit && (*dswidth % *dsunit != 0)) {
		fprintf(stderr,
			_("data stripe width (%d) must be a multiple of the "
			"data stripe unit (%d)\n"), *dswidth, *dsunit);
		usage();
	}

	/* Handle log sunit options */

	if (*lsunit) {
		if (lsu) {
			fprintf(stderr,
				_("log su should not be used in "
				"conjunction with log sunit\n"));
			usage();
		}
	}

	if (lsu) {
		if (*lsunit) {
			fprintf(stderr,
				_("log sunit should not be used in "
				"conjunction with log su\n"));
			usage();
		}
		*lsunit = (int)BTOBBT(lsu);
	}
}

static int
check_overwrite(
	char		*device)
{
	char		*type;

	if (device && *device) {
		if ((type = fstype(device)) != NULL) {
			fprintf(stderr,
				_("%s: %s appears to contain an existing "
				"filesystem (%s).\n"), progname, device, type);
			return 1;
		}
		if ((type = pttype(device)) != NULL) {
			fprintf(stderr,
				_("%s: %s appears to contain a partition "
				"table (%s).\n"), progname, device, type);
			return 1;
		}
	}
	return 0;
}

static void
fixup_log_stripe_unit(
	int		lsflag,
	int		sunit,
	xfs_drfsbno_t	*logblocks,
	int		blocklog)
{
	__uint64_t	tmp_logblocks;

	/*
	 * Make sure that the log size is a multiple of the stripe unit
	 */
	if ((*logblocks % sunit) != 0) {
		if (!lsflag) {
			tmp_logblocks = ((*logblocks + (sunit - 1))
						/ sunit) * sunit;
			/*
			 * If the log is too large, round down
			 * instead of round up
			 */
			if ((tmp_logblocks > XFS_MAX_LOG_BLOCKS) ||
			    ((tmp_logblocks << blocklog) > XFS_MAX_LOG_BYTES)) {
				tmp_logblocks = (*logblocks / sunit) * sunit;
			}
			*logblocks = tmp_logblocks;
		} else {
			fprintf(stderr, _("log size %lld is not a multiple "
					  "of the log stripe unit %d\n"),
				(long long) *logblocks, sunit);
			usage();
		}
	}
}

static xfs_dfsbno_t
fixup_internal_log_stripe(
	xfs_mount_t	*mp,
	int		lsflag,
	xfs_dfsbno_t	logstart,
	__uint64_t	agsize,
	int		sunit,
	xfs_drfsbno_t	*logblocks,
	int		blocklog,
	int		*lalign)
{
	if ((logstart % sunit) != 0) {
		logstart = ((logstart + (sunit - 1))/sunit) * sunit;
		*lalign = 1;
	}

	fixup_log_stripe_unit(lsflag, sunit, logblocks, blocklog);

	if (*logblocks > agsize - XFS_FSB_TO_AGBNO(mp, logstart)) {
		fprintf(stderr,
			_("Due to stripe alignment, the internal log size "
			"(%lld) is too large.\n"), (long long) *logblocks);
		fprintf(stderr, _("Must fit within an allocation group.\n"));
		usage();
	}
	return logstart;
}

void
validate_log_size(__uint64_t logblocks, int blocklog, int min_logblocks)
{
	if (logblocks < min_logblocks) {
		fprintf(stderr,
	_("log size %lld blocks too small, minimum size is %d blocks\n"),
			(long long)logblocks, min_logblocks);
		usage();
	}
	if (logblocks > XFS_MAX_LOG_BLOCKS) {
		fprintf(stderr,
	_("log size %lld blocks too large, maximum size is %d blocks\n"),
			(long long)logblocks, XFS_MAX_LOG_BLOCKS);
		usage();
	}
	if ((logblocks << blocklog) > XFS_MAX_LOG_BYTES) {
		fprintf(stderr,
	_("log size %lld bytes too large, maximum size is %d bytes\n"),
			(long long)(logblocks << blocklog), XFS_MAX_LOG_BYTES);
		usage();
	}
}

void
calc_default_ag_geometry(
	int		blocklog,
	__uint64_t	dblocks,
	__uint64_t	*agsize,
	__uint64_t	*agcount)
{
	__uint64_t	blocks;
	__uint64_t	count = 0;
	int		shift = 0;

	/*
	 * First handle the extremes - the points at which we will
	 * always use the maximum AG size, the points at which we
	 * always use the minimum, and a "small-step" for 16-128Mb.
	 */
	if (dblocks >= TERABYTES(32, blocklog)) {
		blocks = XFS_AG_MAX_BLOCKS(blocklog);
		goto done;
	} else if (dblocks < MEGABYTES(16, blocklog)) {
		blocks = dblocks;
		count = 1;
		goto done;
	} else if (dblocks < MEGABYTES(128, blocklog)) {
		blocks = MEGABYTES(16, blocklog);
		goto done;
	}

	/*
	 * For the remainder we choose an AG size based on the
	 * number of data blocks available, trying to keep the
	 * number of AGs relatively small (especially compared
	 * to the original algorithm).  AG count is calculated
	 * based on the prefered AG size, not vice-versa - the
	 * count can be increased by growfs, so prefer to use
	 * smaller counts at mkfs time.
	 * 
	 * This scales us up smoothly between min/max AG sizes.
	 */
	if (dblocks > GIGABYTES(512, blocklog))
		shift = 5;
	else if (dblocks > GIGABYTES(8, blocklog))
		shift = 4;
	else if (dblocks >= MEGABYTES(128, blocklog))
		shift = 3;
	else
		ASSERT(0);
	blocks = dblocks >> shift;

done:
	if (!count)
		count = dblocks / blocks + (dblocks % blocks != 0);
	*agsize = blocks;
	*agcount = count;
}

static void
validate_ag_geometry(
	int		blocklog,
	__uint64_t	dblocks,
	__uint64_t	agsize,
	__uint64_t	agcount)
{
	if (agsize < XFS_AG_MIN_BLOCKS(blocklog)) {
		fprintf(stderr,
	_("agsize (%lldb) too small, need at least %lld blocks\n"),
			(long long)agsize,
			(long long)XFS_AG_MIN_BLOCKS(blocklog));
		usage();
	}

	if (agsize > XFS_AG_MAX_BLOCKS(blocklog)) {
		fprintf(stderr,
	_("agsize (%lldb) too big, maximum is %lld blocks\n"),
			(long long)agsize,
			(long long)XFS_AG_MAX_BLOCKS(blocklog));
		usage();
	}

	if (agsize > dblocks) {
		fprintf(stderr,
	_("agsize (%lldb) too big, data area is %lld blocks\n"),
			(long long)agsize, (long long)dblocks);
			usage();
	}

	if (agsize < XFS_AG_MIN_BLOCKS(blocklog)) {
		fprintf(stderr,
	_("too many allocation groups for size = %lld\n"),
				(long long)agsize);
		fprintf(stderr, _("need at most %lld allocation groups\n"),
			(long long)(dblocks / XFS_AG_MIN_BLOCKS(blocklog) +
				(dblocks % XFS_AG_MIN_BLOCKS(blocklog) != 0)));
		usage();
	}

	if (agsize > XFS_AG_MAX_BLOCKS(blocklog)) {
		fprintf(stderr,
	_("too few allocation groups for size = %lld\n"), (long long)agsize);
		fprintf(stderr,
	_("need at least %lld allocation groups\n"),
		(long long)(dblocks / XFS_AG_MAX_BLOCKS(blocklog) + 
			(dblocks % XFS_AG_MAX_BLOCKS(blocklog) != 0)));
		usage();
	}

	/*
	 * If the last AG is too small, reduce the filesystem size
	 * and drop the blocks.
	 */
	if ( dblocks % agsize != 0 &&
	     (dblocks % agsize < XFS_AG_MIN_BLOCKS(blocklog))) {
		fprintf(stderr,
	_("last AG size %lld blocks too small, minimum size is %lld blocks\n"),
			(long long)(dblocks % agsize),
			(long long)XFS_AG_MIN_BLOCKS(blocklog));
		usage();
	}

	/*
	 * If agcount is too large, make it smaller.
	 */
	if (agcount > XFS_MAX_AGNUMBER + 1) {
		fprintf(stderr,
	_("%lld allocation groups is too many, maximum is %lld\n"),
			(long long)agcount, (long long)XFS_MAX_AGNUMBER + 1);
		usage();
	}
}

int xfs_fd;
unsigned long xfs_blocksize;
unsigned long xfs_dirblocksize;
unsigned long xfs_blocks_count;
unsigned long *xfs_block_bitmap;

unsigned long xfs_agsize;
unsigned long xfs_agblklog;

#define XFS_BLKNO(bno) ( ((bno)/xfs_agsize)<<xfs_agblklog | ((bno)%xfs_agsize) )
#define ANYFS_BLKNO(bno) ( ((bno)>>xfs_agblklog)*xfs_agsize + ((bno)&((1<<xfs_agblklog)-1)) )

unsigned long xfs_aginodes;
unsigned long xfs_aginolog;

#define XFS_INO(ino) ( ((ino)/xfs_aginodes)<<xfs_aginolog | ((ino)%xfs_aginodes) )
#define ANYFS_INO(ino) ( ((ino)>>xfs_aginolog)*xfs_aginodes + ((ino)&((1<<xfs_aginolog)-1)) )

int readblk(unsigned long from, unsigned long n, char *buffer)
{
	ssize_t r = pread64(xfs_fd, buffer, n*xfs_blocksize, from*xfs_blocksize);
	if (r<0) return errno;
	if (r<n) return 1;
	return 0;
}

int writeblk(unsigned long from, unsigned long n, char *buffer)
{
	ssize_t r = pwrite64(xfs_fd, buffer, n*xfs_blocksize, from*xfs_blocksize);
	if (r<0) return errno;
	if (r<n) return 1;
	return 0;
}

int testblk(unsigned long bitno)
{
	return test_bit(bitno, xfs_block_bitmap);
}

unsigned long getblkcount()
{
	return xfs_blocks_count;
}

struct blockskeys {
	uint64_t startoff;
	uint64_t child;
};

uint64_t write_extentlist( 
		struct any_file_frags *frags, 
	        xfs_dinode_core_t *inode, 
	        int isize, 
	        void *p_inode,
	        uint32_t countu,
		uint32_t countleaf)
{
	unsigned long j;
	uint32_t nullfrags = 0;

	for (j=0; j < frags->fr_nfrags; j++)
	{
		uint64_t startblock =
			frags->fr_frags[j].fr_start;
		uint64_t blockcount =
			frags->fr_frags[j].fr_length;

		if (!startblock || !blockcount) 
		{
			nullfrags++;
			continue;
		}
	}

	uint64_t size = (frags->fr_nfrags - nullfrags)*16;
	uint64_t nblocks = 0;
	uint32_t blocksize = xfs_blocksize;
	uint64_t dblocks = xfs_blocks_count;

	/*FIXME*/
	countu *= xfs_dirblocksize/xfs_blocksize;
	countleaf *= xfs_dirblocksize/xfs_blocksize;

	//uint64_t xfs_dirdatablk = XFS_DIR2_DATA_OFFSET/xfs_blocksize;
	uint64_t xfs_dirleafblk = XFS_DIR2_LEAF_OFFSET/xfs_blocksize;
	uint64_t xfs_dirfreeblk = XFS_DIR2_FREE_OFFSET/xfs_blocksize;
	
	if ( (isize - sizeof(xfs_dinode_core_t) - 4)<size )
	{
		/*Long extent list*/

		INT_SET(inode->di_format, ARCH_CONVERT, XFS_DINODE_FMT_BTREE);
		if (verbose>=2) fprintf(stderr, "Long extent list\n");

		uint32_t numrecs_max = (blocksize - 24)/16;
		uint32_t numrecs_rest = frags->fr_nfrags - nullfrags;
		uint32_t bmap_blocks = ( numrecs_rest + numrecs_max - 1 )/numrecs_max;
		uint32_t nblock = 0;
		uint32_t nrec_inblock = 0;

		int n = frags->fr_nfrags;
		int numrecs_0 = floorf( (float)n/bmap_blocks );
		int numrecs_1 = numrecs_0+1;

		ASSERT(bmap_blocks==1 || numrecs_0 >= (numrecs_max/2));
		ASSERT(numrecs_1 <= (numrecs_max+1));

		//int n0 = numrecs_0*bmap_blocks;
		int n1 = numrecs_1*bmap_blocks;

		int l0 = n1-n;
		/*int l1 = n-n0;*/

		uint16_t numrecs_val = (nblock<l0)?numrecs_0:numrecs_1;

		uint32_t level = 0;

		uint32_t b = find_first_zero_bit(
				xfs_block_bitmap,
				dblocks);

		if ( b >= dblocks )
		{
			fprintf(stderr, "Cannot find free block for blockmap: not enough space\n");
			int retval = -ENOSPC; exit(-retval);
		}

		set_bit ( b, xfs_block_bitmap );

		char* bbuf = (char*) malloc(xfs_blocksize);
		memset(bbuf, 0, xfs_blocksize);

		void *bp = (void*) bbuf;

		uint32_t *magic = (uint32_t *)bp;
		bp = (void*) (magic+1);

		uint16_t *plevel = (uint16_t *)bp;
		bp = (void*) (plevel+1);

		uint16_t *numrecs = (uint16_t *)bp;
		bp = (void*) (numrecs+1);

		uint64_t *leftsib = (uint64_t *)bp;
		bp = (void*) (leftsib+1);

		uint64_t *rightsib = (uint64_t *)bp;
		bp = (void*) (rightsib+1);

		struct blockskeys *blocks =
			malloc(sizeof(struct blockskeys)*bmap_blocks);

		ASSERT( ((char*)bp-(char*)bbuf) <= blocksize );

		INT_SET(*magic, ARCH_CONVERT, XFS_BMAP_MAGIC);
		INT_SET(*plevel, ARCH_CONVERT, level);
		INT_SET(*numrecs, ARCH_CONVERT, numrecs_val);
		INT_SET(*leftsib, ARCH_CONVERT, NULLFSBLOCK);
		INT_SET(*rightsib, ARCH_CONVERT, NULLFSBLOCK);

		uint64_t startoff = 0;
		for (j=0; j < frags->fr_nfrags; j++)
		{
			uint64_t startblock =
				XFS_BLKNO(frags->fr_frags[j].fr_start);
			uint64_t blockcount =
				frags->fr_frags[j].fr_length;

			if (!startblock || !blockcount) continue;

			if (nrec_inblock==numrecs_val)
			{
				uint64_t leftsib_val = b;

				b = find_first_zero_bit(
						xfs_block_bitmap,
						dblocks);

				if ( b >= dblocks )
				{
					fprintf(stderr, "Cannot find free block for blockmap: not enough space\n");
					int retval = -ENOSPC; exit(-retval);
				}

				set_bit ( b, xfs_block_bitmap );

				INT_SET(*rightsib, ARCH_CONVERT, XFS_BLKNO(b));
				if (!noaction)
					pwrite64(xfs_fd, bbuf, blocksize, leftsib_val*blocksize);

				memset (bbuf, 0, blocksize);

				bp = (void*) bbuf;

				uint32_t *magic = (uint32_t *)bp;
				bp = (void*) (magic+1);

				uint16_t *plevel = (uint16_t *)bp;
				bp = (void*) (plevel+1);

				uint16_t *numrecs = (uint16_t *)bp;
				bp = (void*) (numrecs+1);

				uint64_t *leftsib = (uint64_t *)bp;
				bp = (void*) (leftsib+1);

				uint64_t *rightsib = (uint64_t *)bp;
				bp = (void*) (rightsib+1);

				nblock++;
				nrec_inblock=0;
				numrecs_val = (nblock<l0)?numrecs_0:numrecs_1;

				INT_SET(*magic, ARCH_CONVERT, XFS_BMAP_MAGIC);
				INT_SET(*plevel, ARCH_CONVERT, level);
				INT_SET(*numrecs, ARCH_CONVERT, numrecs_val);
				INT_SET(*leftsib, ARCH_CONVERT, XFS_BLKNO(leftsib_val));
				INT_SET(*rightsib, ARCH_CONVERT, NULLFSBLOCK);
			}

			nblocks += blockcount;

			if (!nrec_inblock)
			{
				blocks[nblock].startoff = startoff;
				blocks[nblock].child = b;
			}

			if (verbose>=3)                         
				printf ("fragment: %lu from %u (startblock = %llu, blockcount = %llu)\n", j,
						frags->fr_nfrags, (unsigned long long) startblock, 
								  (unsigned long long) blockcount);

			uint32_t l0=0, l1=0, l2=0, l3=0;

			if (countu && startoff < xfs_dirleafblk && startoff >= countu)
				startoff = xfs_dirleafblk;

			if (countleaf && startoff >= xfs_dirleafblk && startoff < xfs_dirfreeblk 
					&& (startoff-xfs_dirleafblk)>=countleaf)
				startoff = xfs_dirfreeblk;

			l0 = startoff >> 23;
			l1 = ((startoff & 0x7fffff)<<9) | ((startblock >> 43)&0x1ff);
			l2 = (startblock >> 11)&0xffffffff;
			l3 = ((startblock & 0x7ff)<<21) | (blockcount & 0x1fffff);

			uint32_t *l = (uint32_t *) bp;

			INT_SET(l[0], ARCH_CONVERT, l0);
			INT_SET(l[1], ARCH_CONVERT, l1);
			INT_SET(l[2], ARCH_CONVERT, l2);
			INT_SET(l[3], ARCH_CONVERT, l3);

			bp = (void*) (l+4);

			ASSERT( ((char*)bp-(char*)bbuf) <= blocksize );

			startoff += blockcount;

			numrecs_rest--;
			nrec_inblock++;
		}
		nblock++;
		
		nblocks+=nblock;

		ASSERT(!numrecs_rest);
		ASSERT(nblock==bmap_blocks);

		if (!noaction)
			pwrite64(xfs_fd, bbuf, blocksize, b*blocksize);

		int numrecs_max_at_inode = (isize - ((char*)p_inode - (char*)inode) - 4)/16;

		while( nblock > numrecs_max_at_inode )
		{
			level++;

			uint32_t bmap_blocks0 = nblock;

			numrecs_max = (blocksize - 24)/16;
			numrecs_rest = bmap_blocks0;
			bmap_blocks = ( numrecs_rest + numrecs_max - 1 )/numrecs_max;
			nblock = 0;
			nrec_inblock = 0;

			int n = bmap_blocks0;
			int numrecs_0 = floorf( (float)n/bmap_blocks );
			int numrecs_1 = numrecs_0+1;

			ASSERT(bmap_blocks==1 || numrecs_0 >= (numrecs_max/2));
			ASSERT(numrecs_1 <= (numrecs_max+1));

			//int n0 = numrecs_0*bmap_blocks;
			int n1 = numrecs_1*bmap_blocks;

			int l0 = n1-n;
			/*int l1 = n-n0;*/

			numrecs_val = (nblock<l0)?numrecs_0:numrecs_1;

			struct blockskeys *blocks_r = blocks;

			blocks = malloc(sizeof(struct blockskeys)*bmap_blocks);

			b = find_first_zero_bit(
					xfs_block_bitmap,
					dblocks);

			if ( b >= dblocks )
			{
				fprintf(stderr, "Cannot find free block for blockmap: not enough space\n");
				int retval = -ENOSPC; exit(-retval);
			}

			set_bit ( b, xfs_block_bitmap );

			memset (bbuf, 0, blocksize);

			bp = (void*) bbuf;

			uint32_t *magic = (uint32_t *)bp;
			bp = (void*) (magic+1);

			uint16_t *plevel = (uint16_t *)bp;
			bp = (void*) (plevel+1);

			uint16_t *numrecs = (uint16_t *)bp;
			bp = (void*) (numrecs+1);

			uint64_t *leftsib = (uint64_t *)bp;
			bp = (void*) (leftsib+1);

			uint64_t *rightsib = (uint64_t *)bp;
			bp = (void*) (rightsib+1);

			INT_SET(*magic, ARCH_CONVERT, XFS_BMAP_MAGIC);
			INT_SET(*plevel, ARCH_CONVERT, level);
			INT_SET(*numrecs, ARCH_CONVERT, numrecs_val);
			INT_SET(*leftsib, ARCH_CONVERT, NULLFSBLOCK);
			INT_SET(*rightsib, ARCH_CONVERT, NULLFSBLOCK);

			uint32_t sizerest = blocksize - ((char*) bp - bbuf);
			sizerest = sizerest/16*16;
			void* bp2_s = (void*) ((char*) bp + sizerest/2);
			void* bp2 = bp2_s;

			for (j=0; j < bmap_blocks0; j++)
			{
				if (nrec_inblock==numrecs_val)
				{
					uint64_t leftsib_val = b;
					b = find_first_zero_bit(
							xfs_block_bitmap,
							dblocks);

					if ( b >= dblocks )
					{
						fprintf(stderr, "Cannot find free block for blockmap: not enough space\n");
						int retval = -ENOSPC; exit(-retval);
					}

					set_bit ( b, xfs_block_bitmap );

					INT_SET(*rightsib, ARCH_CONVERT, XFS_BLKNO(b));
					if (!noaction)
						pwrite64(xfs_fd, bbuf, blocksize, leftsib_val*blocksize);

					memset (bbuf, 0, blocksize);

					bp = (void*) bbuf;

					uint32_t *magic = (uint32_t *)bp;
					bp = (void*) (magic+1);

					uint16_t *plevel = (uint16_t *)bp;
					bp = (void*) (plevel+1);

					uint16_t *numrecs = (uint16_t *)bp;
					bp = (void*) (numrecs+1);

					uint64_t *leftsib = (uint64_t *)bp;
					bp = (void*) (leftsib+1);

					uint64_t *rightsib = (uint64_t *)bp;
					bp = (void*) (rightsib+1);

					nblock++;
					nrec_inblock=0;
					numrecs_val = (nblock<l0)?numrecs_0:numrecs_1;

					INT_SET(*magic, ARCH_CONVERT, XFS_BMAP_MAGIC);
					INT_SET(*plevel, ARCH_CONVERT, level);
					INT_SET(*numrecs, ARCH_CONVERT, numrecs_val);
					INT_SET(*leftsib, ARCH_CONVERT, XFS_BLKNO(leftsib_val));
					INT_SET(*rightsib, ARCH_CONVERT, NULLFSBLOCK);
				}

				if (!nrec_inblock)
				{
					blocks[nblock].startoff = blocks_r[j].startoff;
					blocks[nblock].child = b;
				}

				uint64_t *startoff = bp;
				bp = (void*) (startoff+1);

				uint64_t *child = bp2;
				bp2 = (void*) (child+1);

				INT_SET((*startoff), ARCH_CONVERT, blocks_r[j].startoff);
				INT_SET((*child), ARCH_CONVERT, XFS_BLKNO(blocks_r[j].child));

				ASSERT(bp<=bp2_s);
				ASSERT( ((char*)bp2-(char*)bbuf) <= blocksize );

				numrecs_rest--;
				nrec_inblock++;
			}

			nblock++;
			nblocks+=nblock;

			free (blocks_r);
		}

		if (!noaction)
			pwrite64(xfs_fd, bbuf, blocksize, b*blocksize);
		free (bbuf);

		level++;

		ASSERT(nblock);
		uint32_t bmap_blocks0 = nblock;

		numrecs_max = (isize - ((char*)p_inode - (char*)inode) - 4)/16;
		numrecs_rest = bmap_blocks0;
		numrecs_val = min_t(uint32_t, numrecs_max, numrecs_rest);

		struct blockskeys *blocks_r = blocks;

		bbuf = p_inode;
		bp = (void*) bbuf;

		plevel = (uint16_t *)bp;
		bp = (void*) (plevel+1);

		numrecs = (uint16_t *)bp;
		bp = (void*) (numrecs+1);

		INT_SET(*plevel, ARCH_CONVERT, level);
		INT_SET(*numrecs, ARCH_CONVERT, numrecs_val);

		uint32_t sizerest = (isize - ((char*)p_inode - (char*)inode)) - ((char*) bp - bbuf);
		sizerest = sizerest/16*16;
		void* bp2_s = (void*) ((char*) bp + sizerest/2);
		void* bp2 = bp2_s;

		for (j=0; j < bmap_blocks0; j++)
		{
			int thisfieldsize = 16;
			ASSERT ( (blocksize - ((char*) bp - bbuf + bp2 - bp2_s))>=thisfieldsize );

			uint64_t *startoff = bp;
			bp = (void*) (startoff+1);

			uint64_t *child = bp2;
			bp2 = (void*) (child+1);

			ASSERT(bp<=bp2_s);
			ASSERT( ((char*)bp2-(char*)inode) <= isize );

			INT_SET((*startoff), ARCH_CONVERT, blocks_r[j].startoff);
			INT_SET((*child), ARCH_CONVERT, XFS_BLKNO(blocks_r[j].child));
		}

		free (blocks_r);
	}
	else
	{
		/*Short extent list*/
		nullfrags = 0;
		uint64_t startoff = 0;
		for (j=0; j < frags->fr_nfrags; j++)
		{
			uint64_t startblock =
				XFS_BLKNO(frags->fr_frags[j].fr_start);
			uint64_t blockcount =
				frags->fr_frags[j].fr_length;

			if (!startblock || !blockcount) 
			{
				nullfrags++;
				continue;
			}

			nblocks += blockcount;

			if (verbose>=3)                         
				printf ("fragment: %lu from %u (startblock = %llu, blockcount = %llu)\n", j,
						frags->fr_nfrags, (unsigned long long) startblock, 
								(unsigned long long) blockcount);

			uint32_t l0=0, l1=0, l2=0, l3=0;

			if (countu && startoff < xfs_dirleafblk && startoff >= countu)
				startoff = xfs_dirleafblk;

			if (countleaf && startoff >= xfs_dirleafblk && startoff < xfs_dirfreeblk
					&& (startoff-xfs_dirleafblk) >= countleaf)
				startoff = xfs_dirfreeblk;

			l0 = startoff >> 23;
			l1 = ((startoff & 0x7fffff)<<9) | ((startblock >> 43)&0x1ff);
			l2 = (startblock >> 11)&0xffffffff;
			l3 = ((startblock & 0x7ff)<<21) | (blockcount & 0x1fffff);

			uint32_t *l = (uint32_t *) p_inode + 4*(j-nullfrags);

			INT_SET(l[0], ARCH_CONVERT, l0);
			INT_SET(l[1], ARCH_CONVERT, l1);
			INT_SET(l[2], ARCH_CONVERT, l2);
			INT_SET(l[3], ARCH_CONVERT, l3);

			startoff += blockcount;
		}
	}

	INT_SET(inode->di_nblocks, ARCH_CONVERT, nblocks);
	INT_SET(inode->di_nextents, ARCH_CONVERT, frags->fr_nfrags - nullfrags);
	return nblocks;
}

struct hashlist {
	uint32_t hashval;
	uint32_t address;
	uint32_t data;
	struct hashlist *next;
};

void add_to_hashlist_data(struct hashlist **p_hashlist,
		uint32_t hashval, uint32_t address, uint32_t data)
{
	while ( (*p_hashlist) && (hashval >= (*p_hashlist)->hashval) )
		p_hashlist = &(*p_hashlist)->next;

	struct hashlist *oldnext = *p_hashlist;

	(*p_hashlist) = malloc(sizeof(struct hashlist));
	if ( !(*p_hashlist) )
	{
		fprintf(stderr, "Not enough memory");
		int retval = -ENOMEM; exit(-retval);
	}
	(*p_hashlist)->hashval = hashval;
	(*p_hashlist)->address = address;
	(*p_hashlist)->data = data;
	(*p_hashlist)->next = oldnext;
}

void add_to_hashlist(struct hashlist **p_hashlist,
		uint32_t hashval, uint32_t address)
{
	add_to_hashlist_data(p_hashlist, hashval, address, 0);
}

int add_blocks ( struct any_file_frags *dir_frags, uint32_t countu, int dirblkfsbs )
{
	unsigned long dblocks = xfs_blocks_count;
	int i;
	ASSERT(dirblkfsbs);

#define TEST_BITS(from, n, bmp) ({ 				\
	int __res = 0;						\
	for (i=0; i<n; i++) if ( test_bit(from+i, bmp) ) __res = 1;\
	__res;							\
})
#define SET_BITS(from, n, bmp) ({ 				\
	for (i=0; i<n; i++) set_bit(from+i, bmp);		\
})

	if ( !dir_frags->fr_nfrags )
	{
		struct any_file_fragment *frags = 
			malloc (sizeof(struct any_file_fragment)*(countu+1));
		dir_frags->fr_nfrags = 1;
		dir_frags->fr_frags = frags;

		uint32_t nfrag = 0;
		frags[nfrag].fr_start = 0;
		do {
			frags[nfrag].fr_start =
				find_next_zero_bit(
						xfs_block_bitmap,
						dblocks,
						frags[nfrag].fr_start);
		} while ( TEST_BITS(frags[nfrag].fr_start, dirblkfsbs, xfs_block_bitmap) &&
		       (frags[nfrag].fr_start+=1) );
		frags[nfrag].fr_length = dirblkfsbs;

		if ( (frags[nfrag].fr_start + dirblkfsbs)  > dblocks )
			return -1;

		SET_BITS ( frags[nfrag].fr_start, dirblkfsbs, xfs_block_bitmap );
	}
	else
	{
		struct any_file_fragment *frags = dir_frags->fr_frags;
		uint32_t nfrag = dir_frags->fr_nfrags-1;

		uint64_t nextblock = 0;
	        do {
			nextblock = find_next_zero_bit(          
					xfs_block_bitmap,  
					dblocks,
					nextblock);
		} while ( TEST_BITS(nextblock, dirblkfsbs, xfs_block_bitmap) &&
		       (nextblock+=1) );

		if ( (nextblock + dirblkfsbs) > dblocks )
			return -1;

		SET_BITS ( nextblock, dirblkfsbs, xfs_block_bitmap );

		if ( (frags[nfrag].fr_start + frags[nfrag].fr_length)==nextblock && !countu )
			frags[nfrag].fr_length += dirblkfsbs;
		else
		{
			nfrag++;
			dir_frags->fr_nfrags++;
			frags[nfrag].fr_start = nextblock;
			frags[nfrag].fr_length = dirblkfsbs;
		}
	}

#undef TEST_BITS
#undef SET_BITS

	return 0;
}

inline int add_dirblock ( struct any_file_frags *dir_frags, uint32_t countu )
{
	int dirblkfsbs = xfs_dirblocksize/xfs_blocksize;
	return add_blocks ( dir_frags, countu, dirblkfsbs );
}

inline int add_block ( struct any_file_frags *dir_frags )
{
	return add_blocks ( dir_frags, 0, 1 );
}

void fill_direntry(char *bbuf, void **pbp, 
		struct any_dirent* dirent, uint64_t ino)
{
	void * bp = *pbp;

	void *bbp = bp;

	uint64_t *inumber = (uint64_t*) bp;
	bp = (void*) (inumber+1);

	uint8_t *namelen = (uint8_t*) bp;
	bp = (void*) (namelen+1);

	char *name = (char*) bp;
	int namefieldsize = (11 + strlen(dirent->d_name) +7)/8*8 - 11;
	bp = (void*) (name + namefieldsize);

	uint16_t *tag = (uint16_t*) bp;
	bp = (void*) (tag+1);

	ASSERT( ( (char*)bp - bbuf ) <= xfs_dirblocksize );
	ASSERT( (( (char*)bp - bbuf )%8) == 0 );

	INT_SET(*inumber, ARCH_CONVERT, ino);
	INT_SET(*namelen, ARCH_CONVERT, strlen(dirent->d_name));
	memcpy(name, dirent->d_name, strlen(dirent->d_name));
	INT_SET(*tag, ARCH_CONVERT, (char*)bbp - bbuf );

	*pbp = bp;
}

void fill_unused(char *bbuf, void **pbp, uint32_t unused_len)
{
	//uint32_t blocksize = xfs_blocksize;
	void * bp = *pbp;

	void *bbp = bp;

	uint16_t *freetag = (uint16_t*) bp;
	bp = (void*) (freetag+1);

	uint16_t *length = (uint16_t*) bp;
	bp = (void*) (length+1);

	INT_SET(*freetag, ARCH_CONVERT, 0xFFFF);
	INT_SET(*length, ARCH_CONVERT, unused_len*8);

	bp = (char*) bp + (unused_len*8-6);

	uint16_t *tag = (uint16_t*) bp;
	bp = (void*) (tag+1);

	//ASSERT( ( (char*)bp - bbuf ) == blocksize );
	ASSERT( (( (char*)bp - bbuf )%8) == 0 );

	INT_SET(*tag, ARCH_CONVERT, (char*)bbp - bbuf );

	*pbp = bp;
}

void calc_links_at_levels( int **p_links_at_level, int ***p_nlinks, int *p_height,
	       	uint32_t keys, int maxkeys_at_block )
{
	int *links_at_level;
	int **nlinks;

	if (!keys)
	{
		(*p_height) = 0;
		return;
	}

	int height = ceilf( logf(keys)/logf(maxkeys_at_block) );
	links_at_level = (int*) malloc(sizeof(int)*(height+1));
	nlinks = (int**) malloc(sizeof(int*)*(height+1));

	int i;
	for (i=height; i>=0; i--)
	{
		links_at_level[i] = (i==height)?keys:
			(links_at_level[i+1] + maxkeys_at_block - 1)/maxkeys_at_block;
		if (!i) nlinks[i] = NULL;
		else
		{
			int blocks_at_level = (links_at_level[i] + maxkeys_at_block - 1)/maxkeys_at_block;
			nlinks[i] = malloc(sizeof(int)*blocks_at_level);

			int n = links_at_level[i];
			int links_at_block_0 = floorf( (float)n/blocks_at_level );
			int links_at_block_1 = links_at_block_0+1;

			//int n0 = links_at_block_0*blocks_at_level;
			int n1 = links_at_block_1*blocks_at_level;

			int l0 = n1-n;
			/*int l1 = n-n0;*/

			int sum_links = 0;
			int j;
			for (j=0; j<blocks_at_level; j++)
			{
				if (j<l0)
					nlinks[i][j] = links_at_block_0;
				else
					nlinks[i][j] = links_at_block_1;

				ASSERT( nlinks[i][j]<=maxkeys_at_block );
				ASSERT( blocks_at_level==1 || nlinks[i][j]>=maxkeys_at_block/2 );
				sum_links += nlinks[i][j];
			}
			ASSERT(sum_links==n);
		}
	}

	(*p_links_at_level) = links_at_level;
	(*p_nlinks) = nlinks;
	(*p_height) = height;
}

int
main(
	int			argc,
	char			**argv)
{
	__uint64_t		agcount;
	xfs_agf_t		*agf;
	xfs_agi_t		*agi;
	xfs_agnumber_t		agno;
	__uint64_t		agsize;
	int			attrversion;
	xfs_btree_sblock_t	*block;
	int			blflag;
	int			blocklog;
	unsigned int		blocksize;
	int			bsflag;
	int			bsize;
	xfs_buf_t		*buf;
	int			c;
	int			daflag;
	int			dasize;
	xfs_drfsbno_t		dblocks;
	char			*dfile;
	int			dirblocklog;
	int			dirblocksize;
	int			dirversion;
	char			*dsize;
	int			dsu;
	int			dsw;
	int			dsunit;
	int			dswidth;
	int			extent_flagging;
	int			force_overwrite;
	struct fsxattr		fsx;
	int			iaflag;
	int			ilflag;
	int			imaxpct;
	int			imflag;
	int			inodelog;
	int			inopblock;
	int			ipflag;
	int			isflag;
	int			isize;
	char			*label = NULL;
	int			laflag;
	int			lalign;
	int			ldflag;
	int			liflag;
	xfs_agnumber_t		logagno;
	xfs_drfsbno_t		logblocks;
	char			*logfile;
	int			loginternal;
	char			*logsize;
	xfs_dfsbno_t		logstart;
	int			logversion;
	int			lvflag;
	int			lsflag;
	int			lsectorlog;
	int			lsectorsize;
	int			lslflag;
	int			lssflag;
	int			lsu;
	int			lsunit;
	int			max_tr_res;
	int			min_logblocks;
	xfs_mount_t		*mp;
	xfs_mount_t		mbuf;
	xfs_extlen_t		nbmblocks;
	int			nlflag;
	int			nodsflag;
	int			norsflag;
	//xfs_alloc_rec_t		*nrec;
	int			nsflag;
	int			nvflag;
	int			Nflag;
	char			*p;
	int			qflag;
	xfs_drfsbno_t		rtblocks;
	xfs_extlen_t		rtextblocks;
	xfs_drtbno_t		rtextents;
	char			*rtextsize;
	char			*rtfile;
	char			*rtsize;
	xfs_sb_t		*sbp;
	int			sectoralign;
	int			sectorlog;
	unsigned int		sectorsize;
	int			slflag;
	int			ssflag;
	__uint64_t		tmp_agsize;
	uuid_t			uuid;
	int			worst_freelist;
	libxfs_init_t		xi;
	int 			xlv_dsunit;
	int			xlv_dswidth;

	const char * 		inode_table;
	struct any_sb_info	*info;
	unsigned long bitmap_l;
	unsigned long *block_bitmap;
	uint64_t *inode_map;
	xfs_agi_t  *agi_array;
	uint32_t i, j;
	int retval = 0;

	progname = basename(argv[0]);
#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
#endif

	attrversion = 0;
	blflag = bsflag = slflag = ssflag = lslflag = lssflag = 0;
	blocklog = 0;
	blocksize = -1;
	sectorlog = lsectorlog = XFS_MIN_SECTORSIZE_LOG;
	sectorsize = lsectorsize = XFS_MIN_SECTORSIZE;
	agsize = daflag = dasize = dblocks = 0;
	ilflag = imflag = ipflag = isflag = 0;
	liflag = laflag = lsflag = ldflag = lvflag = 0;
	loginternal = 1;
	logversion = 1;
	logagno = logblocks = rtblocks = 0;
	Nflag = nlflag = nsflag = nvflag = 0;
	dirblocklog = dirblocksize = dirversion = 0;
	qflag = 0;
	imaxpct = inodelog = inopblock = isize = 0;
	iaflag = XFS_IFLAG_ALIGN;
	dfile = logfile = rtfile = NULL;
	dsize = logsize = rtsize = rtextsize = NULL;
	dsu = dsw = dsunit = dswidth = lalign = lsu = lsunit = 0;
	nodsflag = norsflag = 0;
	extent_flagging = 1;
	force_overwrite = 0;
	worst_freelist = 0;
	bzero(&fsx, sizeof(fsx));

	bzero(&xi, sizeof(xi));
	xi.isdirect = 0;
	xi.isreadonly = LIBXFS_EXCLUSIVELY;

	verbose = 0;
	
	while ((c = getopt(argc, argv, "b:d:i:l:L:n:Np:qr:s:CfvV")) != EOF) {
		switch (c) {
		case 'C':
		case 'f':
			force_overwrite = 1;
			break;
		case 'b':
			p = optarg;
			while (*p != '\0') {
				char	*value;

				switch (getsubopt(&p, (constpp)bopts, &value)) {
				case B_LOG:
					if (!value)
						reqval('b', bopts, B_LOG);
					if (blflag)
						respec('b', bopts, B_LOG);
					if (bsflag)
						conflict('b', bopts, B_SIZE,
							 B_LOG);
					blocklog = atoi(value);
					if (blocklog <= 0)
						illegal(value, "b log");
					blocksize = 1 << blocklog;
					blflag = 1;
					break;
				case B_SIZE:
					if (!value)
						reqval('b', bopts, B_SIZE);
					if (bsflag)
						respec('b', bopts, B_SIZE);
					if (blflag)
						conflict('b', bopts, B_LOG,
							 B_SIZE);
					blocksize = cvtnum(
						blocksize, sectorsize, value);
					if (blocksize <= 0 ||
					    !ispow2(blocksize))
						illegal(value, "b size");
					blocklog = libxfs_highbit32(blocksize);
					bsflag = 1;
					break;
				default:
					unknown('b', value);
				}
			}
			break;
		case 'd':
			p = optarg;
			while (*p != '\0') {
				char	*value;

				switch (getsubopt(&p, (constpp)dopts, &value)) {
				case D_AGCOUNT:
					if (!value)
						reqval('d', dopts, D_AGCOUNT);
					if (daflag)
						respec('d', dopts, D_AGCOUNT);
					agcount = (__uint64_t)
						strtoul(value, NULL, 10);
					if ((__int64_t)agcount <= 0)
						illegal(value, "d agcount");
					daflag = 1;
					break;
				case D_AGSIZE:
					if (!value)
						reqval('d', dopts, D_AGSIZE);
					if (dasize)
						respec('d', dopts, D_AGSIZE);
					agsize = cvtnum(
						blocksize, sectorsize, value);
					dasize = 1;
					break;
				case D_FILE:
					if (!value)
						value = "1";
					xi.disfile = atoi(value);
					if (xi.disfile < 0 || xi.disfile > 1)
						illegal(value, "d file");
					if (xi.disfile)
						xi.dcreat = 1;
					break;
				case D_NAME:
					if (!value)
						reqval('d', dopts, D_NAME);
					if (xi.dname)
						respec('d', dopts, D_NAME);
					xi.dname = value;
					break;
				case D_SIZE:
					if (!value)
						reqval('d', dopts, D_SIZE);
					if (dsize)
						respec('d', dopts, D_SIZE);
					dsize = value;
					break;
				case D_SUNIT:
					if (!value)
						reqval('d', dopts, D_SUNIT);
					if (dsunit)
						respec('d', dopts, D_SUNIT);
					if (nodsflag)
						conflict('d', dopts, D_NOALIGN,
							 D_SUNIT);
					if (!isdigits(value)) {
						fprintf(stderr,
	_("%s: Specify data sunit in 512-byte blocks, no unit suffix\n"),
							progname);
						exit(1);
					}
					dsunit = cvtnum(0, 0, value);
					break;
				case D_SWIDTH:
					if (!value)
						reqval('d', dopts, D_SWIDTH);
					if (dswidth)
						respec('d', dopts, D_SWIDTH);
					if (nodsflag)
						conflict('d', dopts, D_NOALIGN,
							 D_SWIDTH);
					if (!isdigits(value)) {
						fprintf(stderr,
	_("%s: Specify data swidth in 512-byte blocks, no unit suffix\n"),
							progname);
						exit(1);
					}
					dswidth = cvtnum(0, 0, value);
					break;
				case D_SU:
					if (!value)
						reqval('d', dopts, D_SU);
					if (dsu)
						respec('d', dopts, D_SU);
					if (nodsflag)
						conflict('d', dopts, D_NOALIGN,
							 D_SU);
					dsu = cvtnum(
						blocksize, sectorsize, value);
					break;
				case D_SW:
					if (!value)
						reqval('d', dopts, D_SW);
					if (dsw)
						respec('d', dopts, D_SW);
					if (nodsflag)
						conflict('d', dopts, D_NOALIGN,
							 D_SW);
					if (!isdigits(value)) {
						fprintf(stderr,
		_("%s: Specify data sw as multiple of su, no unit suffix\n"),
							progname);
						exit(1);
					}
					dsw = cvtnum(0, 0, value);
					break;
				case D_NOALIGN:
					if (dsu)
						conflict('d', dopts, D_SU,
							 D_NOALIGN);
					if (dsunit)
						conflict('d', dopts, D_SUNIT,
							 D_NOALIGN);
					if (dsw)
						conflict('d', dopts, D_SW,
							 D_NOALIGN);
					if (dswidth)
						conflict('d', dopts, D_SWIDTH,
							 D_NOALIGN);
					nodsflag = 1;
					break;
				case D_UNWRITTEN:
					if (!value)
						reqval('d', dopts, D_UNWRITTEN);
					c = atoi(value);
					if (c < 0 || c > 1)
						illegal(value, "d unwritten");
					extent_flagging = c;
					break;
				case D_SECTLOG:
					if (!value)
						reqval('d', dopts, D_SECTLOG);
					if (slflag)
						respec('d', dopts, D_SECTLOG);
					if (ssflag)
						conflict('d', dopts, D_SECTSIZE,
							 D_SECTLOG);
					sectorlog = atoi(value);
					if (sectorlog <= 0)
						illegal(value, "d sectlog");
					sectorsize = 1 << sectorlog;
					slflag = 1;
					break;
				case D_SECTSIZE:
					if (!value)
						reqval('d', dopts, D_SECTSIZE);
					if (ssflag)
						respec('d', dopts, D_SECTSIZE);
					if (slflag)
						conflict('d', dopts, D_SECTLOG,
							 D_SECTSIZE);
					sectorsize = cvtnum(
						blocksize, sectorsize, value);
					if (sectorsize <= 0 ||
					    !ispow2(sectorsize))
						illegal(value, "d sectsize");
					sectorlog =
						libxfs_highbit32(sectorsize);
					ssflag = 1;
					break;
				case D_RTINHERIT:
					fsx.fsx_xflags |= \
						XFS_DIFLAG_RTINHERIT;
					break;
				case D_PROJINHERIT:
					if (!value)
						reqval('d', dopts, D_PROJINHERIT);
					fsx.fsx_projid = atoi(value);
					fsx.fsx_xflags |= \
						XFS_DIFLAG_PROJINHERIT;
					break;
				case D_EXTSZINHERIT:
					if (!value)
						reqval('d', dopts, D_EXTSZINHERIT);
					fsx.fsx_extsize = atoi(value);
					fsx.fsx_xflags |= \
						XFS_DIFLAG_EXTSZINHERIT;
					break;
				default:
					unknown('d', value);
				}
			}
			break;
		case 'i':
			p = optarg;
			while (*p != '\0') {
				char	*value;

				switch (getsubopt(&p, (constpp)iopts, &value)) {
				case I_ALIGN:
					if (!value)
						value = "1";
					iaflag = atoi(value);
					if (iaflag < 0 || iaflag > 1)
						illegal(value, "i align");
					break;
				case I_LOG:
					if (!value)
						reqval('i', iopts, I_LOG);
					if (ilflag)
						respec('i', iopts, I_LOG);
					if (ipflag)
						conflict('i', iopts, I_PERBLOCK,
							 I_LOG);
					if (isflag)
						conflict('i', iopts, I_SIZE,
							 I_LOG);
					inodelog = atoi(value);
					if (inodelog <= 0)
						illegal(value, "i log");
					isize = 1 << inodelog;
					ilflag = 1;
					break;
				case I_MAXPCT:
					if (!value)
						reqval('i', iopts, I_MAXPCT);
					if (imflag)
						respec('i', iopts, I_MAXPCT);
					imaxpct = atoi(value);
					if (imaxpct < 0 || imaxpct > 100)
						illegal(value, "i maxpct");
					imflag = 1;
					break;
				case I_PERBLOCK:
					if (!value)
						reqval('i', iopts, I_PERBLOCK);
					if (ilflag)
						conflict('i', iopts, I_LOG,
							 I_PERBLOCK);
					if (ipflag)
						respec('i', iopts, I_PERBLOCK);
					if (isflag)
						conflict('i', iopts, I_SIZE,
							 I_PERBLOCK);
					inopblock = atoi(value);
					if (inopblock <
						XFS_MIN_INODE_PERBLOCK ||
					    !ispow2(inopblock))
						illegal(value, "i perblock");
					ipflag = 1;
					break;
				case I_SIZE:
					if (!value)
						reqval('i', iopts, I_SIZE);
					if (ilflag)
						conflict('i', iopts, I_LOG,
							 I_SIZE);
					if (ipflag)
						conflict('i', iopts, I_PERBLOCK,
							 I_SIZE);
					if (isflag)
						respec('i', iopts, I_SIZE);
					isize = cvtnum(0, 0, value);
					if (isize <= 0 || !ispow2(isize))
						illegal(value, "i size");
					inodelog = libxfs_highbit32(isize);
					isflag = 1;
					break;
				case I_ATTR:
					if (!value)
						reqval('i', iopts, I_ATTR);
					c = atoi(value);
					if (c < 0 || c > 2)
						illegal(value, "i attr");
					attrversion = c;
					break;
				default:
					unknown('i', value);
				}
			}
			break;
		case 'l':
			p = optarg;
			while (*p != '\0') {
				char	*value;

				switch (getsubopt(&p, (constpp)lopts, &value)) {
				case L_AGNUM:
					if (laflag)
						respec('l', lopts, L_AGNUM);

					if (ldflag) 
						conflict('l', lopts, L_AGNUM, L_DEV);

					logagno = atoi(value);
					laflag = 1;
					break;
				case L_FILE:
					if (!value)
						value = "1";
					if (loginternal)
						conflict('l', lopts, L_INTERNAL,
							 L_FILE);
					xi.lisfile = atoi(value);
					if (xi.lisfile < 0 || xi.lisfile > 1)
						illegal(value, "l file");
					if (xi.lisfile)
						xi.lcreat = 1;
					break;
				case L_INTERNAL:
					if (!value)
						value = "1";

					if (ldflag)
						conflict('l', lopts, L_INTERNAL, L_DEV);
					if (xi.lisfile)
						conflict('l', lopts, L_FILE,
							 L_INTERNAL);
					if (liflag)
						respec('l', lopts, L_INTERNAL);
					loginternal = atoi(value);
					if (loginternal < 0 || loginternal > 1)
						illegal(value, "l internal");
					liflag = 1;
					break;
				case L_SU:
					if (!value)
						reqval('l', lopts, L_SU);
					if (lsu)
						respec('l', lopts, L_SU);
					lsu = cvtnum(
						blocksize, sectorsize, value);
					break;
				case L_SUNIT:
					if (!value)
						reqval('l', lopts, L_SUNIT);
					if (lsunit)
						respec('l', lopts, L_SUNIT);
					if (!isdigits(value)) {
						fprintf(stderr,
		_("Specify log sunit in 512-byte blocks, no size suffix\n"));
						usage();
					}
					lsunit = cvtnum(0, 0, value);
					break;
				case L_NAME:
					if (!value)
						reqval('l', lopts, L_NAME);
					if (xi.logname)
						respec('l', lopts, L_NAME);
					ldflag = 1;
					loginternal = 0;
					logfile = value;
					xi.logname = value;
					break;
				case L_VERSION:
					if (!value)
						reqval('l', lopts, L_VERSION);
					if (lvflag)
						respec('l', lopts, L_VERSION);
					logversion = atoi(value);
					if (logversion < 1 || logversion > 2)
						illegal(value, "l version");
					lvflag = 1;
					break;
				case L_SIZE:
					if (!value)
						reqval('l', lopts, L_SIZE);
					if (logsize)
						respec('l', lopts, L_SIZE);
					logsize = value;
					lsflag = 1;
					break;
				case L_SECTLOG:
					if (!value)
						reqval('l', lopts, L_SECTLOG);
					if (lslflag)
						respec('l', lopts, L_SECTLOG);
					if (lssflag)
						conflict('l', lopts, L_SECTSIZE,
							 L_SECTLOG);
					lsectorlog = atoi(value);
					if (lsectorlog <= 0)
						illegal(value, "l sectlog");
					lsectorsize = 1 << lsectorlog;
					lslflag = 1;
					break;
				case L_SECTSIZE:
					if (!value)
						reqval('l', lopts, L_SECTSIZE);
					if (lssflag)
						respec('l', lopts, L_SECTSIZE);
					if (lslflag)
						conflict('l', lopts, L_SECTLOG,
							 L_SECTSIZE);
					lsectorsize = cvtnum(
						blocksize, sectorsize, value);
					if (lsectorsize <= 0 ||
					    !ispow2(lsectorsize))
						illegal(value, "l sectsize");
					lsectorlog =
						libxfs_highbit32(lsectorsize);
					lssflag = 1;
					break;
				default:
					unknown('l', value);
				}
			}
			break;
		case 'L':
			if (strlen(optarg) > sizeof(sbp->sb_fname))
				illegal(optarg, "L");
			label = optarg;
			break;
		case 'n':
			p = optarg;
			while (*p != '\0') {
				char	*value;

				switch (getsubopt(&p, (constpp)nopts, &value)) {
				case N_LOG:
					if (!value)
						reqval('n', nopts, N_LOG);
					if (nlflag)
						respec('n', nopts, N_LOG);
					if (nsflag)
						conflict('n', nopts, N_SIZE,
							 N_LOG);
					dirblocklog = atoi(value);
					if (dirblocklog <= 0)
						illegal(value, "n log");
					dirblocksize = 1 << dirblocklog;
					nlflag = 1;
					break;
				case N_SIZE:
					if (!value)
						reqval('n', nopts, N_SIZE);
					if (nsflag)
						respec('n', nopts, N_SIZE);
					if (nlflag)
						conflict('n', nopts, N_LOG,
							 N_SIZE);
					dirblocksize = cvtnum(
						blocksize, sectorsize, value);
					if (dirblocksize <= 0 ||
					    !ispow2(dirblocksize))
						illegal(value, "n size");
					dirblocklog =
						libxfs_highbit32(dirblocksize);
					nsflag = 1;
					break;
				case N_VERSION:
					if (!value)
						reqval('n', nopts, N_VERSION);
					if (nvflag)
						respec('n', nopts, N_VERSION);
					dirversion = atoi(value);
					if (dirversion < 1 || dirversion > 2)
						illegal(value, "n version");
					nvflag = 1;
					break;
				default:
					unknown('n', value);
				}
			}
			break;
		case 'N':
			Nflag = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'r':
			p = optarg;
			while (*p != '\0') {
				char	*value;

				switch (getsubopt(&p, (constpp)ropts, &value)) {
				case R_EXTSIZE:
					if (!value)
						reqval('r', ropts, R_EXTSIZE);
					if (rtextsize)
						respec('r', ropts, R_EXTSIZE);
					rtextsize = value;
					break;
				case R_FILE:
					if (!value)
						value = "1";
					xi.risfile = atoi(value);
					if (xi.risfile < 0 || xi.risfile > 1)
						illegal(value, "r file");
					if (xi.risfile)
						xi.rcreat = 1;
					break;
				case R_NAME:
				case R_DEV:
					if (!value)
						reqval('r', ropts, R_NAME);
					if (xi.rtname)
						respec('r', ropts, R_NAME);
					xi.rtname = value;
					break;
				case R_SIZE:
					if (!value)
						reqval('r', ropts, R_SIZE);
					if (rtsize)
						respec('r', ropts, R_SIZE);
					rtsize = value;
					break;
				case R_NOALIGN:
					norsflag = 1;
					break;
				default:
					unknown('r', value);
				}
			}
			break;
		case 's':
			p = optarg;
			while (*p != '\0') {
				char	*value;

				switch (getsubopt(&p, (constpp)sopts, &value)) {
				case S_LOG:
				case S_SECTLOG:
					if (!value)
						reqval('s', sopts, S_SECTLOG);
					if (slflag || lslflag)
						respec('s', sopts, S_SECTLOG);
					if (ssflag || lssflag)
						conflict('s', sopts, S_SECTSIZE,
							 S_SECTLOG);
					sectorlog = atoi(value);
					if (sectorlog <= 0)
						illegal(value, "s sectlog");
					lsectorlog = sectorlog;
					sectorsize = 1 << sectorlog;
					lsectorsize = sectorsize;
					lslflag = slflag = 1;
					break;
				case S_SIZE:
				case S_SECTSIZE:
					if (!value)
						reqval('s', sopts, S_SECTSIZE);
					if (ssflag || lssflag)
						respec('s', sopts, S_SECTSIZE);
					if (slflag || lslflag)
						conflict('s', sopts, S_SECTLOG,
							 S_SECTSIZE);
					sectorsize = cvtnum(
						blocksize, sectorsize, value);
					if (sectorsize <= 0 ||
					    !ispow2(sectorsize))
						illegal(value, "s sectsize");
					lsectorsize = sectorsize;
					sectorlog =
						libxfs_highbit32(sectorsize);
					lsectorlog = sectorlog;
					lssflag = ssflag = 1;
					break;
				default:
					unknown('s', value);
				}
			}
			break;
		case 'v':
			verbose++;
			break;
		case 'V':
			printf(_("%s %s (%s)\n"), progname, 
					ANYFSTOOLS_VERSION,
					ANYFSTOOLS_DATE);
			exit(0);
		case '?':
			unknown(optopt, "");
		}
	}
	if (argc - optind > 2) {
		fprintf(stderr, _("extra arguments\n"));
		usage();
	} else if (argc - optind < 1) {
		fprintf(stderr, _("not enough arguments\n"));
		usage();
	} else if (argc - optind == 2) {
		dfile = xi.volname = argv[optind+1];
		if (xi.dname) {
			fprintf(stderr,
				_("cannot specify both %s and -d name=%s\n"),
				xi.volname, xi.dname);
			usage();
		}
	} else
		dfile = xi.dname;
		
	inode_table = argv[optind];
	noaction = Nflag;
	if (noaction) xi.isreadonly = LIBXFS_ISREADONLY;

	retval = read_it (&info, (char*) inode_table);
	if (retval)
	{
		fprintf(stderr,
_("Error while reading inode table: %s\n"),
			(errno)?strerror(errno):_("bad format"));
		exit(-retval);
	}

	if ( blocksize!=-1 && blocksize!=info->si_blocksize )
	{
		fprintf(stderr,
_("You try to use blocksize %d, although inode table given for %lu blocksize\n"
  "But changing of filesystem blocksize is not my task. Use 'reblock' for it.\n"),
			blocksize, info->si_blocksize);
		exit(1);
	}

	blocksize = info->si_blocksize;
	blocklog = libxfs_highbit32(blocksize);
	bsflag = 1;

	/*
	 * Blocksize and sectorsize first, other things depend on them
	 * For RAID4/5/6 we want to align sector size and block size,
	 * so we need to start with the device geometry extraction too.
	 */
	if (!blflag && !bsflag) {
		blocklog = XFS_DFL_BLOCKSIZE_LOG;
		blocksize = 1 << XFS_DFL_BLOCKSIZE_LOG;
	}
	if (blocksize < XFS_MIN_BLOCKSIZE || blocksize > XFS_MAX_BLOCKSIZE) {
		fprintf(stderr, _("illegal block size %d\n"), blocksize);
		usage();
	}

	sectoralign = 0;
	xlv_dsunit = xlv_dswidth = 0;
	if (!nodsflag && !xi.disfile)
		get_subvol_stripe_wrapper(dfile, SVTYPE_DATA,
				&xlv_dsunit, &xlv_dswidth, &sectoralign);
	if (sectoralign) {
		sectorsize = blocksize;
		sectorlog = libxfs_highbit32(sectorsize);
		if (loginternal) {
			lsectorsize = sectorsize;
			lsectorlog = sectorlog;
		}
	}
	if (sectorsize < XFS_MIN_SECTORSIZE ||
	    sectorsize > XFS_MAX_SECTORSIZE || sectorsize > blocksize) {
		fprintf(stderr, _("illegal sector size %d\n"), sectorsize);
		usage();
	}
	if (lsectorsize < XFS_MIN_SECTORSIZE ||
	    lsectorsize > XFS_MAX_SECTORSIZE || lsectorsize > blocksize) {
		fprintf(stderr, _("illegal log sector size %d\n"), lsectorsize);
		usage();
	} else if (lsectorsize > XFS_MIN_SECTORSIZE && !lsu && !lsunit) {
		lsu = blocksize;
		logversion = 2;
	}

	if (!nvflag)
		dirversion = (nsflag || nlflag) ? 2 : XFS_DFL_DIR_VERSION;
	switch (dirversion) {
	case 1:
		if ((nsflag || nlflag) && dirblocklog != blocklog) {
			fprintf(stderr, _("illegal directory block size %d\n"),
				dirblocksize);
			usage();
		}
		fprintf(stderr, _("Sorry, build_xfs doesn't support first version of naming\n"));
		exit(1);
		break;
	case 2:
		if (nsflag || nlflag) {
			if (dirblocksize < blocksize ||
			    dirblocksize > XFS_MAX_BLOCKSIZE) {
				fprintf(stderr,
					_("illegal directory block size %d\n"),
					dirblocksize);
				usage();
			}
		} else {
			if (blocksize < (1 << XFS_MIN_REC_DIRSIZE))
				dirblocklog = XFS_MIN_REC_DIRSIZE;
			else
				dirblocklog = blocklog;
			dirblocksize = 1 << dirblocklog;
		}
		break;
	}

	if (daflag && dasize) {
		fprintf(stderr,
	_("both -d agcount= and agsize= specified, use one or the other\n"));
		usage();
	}

	if (xi.disfile && (!dsize || !xi.dname)) {
		fprintf(stderr,
	_("if -d file then -d name and -d size are required\n"));
		usage();
	}
	if (dsize) {
		__uint64_t dbytes;

		dbytes = cvtnum(blocksize, sectorsize, dsize);
		if (dbytes % XFS_MIN_BLOCKSIZE) {
			fprintf(stderr,
			_("illegal data length %lld, not a multiple of %d\n"),
				(long long)dbytes, XFS_MIN_BLOCKSIZE);
			usage();
		}
		dblocks = (xfs_drfsbno_t)(dbytes >> blocklog);
		if (dbytes % blocksize)
			fprintf(stderr, _("warning: "
	"data length %lld not a multiple of %d, truncated to %lld\n"),
				(long long)dbytes, blocksize,
				(long long)(dblocks << blocklog));
	}
	if (ipflag) {
		inodelog = blocklog - libxfs_highbit32(inopblock);
		isize = 1 << inodelog;
	} else if (!ilflag && !isflag) {
		inodelog = XFS_DINODE_DFL_LOG;
		isize = 1 << inodelog;
	}
	if (xi.lisfile && (!logsize || !xi.logname)) {
		fprintf(stderr,
		_("if -l file then -l name and -l size are required\n"));
		usage();
	}
	if (logsize) {
		__uint64_t logbytes;

		logbytes = cvtnum(blocksize, sectorsize, logsize);
		if (logbytes % XFS_MIN_BLOCKSIZE) {
			fprintf(stderr,
			_("illegal log length %lld, not a multiple of %d\n"),
				(long long)logbytes, XFS_MIN_BLOCKSIZE);
			usage();
		}
		logblocks = (xfs_drfsbno_t)(logbytes >> blocklog);
		if (logbytes % blocksize)
			fprintf(stderr,
	_("warning: log length %lld not a multiple of %d, truncated to %lld\n"),
				(long long)logbytes, blocksize,
				(long long)(logblocks << blocklog));
	}
	if (xi.risfile && (!rtsize || !xi.rtname)) {
		fprintf(stderr,
		_("if -r file then -r name and -r size are required\n"));
		usage();
	}
	if (rtsize) {
		__uint64_t rtbytes;

		rtbytes = cvtnum(blocksize, sectorsize, rtsize);
		if (rtbytes % XFS_MIN_BLOCKSIZE) {
			fprintf(stderr,
			_("illegal rt length %lld, not a multiple of %d\n"),
				(long long)rtbytes, XFS_MIN_BLOCKSIZE);
			usage();
		}
		rtblocks = (xfs_drfsbno_t)(rtbytes >> blocklog);
		if (rtbytes % blocksize)
			fprintf(stderr,
	_("warning: rt length %lld not a multiple of %d, truncated to %lld\n"),
				(long long)rtbytes, blocksize,
				(long long)(rtblocks << blocklog));
	}
	/*
	 * If specified, check rt extent size against its constraints.
	 */
	if (rtextsize) {
		__uint64_t rtextbytes;

		rtextbytes = cvtnum(blocksize, sectorsize, rtextsize);
		if (rtextbytes % blocksize) {
			fprintf(stderr,
		_("illegal rt extent size %lld, not a multiple of %d\n"),
				(long long)rtextbytes, blocksize);
			usage();
		}
		if (rtextbytes > XFS_MAX_RTEXTSIZE) {
			fprintf(stderr,
				_("rt extent size %s too large, maximum %d\n"),
				rtextsize, XFS_MAX_RTEXTSIZE);
			usage();
		}
		if (rtextbytes < XFS_MIN_RTEXTSIZE) {
			fprintf(stderr,
				_("rt extent size %s too small, minimum %d\n"),
				rtextsize, XFS_MIN_RTEXTSIZE);
			usage();
		}
		rtextblocks = (xfs_extlen_t)(rtextbytes >> blocklog);
	} else {
		/*
		 * If realtime extsize has not been specified by the user,
		 * and the underlying volume is striped, then set rtextblocks
		 * to the stripe width.
		 */
		int		dummy1, rswidth;
		__uint64_t	rtextbytes;

		dummy1 = rswidth = 0;

		if (!norsflag && !xi.risfile && !(!rtsize && xi.disfile))
			get_subvol_stripe_wrapper(dfile, SVTYPE_RT, &dummy1,
						  &rswidth, &dummy1);

		/* check that rswidth is a multiple of fs blocksize */
		if (!norsflag && rswidth && !(BBTOB(rswidth) % blocksize)) {
			rswidth = DTOBT(rswidth);
			rtextbytes = rswidth << blocklog;
			if (XFS_MIN_RTEXTSIZE <= rtextbytes &&
			    (rtextbytes <= XFS_MAX_RTEXTSIZE)) {
				rtextblocks = rswidth;
			} else {
				rtextblocks = XFS_DFL_RTEXTSIZE >> blocklog;
			}
		} else
			rtextblocks = XFS_DFL_RTEXTSIZE >> blocklog;
	}

	/*
	 * Check some argument sizes against mins, maxes.
	 */
	if (isize > blocksize / XFS_MIN_INODE_PERBLOCK ||
	    isize < XFS_DINODE_MIN_SIZE ||
	    isize > XFS_DINODE_MAX_SIZE) {
		int	maxsz;

		fprintf(stderr, _("illegal inode size %d\n"), isize);
		maxsz = MIN(blocksize / XFS_MIN_INODE_PERBLOCK,
			    XFS_DINODE_MAX_SIZE);
		if (XFS_DINODE_MIN_SIZE == maxsz)
			fprintf(stderr,
			_("allowable inode size with %d byte blocks is %d\n"),
				blocksize, XFS_DINODE_MIN_SIZE);
		else
			fprintf(stderr,
	_("allowable inode size with %d byte blocks is between %d and %d\n"),
				blocksize, XFS_DINODE_MIN_SIZE, maxsz);
		usage();
	}

	/* if lsu or lsunit was specified, automatically use v2 logs */
	if ((lsu || lsunit) && logversion == 1) {
		fprintf(stderr,
			_("log stripe unit specified, using v2 logs\n"));
		logversion = 2;
	}

	calc_stripe_factors(dsu, dsw, sectorsize, lsu, lsectorsize,
				&dsunit, &dswidth, &lsunit);

	if (slflag || ssflag)
		xi.setblksize = sectorsize;
	else
		xi.setblksize = 1;

	/*
	 * Initialize.  This will open the log and rt devices as well.
	 */
	if (!libxfs_init(&xi))
		usage();
	if (!xi.ddev) {
		fprintf(stderr, _("no device name given in argument list\n"));
		usage();
	}

	/*
	 * Ok, Linux only has a 1024-byte resolution on device _size_,
	 * and the sizes below are in basic 512-byte blocks,
	 * so if we have (size % 2), on any partition, we can't get
	 * to the last 512 bytes.  Just chop it down by a block.
	 */

	xi.dsize -= (xi.dsize % 2);
	xi.rtsize -= (xi.rtsize % 2);
	xi.logBBsize -= (xi.logBBsize % 2);

	if (!force_overwrite) {
		if (check_overwrite(dfile) ||
		    check_overwrite(logfile) ||
		    check_overwrite(xi.rtname)) {
			fprintf(stderr,
			_("%s: Use the -f option to force overwrite.\n"),
				progname);
			exit(1);
		}
	}

	if (!liflag && !ldflag)
		loginternal = xi.logdev == 0;
	if (xi.logname)
		logfile = xi.logname;
	else if (loginternal)
		logfile = _("internal log");
	else if (xi.volname && xi.logdev)
		logfile = _("volume log");
	else if (!ldflag) {
		fprintf(stderr, _("no log subvolume or internal log\n"));
		usage();
	}
	if (xi.rtname)
		rtfile = xi.rtname;
	else
	if (xi.volname && xi.rtdev)
		rtfile = _("volume rt");
	else if (!xi.rtdev)
		rtfile = _("none");
	if (dsize && xi.dsize > 0 && dblocks > DTOBT(xi.dsize)) {
		fprintf(stderr,
			_("size %s specified for data subvolume is too large, "
			"maximum is %lld blocks\n"),
			dsize, (long long)DTOBT(xi.dsize));
		usage();
	} else if (!dsize && xi.dsize > 0)
		dblocks = DTOBT(xi.dsize);
	else if (!dsize) {
		fprintf(stderr, _("can't get size of data subvolume\n"));
		usage();
	} 
	if (dblocks < XFS_MIN_DATA_BLOCKS) {
		fprintf(stderr,
	_("size %lld of data subvolume is too small, minimum %d blocks\n"),
			(long long)dblocks, XFS_MIN_DATA_BLOCKS);
		usage();
	}

	if (loginternal && xi.logdev) {
		fprintf(stderr,
			_("can't have both external and internal logs\n"));
		usage();
	} else if (loginternal && sectorsize != lsectorsize) {
		fprintf(stderr,
	_("data and log sector sizes must be equal for internal logs\n"));
		usage();
	}

	if (xi.dbsize > sectorsize) {
		fprintf(stderr, _(
"Warning: the data subvolume sector size %u is less than the sector size \n\
reported by the device (%u).\n"),
			sectorsize, xi.dbsize);
	}
	if (!loginternal && xi.lbsize > lsectorsize) {
		fprintf(stderr, _(
"Warning: the log subvolume sector size %u is less than the sector size\n\
reported by the device (%u).\n"),
			lsectorsize, xi.lbsize);
	}
	if (rtsize && xi.rtsize > 0 && xi.rtbsize > sectorsize) {
		fprintf(stderr, _(
"Warning: the realtime subvolume sector size %u is less than the sector size\n\
reported by the device (%u).\n"),
			sectorsize, xi.rtbsize);
	}

	max_tr_res = max_trans_res(dirversion,
				   sectorlog, blocklog, inodelog, dirblocklog);
	ASSERT(max_tr_res);
	min_logblocks = max_tr_res * XFS_MIN_LOG_FACTOR;
	min_logblocks = MAX(XFS_MIN_LOG_BLOCKS, min_logblocks);
	if (!logsize && dblocks >= (1024*1024*1024) >> blocklog)
		min_logblocks = MAX(min_logblocks, (10*1024*1024)>>blocklog);
	if (logsize && xi.logBBsize > 0 && logblocks > DTOBT(xi.logBBsize)) {
		fprintf(stderr,
_("size %s specified for log subvolume is too large, maximum is %lld blocks\n"),
			logsize, (long long)DTOBT(xi.logBBsize));
		usage();
	} else if (!logsize && xi.logBBsize > 0) {
		logblocks = DTOBT(xi.logBBsize);
	} else if (logsize && !xi.logdev && !loginternal) {
		fprintf(stderr,
			_("size specified for non-existent log subvolume\n"));
		usage();
	} else if (loginternal && logsize && logblocks >= dblocks) {
		fprintf(stderr, _("size %lld too large for internal log\n"),
			(long long)logblocks);
		usage();
	} else if (!loginternal && !xi.logdev) {
		logblocks = 0;
	} else if (loginternal && !logsize) {
		/*
		 * logblocks grows from min_logblocks to XFS_MAX_LOG_BLOCKS
		 * at 128GB
		 *
		 * 2048 = 128GB / MAX_LOG_BYTES
		 */
		logblocks = (dblocks << blocklog) / 2048;
		logblocks = logblocks >> blocklog;
		logblocks = MAX(min_logblocks, logblocks);
		logblocks = MAX(logblocks,
				MAX(XFS_DFL_LOG_SIZE,
					max_tr_res * XFS_DFL_LOG_FACTOR));
		logblocks = MIN(logblocks, XFS_MAX_LOG_BLOCKS); 
		if ((logblocks << blocklog) > XFS_MAX_LOG_BYTES) {
			logblocks = XFS_MAX_LOG_BYTES >> blocklog;
		}
	}
	validate_log_size(logblocks, blocklog, min_logblocks);

	if (rtsize && xi.rtsize > 0 && rtblocks > DTOBT(xi.rtsize)) {
		fprintf(stderr,
			_("size %s specified for rt subvolume is too large, "
			"maximum is %lld blocks\n"),
			rtsize, (long long)DTOBT(xi.rtsize));
		usage();
	} else if (!rtsize && xi.rtsize > 0)
		rtblocks = DTOBT(xi.rtsize);
	else if (rtsize && !xi.rtdev) {
		fprintf(stderr,
			_("size specified for non-existent rt subvolume\n"));
		usage();
	}
	if (xi.rtdev) {
		rtextents = rtblocks / rtextblocks;
		nbmblocks = (xfs_extlen_t)howmany(rtextents, NBBY * blocksize);
	} else {
		rtextents = rtblocks = 0;
		nbmblocks = 0;
	}

	if (dasize) {		/* User-specified AG size */
		/*
		 * Check specified agsize is a multiple of blocksize.
		 */
		if (agsize % blocksize) {
			fprintf(stderr,
		_("agsize (%lld) not a multiple of fs blk size (%d)\n"),
				(long long)agsize, blocksize);
			usage();
		}
		agsize /= blocksize;
		agcount = dblocks / agsize + (dblocks % agsize != 0);

	} else if (daflag)	/* User-specified AG size */
		agsize = dblocks / agcount + (dblocks % agcount != 0);
	else
		calc_default_ag_geometry(blocklog, dblocks, &agsize, &agcount);

	/*
	 * If the last AG is too small, reduce the filesystem size
	 * and drop the blocks.
	 */
	if ( dblocks % agsize != 0 &&
	     (dblocks % agsize < XFS_AG_MIN_BLOCKS(blocklog))) {
		dblocks = (xfs_drfsbno_t)((agcount - 1) * agsize);
		agcount--;
		ASSERT(agcount != 0);
	}

	validate_ag_geometry(blocklog, dblocks, agsize, agcount);

	if (!nodsflag && dsunit) {
		if (xlv_dsunit && xlv_dsunit != dsunit) {
			fprintf(stderr,
				_("%s: Specified data stripe unit %d is not "
				"the same as the volume stripe unit %d\n"),
				progname, dsunit, xlv_dsunit);
		}
		if (xlv_dswidth && xlv_dswidth != dswidth) {
			fprintf(stderr,
				_("%s: Specified data stripe width %d is not "
				"the same as the volume stripe width %d\n"),
				progname, dswidth, xlv_dswidth);
		}
	} else {
		dsunit = xlv_dsunit;
		dswidth = xlv_dswidth;
		nodsflag = 1;
	}

	/*
	 * If dsunit is a multiple of fs blocksize, then check that is a
	 * multiple of the agsize too
	 */
	if (dsunit && !(BBTOB(dsunit) % blocksize) && 
	    dswidth && !(BBTOB(dswidth) % blocksize)) {

		/* convert from 512 byte blocks to fs blocksize */
		dsunit = DTOBT(dsunit);
		dswidth = DTOBT(dswidth);

		/* 
		 * agsize is not a multiple of dsunit
		 */
		if ((agsize % dsunit) != 0) {
			/*
			 * Round up to stripe unit boundary. Also make sure 
			 * that agsize is still larger than 
			 * XFS_AG_MIN_BLOCKS(blocklog)
		 	 */
			tmp_agsize = ((agsize + (dsunit - 1))/ dsunit) * dsunit;
			/*
			 * Round down to stripe unit boundary if rounding up
			 * created an AG size that is larger than the AG max.
			 */
			if (tmp_agsize > XFS_AG_MAX_BLOCKS(blocklog))
				tmp_agsize = ((agsize) / dsunit) * dsunit;
			if ((tmp_agsize >= XFS_AG_MIN_BLOCKS(blocklog)) &&
			    (tmp_agsize <= XFS_AG_MAX_BLOCKS(blocklog)) &&
			    !daflag) {
				agsize = tmp_agsize;
				agcount = dblocks/agsize + 
						(dblocks % agsize != 0);
				if (dasize || daflag)
					fprintf(stderr,
				_("agsize rounded to %lld, swidth = %d\n"),
						(long long)agsize, dswidth);
			} else {
				if (nodsflag) {
					dsunit = dswidth = 0;
				} else { 
					fprintf(stderr,
_("Allocation group size (%lld) is not a multiple of the stripe unit (%d)\n"),
						(long long)agsize, dsunit);
					exit(1);
				}
			}
		}
		if (dswidth && ((agsize % dswidth) == 0) && (agcount > 1)) {
			/* This is a non-optimal configuration because all AGs
			 * start on the same disk in the stripe.  Changing 
			 * the AG size by one sunit will guarantee that this
			 * does not happen.
			 */
			tmp_agsize = agsize - dsunit;
			if (tmp_agsize < XFS_AG_MIN_BLOCKS(blocklog)) {
				tmp_agsize = agsize + dsunit;
				if (dblocks < agsize) {
					/* oh well, nothing to do */
					tmp_agsize = agsize;
				}
			}
			if (daflag || dasize) {
				fprintf(stderr, _(
"Warning: AG size is a multiple of stripe width.  This can cause performance\n\
problems by aligning all AGs on the same disk.  To avoid this, run mkfs with\n\
an AG size that is one stripe unit smaller, for example %llu.\n"),
					(unsigned long long)tmp_agsize);
			} else {
				agsize = tmp_agsize;
				agcount = dblocks/agsize + (dblocks % agsize != 0);
				/*
				 * If the last AG is too small, reduce the
				 * filesystem size and drop the blocks.
				 */
				if ( dblocks % agsize != 0 &&
				    (dblocks % agsize <
				    XFS_AG_MIN_BLOCKS(blocklog))) {
					dblocks = (xfs_drfsbno_t)((agcount - 1) * agsize);
					agcount--;
					ASSERT(agcount != 0);
				}
			}
		}
	} else {
		if (nodsflag)
			dsunit = dswidth = 0;
		else { 
			fprintf(stderr,
				_("%s: Stripe unit(%d) or stripe width(%d) is "
				"not a multiple of the block size(%d)\n"),
				progname, BBTOB(dsunit), BBTOB(dswidth), 
				blocksize); 	
			exit(1);
		}
	}

	/*
	 * check that log sunit is modulo fsblksize or default it to dsunit.
	 */

	if (lsunit) {
		if ((BBTOB(lsunit) % blocksize != 0)) {
			fprintf(stderr,
	_("log stripe unit (%d) must be a multiple of the block size (%d)\n"),
			BBTOB(lsunit), blocksize);
			exit(1);
		}
		/* convert from 512 byte blocks to fs blocks */
		lsunit = DTOBT(lsunit);
	} else if (logversion == 2 && loginternal && dsunit) {
		/* lsunit and dsunit now in fs blocks */
		lsunit = dsunit;
	}

	if (logversion == 2 && (lsunit * blocksize) > 256 * 1024) {
		fprintf(stderr,
	_("log stripe unit (%d bytes) is too large (maximum is 256KiB)\n"),
			(lsunit * blocksize));
		lsunit = (32 * 1024) >> blocklog;
		fprintf(stderr, _("log stripe unit adjusted to 32KiB\n"));
	}

	bsize = 1 << (blocklog - BBSHIFT);
	mp = &mbuf;
	sbp = &mp->m_sb;
	bzero(mp, sizeof(xfs_mount_t));
	sbp->sb_blocklog = (__uint8_t)blocklog;
	sbp->sb_sectlog = (__uint8_t)sectorlog;
	sbp->sb_agblklog = (__uint8_t)libxfs_log2_roundup((unsigned int)agsize);
	mp->m_blkbb_log = sbp->sb_blocklog - BBSHIFT;
	mp->m_sectbb_log = sbp->sb_sectlog - BBSHIFT;

	if (loginternal) {
		/*
		 * Readjust the log size to fit within an AG if it was sized
		 * automaticly.
		 */
		if (!logsize) {
			logblocks = MIN(logblocks,
					agsize - XFS_PREALLOC_BLOCKS(mp));
		}
		if (logblocks > agsize - XFS_PREALLOC_BLOCKS(mp)) {
			fprintf(stderr,
	_("internal log size %lld too large, must fit in allocation group\n"),
				(long long)logblocks);
			usage();
		}
		if (laflag) {
			if (logagno >= agcount) {
				fprintf(stderr,
		_("log ag number %d too large, must be less than %lld\n"),
					logagno, (long long)agcount);
				usage();
			}
		} else
			logagno = (xfs_agnumber_t)(agcount / 2);

		logstart = XFS_AGB_TO_FSB(mp, logagno, XFS_PREALLOC_BLOCKS(mp));
		/*
		 * Align the logstart at stripe unit boundary.
		 */
		if (lsunit) {
			logstart = fixup_internal_log_stripe(mp,
					lsflag, logstart, agsize, lsunit,
					&logblocks, blocklog, &lalign);
		} else if (dsunit) {
			logstart = fixup_internal_log_stripe(mp,
					lsflag, logstart, agsize, dsunit,
					&logblocks, blocklog, &lalign);
		}
	} else {
		logstart = 0;
		if (lsunit)
			fixup_log_stripe_unit(lsflag, lsunit,
					&logblocks, blocklog);
	}
	validate_log_size(logblocks, blocklog, min_logblocks);

	if (!qflag) {
		printf(_(
		   "meta-data=%-22s isize=%-6d agcount=%lld, agsize=%lld blks\n"
		   "         =%-22s sectsz=%-5u attr=%u\n"
		   "data     =%-22s bsize=%-6u blocks=%llu, imaxpct=%u\n"
		   "         =%-22s sunit=%-6u swidth=%u blks, unwritten=%u\n"
		   "naming   =version %-14u bsize=%-6u\n"
		   "log      =%-22s bsize=%-6d blocks=%lld, version=%d\n"
		   "         =%-22s sectsz=%-5u sunit=%d blks\n"
		   "realtime =%-22s extsz=%-6d blocks=%lld, rtextents=%lld\n"),
			dfile, isize, (long long)agcount, (long long)agsize,
			"", sectorsize, attrversion,
			"", blocksize, (long long)dblocks,
				imflag ? imaxpct : XFS_DFL_IMAXIMUM_PCT,
			"", dsunit, dswidth, extent_flagging,
			dirversion, dirversion == 1 ? blocksize : dirblocksize,
			logfile, 1 << blocklog, (long long)logblocks,
			logversion, "", lsectorsize, lsunit,
			rtfile, rtextblocks << blocklog,
			(long long)rtblocks, (long long)rtextents);
	}

	uint32_t begresv;
	uint32_t rbegresv;
	int maxbtreelevel;
	int yet_resv;

	{
		struct progress_struct progress;

		xfs_fd = libxfs_device_to_fd(xi.ddev);
		xfs_blocksize = info->si_blocksize;
		xfs_blocks_count = dblocks;
		xfs_dirblocksize = dirblocksize;

		xfs_agsize = agsize;
		xfs_agblklog = 1;

		while ( (1<<xfs_agblklog) < agsize ) xfs_agblklog++;

		xfs_aginodes = agsize * xfs_blocksize/isize;
		xfs_aginolog = 1;
		while ( (1<<xfs_aginolog) < xfs_aginodes ) xfs_aginolog++;

		int             level;
		unsigned        maxblocks;
		unsigned        maxleafents;
		int             minleafrecs;
		int             minnoderecs;

		maxleafents = (agsize+1)/2;
		minleafrecs = (blocksize-16)/8;
		minnoderecs = (blocksize-16)/12;
		maxblocks = (maxleafents + minleafrecs - 1) / minleafrecs;
		for (level = 1; maxblocks > 1; level++)
			maxblocks = (maxblocks + minnoderecs - 1) / minnoderecs;
		maxbtreelevel = level;

		begresv = (0x800 + blocksize-1)/blocksize + 3;
		uint32_t begresvp4 = begresv + 4;
		rbegresv = 1;
		while ( rbegresv < begresvp4 ) rbegresv<<=1;

		int inodes_per_ag = agsize*blocksize/isize;
		int recs_inodes_per_ag = inodes_per_ag/64;
		int max_recs_inodes_per_block = (blocksize-16)/16;
		int resv_for_inodes_btree = (recs_inodes_per_ag + 
				max_recs_inodes_per_block - 1)/max_recs_inodes_per_block;

		yet_resv = 2*maxbtreelevel - (rbegresv-begresv) + 
			resv_for_inodes_btree;
		if (yet_resv<0) yet_resv=0;

		uint16_t inopblock = blocksize / isize;
		uint32_t incin = ((inopblock%64)?64/(inopblock%64):1)*inopblock;
		uint32_t max_inag = (agsize - rbegresv - yet_resv)*inopblock;
		uint32_t last_ag = 0;

		int log_ag = -1;
	        if (loginternal)
			log_ag = logstart >> xfs_agblklog;

		bitmap_l = ( dblocks +
				sizeof(unsigned long) - 1 )/sizeof(unsigned long);

		/*allocate memory for block bitmap*/
		block_bitmap =  (typeof(block_bitmap))
			malloc(bitmap_l*sizeof(unsigned long));
		if (!block_bitmap)
		{ retval = -ENOMEM; exit(-retval); }

		memset(block_bitmap, 0, bitmap_l*sizeof(unsigned long));

		retval = fill_block_bitmap (info, block_bitmap, dblocks);
		if (retval) exit(-retval);

		/*allocate memory for block bitmap for system information*/
		xfs_block_bitmap =  (typeof(xfs_block_bitmap))
			malloc(bitmap_l*sizeof(unsigned long));
		if (!block_bitmap)
		{ retval = -ENOMEM; exit(-retval); }

		memset(xfs_block_bitmap, 0, bitmap_l*sizeof(unsigned long));

		/* 
		 * map of inode numbers in anyfs to inode number in
		 * future xfs 
		 */
		inode_map = malloc (info->si_inodes*sizeof(uint64_t));
		if (!inode_map)
		{ retval = -ENOMEM; exit(-retval); }

		memset(inode_map, 0, info->si_inodes*sizeof(uint64_t));

		/*
		 * array of allocation groups
		 */
		agi_array = malloc (agcount*sizeof(xfs_agi_t));
		if (!agi_array)
		{ retval = -ENOMEM; exit(-retval); }

		memset(agi_array, 0, agcount*sizeof(xfs_agi_t));

		if (verbose)
			printf(_("Remapping of inodes to XFS Inode Numbers\n"));

		if (qflag)
			memset(&progress, 0, sizeof(progress));
		else
			progress_init(&progress, _("Remap inodes: "),
					info->si_inodes);
		
		for (i=0; i<info->si_inodes; i++)
		{
			progress_update(&progress, i);

			uint32_t ag = last_ag; 
			uint64_t xfs_ino = 0;
			
			if (!info->si_inode_table[i].i_links_count) continue;

			while(1)
			{
				xfs_ino = (ag*agsize + rbegresv)*inopblock;

				if ( !agi_array[ag].agi_freecount )
				{
					xfs_ino += agi_array[ag].agi_count;
					
					if ( ((xfs_ino+incin-1)/inopblock)>dblocks || (agi_array[ag].agi_count+incin)>max_inag )
					{
						ag++;
						
						if (ag==log_ag) ag++;

						if (ag==agcount) {
							fprintf (stderr, 
							_("Cannot allocate inode: not enough space\n"));
							retval = -ENOSPC;
							exit(-retval);
						}
						continue;
					}
					
					agi_array[ag].agi_count += incin;
					agi_array[ag].agi_freecount += incin-1;
				}
				else
				{
					xfs_ino += agi_array[ag].agi_count - agi_array[ag].agi_freecount;
					agi_array[ag].agi_freecount--;
				}

				break;
			}
			
			if (i==1) 
			{
				if (ag)
				{
					fprintf(stderr,
							_("%s: root inode created in AG %u, not AG 0\n"),
							progname, ag);
					exit(1);
				}
				agi_array[ag].agi_freecount-=2;
			}
			
			inode_map[i] = XFS_INO(xfs_ino);
			last_ag = ag;
		}

		progress_close(&progress);
		
#ifdef DEBUG
		if (verbose>=3)
		for (i=0; i<info->si_inodes; i++)
		{
			if (inode_map[i])
				printf ("%x -> %llx\n", i, (unsigned long long) inode_map[i]);
		}
#endif /*DEBUG*/

		for (i=0; i<agcount; i++)
		{
			for (j=0; j<begresv; j++)
			{
				if ( (i*agsize + j)>dblocks ) break;
				set_bit ( i*agsize + j, xfs_block_bitmap );
			}

			for (j=rbegresv; j<(rbegresv + agi_array[i].agi_count/inopblock); j++)
			{
				if ( (i*agsize + j)>dblocks ) break;
				set_bit ( i*agsize + j, xfs_block_bitmap );
			}
		}

		if (loginternal)
			for (i=0; i<logblocks; i++)
				set_bit ( ANYFS_BLKNO(logstart) + i, xfs_block_bitmap );

		for (i=0; i<agcount; i++)
		{
			if (i==log_ag) continue;

			for (j=begresv; j<rbegresv; j++)
			{
				if ( (i*agsize + j)>dblocks ) break;
				set_bit ( i*agsize + j, xfs_block_bitmap );
			}

			for (j=(rbegresv + agi_array[i].agi_count/inopblock); 
					j<(rbegresv + agi_array[i].agi_count/inopblock + yet_resv); j++)
			{
				if ( (i*agsize + j)>dblocks ) break;
				set_bit ( i*agsize + j, xfs_block_bitmap );
			}
		}

		if (loginternal)
			for (i=logblocks; i<(logblocks + maxbtreelevel*2); i++)
				set_bit ( ANYFS_BLKNO(logstart) + i, xfs_block_bitmap );
	}

	{
		quiet = qflag;
		retval = any_release_sysinfo(info, block_bitmap,
				readblk,
				writeblk,
				testblk,
				getblkcount);
		if (retval<0) return -retval;
	}

	if (verbose)
		printf (_("Starting building XFS inodes\n"));
	
	{
		struct progress_struct progress;
		int dirblkfsbs = dirblocksize/blocksize;

		for (i=1; i<dblocks; i++)
			if ( test_bit( i, block_bitmap) )
				set_bit ( i, xfs_block_bitmap );

		char *ibuf = malloc(isize);
		if (!ibuf)
		{
			fprintf(stderr, _("Not enough memory\n"));
			retval = -ENOMEM; exit(-retval);
		}

		if (qflag)
			memset(&progress, 0, sizeof(progress));
		else
			progress_init(&progress, _("building inodes: "),
					info->si_inodes);

		any_adddadd(info);

		for (i=0; i<info->si_inodes; i++) {
			if ( !info->si_inode_table[i].i_links_count ) continue;
				
			if (verbose>=2)
				fprintf(stderr, _("inode #%d (#%llu)\n"), i, 
						(unsigned long long) inode_map[i]);

			xfs_dinode_core_t *inode = NULL;
			
			progress_update(&progress, i);

			memset(ibuf, 0, isize);
		
			inode = (xfs_dinode_core_t *)ibuf;
			
			INT_SET(inode->di_magic, ARCH_CONVERT, XFS_DINODE_MAGIC);
			INT_SET(inode->di_mode, ARCH_CONVERT, info->si_inode_table[i].i_mode);
			INT_SET(inode->di_version, ARCH_CONVERT, 1);
			INT_SET(inode->di_format, ARCH_CONVERT, XFS_DINODE_FMT_EXTENTS);
			INT_SET(inode->di_onlink, ARCH_CONVERT, info->si_inode_table[i].i_links_count);
			INT_SET(inode->di_uid, ARCH_CONVERT, info->si_inode_table[i].i_uid);
			INT_SET(inode->di_gid, ARCH_CONVERT, info->si_inode_table[i].i_gid);
			INT_SET(inode->di_nlink, ARCH_CONVERT, info->si_inode_table[i].i_links_count);
			INT_SET(inode->di_atime.t_sec, ARCH_CONVERT, info->si_inode_table[i].i_atime);
			INT_SET(inode->di_ctime.t_sec, ARCH_CONVERT, info->si_inode_table[i].i_ctime);
			INT_SET(inode->di_mtime.t_sec, ARCH_CONVERT, info->si_inode_table[i].i_mtime);
			INT_SET(inode->di_size, ARCH_CONVERT, info->si_inode_table[i].i_size);
			INT_SET(inode->di_aformat, ARCH_CONVERT, XFS_DINODE_FMT_EXTENTS);
			
			uint32_t *p_next_unlinked = (uint32_t*) (inode+1);
			INT_SET(*p_next_unlinked, ARCH_CONVERT, 0xFFFFFFFF);

			void *p_inode = (void*) (p_next_unlinked+1);

			if (i==1)
			{
				INT_SET(inode->di_mode, ARCH_CONVERT, 0100000);
				INT_SET(inode->di_onlink, ARCH_CONVERT, 1);
				INT_SET(inode->di_nlink, ARCH_CONVERT, 1);
				INT_SET(inode->di_size, ARCH_CONVERT, 0);
				if (!noaction)
				{
					pwrite64(xfs_fd, ibuf, isize, (ANYFS_INO(inode_map[1])+1)*isize);
					pwrite64(xfs_fd, ibuf, isize, (ANYFS_INO(inode_map[1])+2)*isize);
				}
				INT_SET(inode->di_mode, ARCH_CONVERT, info->si_inode_table[i].i_mode);
				INT_SET(inode->di_onlink, ARCH_CONVERT, info->si_inode_table[i].i_links_count);
				INT_SET(inode->di_nlink, ARCH_CONVERT, info->si_inode_table[i].i_links_count);
				INT_SET(inode->di_size, ARCH_CONVERT, info->si_inode_table[i].i_size);
			}

			if ( S_ISLNK(info->si_inode_table[i].i_mode) ) {
				uint64_t size = strlen(info->si_inode_table[i].i_info.symlink);
				if ( size >= MAXNAMELEN )
				{
					fprintf(stderr, _("symlink #%d (#%llu) has too long size\n"), i, 
							(unsigned long long) inode_map[i]);
					fprintf(stderr, _("its size %llu will truncate to %u\n"),
							(unsigned long long) size, MAXNAMELEN-1);
					fprintf(stderr, _("link \"%s\" "), info->si_inode_table[i].i_info.symlink);
				        info->si_inode_table[i].i_info.symlink[MAXNAMELEN-1] = '\0';
					fprintf(stderr, _("truncated to \"%s\"\n"), info->si_inode_table[i].i_info.symlink);
					size = strlen(info->si_inode_table[i].i_info.symlink);
				}

				INT_SET(inode->di_size, ARCH_CONVERT, size);
				if ( (isize - sizeof(xfs_dinode_core_t) - 4)<size )
				{
					if (verbose>=2)
						fprintf(stderr, _("Long Link\n"));
					/*Long link*/
					struct any_file_frags link_frags;
					link_frags.fr_nfrags = 0;
					link_frags.fr_frags = NULL;

					struct any_file_fragment *frags = link_frags.fr_frags;
					uint32_t nfrag = 0;
					uint64_t b = 0;

					char *bbuf = malloc(blocksize);
					if (!bbuf)
					{
						fprintf(stderr, _("Not enough memory\n"));
						retval = -ENOMEM; exit(-retval);
					}

					char *plink = info->si_inode_table[i].i_info.symlink;

					while (size)
					{
						memset(bbuf, 0, blocksize);

						int err = add_block (&link_frags);
						if (err)
						{
							fprintf(stderr, _("Cannot find free block for long link: Not enough space\n"));
							retval = -ENOSPC; exit(-retval);
						}
						frags = link_frags.fr_frags;
						nfrag = link_frags.fr_nfrags-1;

						b = frags[nfrag].fr_start + frags[nfrag].fr_length - 1;

						int ds = size;
						if (ds > blocksize) ds = blocksize;
						memcpy(bbuf, plink, ds);
						plink += ds;
						size -= ds;

						if (!noaction)
							pwrite64(xfs_fd, bbuf, blocksize, b*blocksize);
					}

					free (bbuf);

					write_extentlist( 
							&link_frags, 
							inode, 
							isize, 
							p_inode, 0, 0 );
					free (link_frags.fr_frags);
				}
				else
				{
					/*Short link*/
					if (verbose>=2)
						fprintf(stderr, _("Short Link\n"));
					INT_SET(inode->di_format, ARCH_CONVERT, XFS_DINODE_FMT_LOCAL);
					memcpy(p_inode, info->si_inode_table[i].i_info.symlink, size);
				}
			} else

			if ( S_ISREG(info->si_inode_table[i].i_mode) ) {
				if (verbose>=2)
					fprintf(stderr, _("Regular File\n"));
				write_extentlist( 
						info->si_inode_table[i].
						i_info.file_frags, 
						inode, 
						isize, 
						p_inode, 0, 0);
			} else

			if ( S_ISDIR(info->si_inode_table[i].i_mode) ) {
				uint64_t size = 6;
				uint32_t parent = 0;
				uint8_t i8count = 0;
				struct any_dirent* dirent = NULL;

				for (dirent = info->si_inode_table[i].i_info.dir->d_dirent; dirent; dirent = dirent->d_next)
				{
					if ( strcmp(dirent->d_name, "..")==0 )
					{
						parent = dirent->d_inode;
						if (inode_map[parent]>0xFFFFFFFF)
							i8count++;
						continue;
					}
					
					if ( strcmp(dirent->d_name, ".")==0 )
						continue;

					if (inode_map[dirent->d_inode]>0xFFFFFFFF)
						i8count++;
					
					size += 7 + strlen(dirent->d_name);
				}

				if (i8count) size += (info->si_inode_table[i].i_info.dir->d_ndirents-1)*4;
				
				if ( (isize - sizeof(xfs_dinode_core_t) - 4)<size )
				{
					/*Long directory*/
					struct any_file_frags dir_frags;
					dir_frags.fr_nfrags = 0;
					dir_frags.fr_frags = NULL;

					uint32_t ulen = 0;
					uint32_t leaflen = 0;
					
					uint32_t countu = 1;
					uint32_t lastulen = 0;
					uint32_t countleaf = 1;
					uint32_t lastleaflen = 0;
					
					for (dirent = info->si_inode_table[i].i_info.dir->d_dirent; dirent; dirent = dirent->d_next)
					{
						uint32_t u = (11 + strlen(dirent->d_name) +7)/8*8;
						ulen += u;
						leaflen += 8;

						if ( (lastulen + u +16)>dirblocksize )
						{
							countu++;
							lastulen = 0;
						}
						lastulen += u;

						if ( (lastleaflen + 8 +16)>dirblocksize )
						{
							countleaf++;
							lastleaflen = 0;
						}
						lastleaflen += 8;
					}

					if ( (16+ulen+leaflen+4)>dirblocksize )
					{
						if (verbose>=2)
						{
							fprintf(stderr, _("multiple blocks directory with XD2D blocks -- %d\n"),
									countu);
							fprintf(stderr, _("\tlast XD2D blocks -- %d\n"),
									lastulen);
						}

						if ( (16+leaflen+2*countu+4)>dirblocksize )
						{
							if (verbose>=2)
							{
								fprintf(stderr, _("\twith multiple block leaf (count = %d, lastlen = %d)\n"),
										countleaf, lastleaflen);
							}

							int height;
							int *links_at_level;
							int **nlinks;

							calc_links_at_levels(&links_at_level, &nlinks,
									&height, info->si_inode_table[i].i_info.dir->d_ndirents, (dirblocksize-16)/8);

							int pblocks = 0;

							for (j=1; j<=height-1; j++)
								pblocks += links_at_level[j];

							int err = add_dirblock (&dir_frags, countu + countleaf + pblocks);
							if (err)
							{
								fprintf(stderr, _("Cannot find free block for multiple block directory\n"
										  "       with multiple leaf block: Not enough space\n"));
								retval = -ENOSPC; exit(-retval);
							}

							countleaf = pblocks+1;

							struct any_file_fragment *frags = dir_frags.fr_frags;
							uint32_t nfrag = 0;

							uint64_t b = frags[nfrag].fr_start + frags[nfrag].fr_length - dirblkfsbs;
							char *bbuf = malloc(dirblocksize);
							if (!bbuf)
							{
								fprintf(stderr, _("Not enough memory\n"));
								retval = -ENOMEM; exit(-retval);
							}
							memset(bbuf, 0, dirblocksize);

							void *bp = (void*) bbuf;

							xfs_dir2_data_hdr_t *hdr = (xfs_dir2_data_hdr_t *)bp;
							INT_SET(hdr->magic, ARCH_CONVERT, XFS_DIR2_DATA_MAGIC);

							int j;
							for (j=0; j<XFS_DIR2_DATA_FD_COUNT; j++)
							{
								INT_SET(hdr->bestfree[j].offset, ARCH_CONVERT, 0);
								INT_SET(hdr->bestfree[j].length, ARCH_CONVERT, 0);
							}

							bp = (void*) (hdr+1);
							struct hashlist *hashlist = NULL;
							struct hashlist **p_hashlist = &hashlist;

							uint32_t count = 0;
							//uint32_t stale = 0;

							uint32_t blockcount = 0;
							uint16_t *bests = malloc(sizeof(uint16_t)*countu);

							for (dirent = info->si_inode_table[i].i_info.dir->d_dirent; dirent; dirent = dirent->d_next)
							{
								int thisfieldsize = (11 + strlen(dirent->d_name) +7)/8*8;
								if ( (dirblocksize - ((char*) bp - bbuf))<thisfieldsize )
								{
									uint32_t unused_len = (dirblocksize - ((char*)bp - bbuf))/8;

									if ( unused_len>0 )
									{
										void *bbp = bp;

										fill_unused(bbuf, &bp, unused_len);

										INT_SET(hdr->bestfree[0].offset, ARCH_CONVERT, (char*)bbp - bbuf);
										INT_SET(hdr->bestfree[0].length, ARCH_CONVERT, unused_len*8);

										//stale++;
									}

									bests[blockcount] = unused_len*8;

									blockcount++;
									ASSERT(blockcount<=countu);

									if (!noaction)
										pwrite64(xfs_fd, bbuf, dirblocksize, b*blocksize);

									memset(bbuf, 0, dirblocksize);

									err = add_dirblock (&dir_frags, 0);
									if (err)
									{
										fprintf(stderr, _("Cannot find free block for multiple block directory\n"
												  "       with multiple leaf block: Not enough space\n"));
										retval = -ENOSPC; exit(-retval);
									}
									nfrag = dir_frags.fr_nfrags-1;

									b = frags[nfrag].fr_start + frags[nfrag].fr_length - dirblkfsbs;

									bp = (void*) bbuf;

									hdr = (xfs_dir2_data_hdr_t *)bp;
									INT_SET(hdr->magic, ARCH_CONVERT, XFS_DIR2_DATA_MAGIC);

									for (j=0; j<XFS_DIR2_DATA_FD_COUNT; j++)
									{
										INT_SET(hdr->bestfree[j].offset, ARCH_CONVERT, 0);
										INT_SET(hdr->bestfree[j].length, ARCH_CONVERT, 0);
									}

									bp = (void*) (hdr+1);
								}

								void *bbp = bp;

								fill_direntry(bbuf, &bp, dirent, inode_map[dirent->d_inode]);

								add_to_hashlist(p_hashlist, 
										libxfs_da_hashname(dirent->d_name, strlen(dirent->d_name)), 
										((char*)bbp - bbuf + blockcount*dirblocksize)/8);

								count++;
							}
							ASSERT( count == info->si_inode_table[i].i_info.dir->d_ndirents );

							uint32_t unused_len = (dirblocksize - ((char*)bp - bbuf))/8;

							if ( unused_len>0 )
							{
								void *bbp = bp;

								fill_unused(bbuf, &bp, unused_len);

								INT_SET(hdr->bestfree[0].offset, ARCH_CONVERT, (char*)bbp - bbuf);
								INT_SET(hdr->bestfree[0].length, ARCH_CONVERT, unused_len*8);

								//stale++;
							}

							bests[blockcount] = unused_len*8;

							blockcount++;
							ASSERT(blockcount==countu);

							if (!noaction)
								pwrite64(xfs_fd, bbuf, dirblocksize, b*blocksize);

							memset(bbuf, 0, dirblocksize);

							pblocks++;
							uint64_t *al_blocks = malloc (sizeof(uint64_t)*pblocks);

							for (j=0; j<pblocks; j++)
							{
								err = add_dirblock (&dir_frags, j?0:1);
								if (err)
								{
									fprintf(stderr, _("Cannot find free block for multiple block directory\n"
											  "       with multiple leaf block: Not enough space\n"));
									retval = -ENOSPC; exit(-retval);
								}
								nfrag = dir_frags.fr_nfrags-1;

								b = frags[nfrag].fr_start + frags[nfrag].fr_length - dirblkfsbs;
								al_blocks[j] = b;
							}

							int al_block = pblocks-1;

							//uint64_t xfs_dirdatablk = XFS_DIR2_DATA_OFFSET/xfs_blocksize;
							uint64_t xfs_dirleafblk = XFS_DIR2_LEAF_OFFSET/xfs_blocksize;
							//uint64_t xfs_dirfreeblk = XFS_DIR2_FREE_OFFSET/xfs_blocksize;
							uint32_t n_address = xfs_dirleafblk + pblocks*dirblkfsbs;

							int l;
							for (l=height; l>0; l--)
							{
								struct hashlist *hashlist_z = NULL;
								struct hashlist **p_hashlist_z = &hashlist_z;

								int j;
								for (j=0; j<links_at_level[l-1]; j++)
								{
									uint32_t n_hashval=0;

									ASSERT(al_block>=0);
									b = al_blocks[al_block--];
									n_address -= dirblkfsbs;

									bp = (void*) bbuf;

									xfs_dir2_leaf_hdr_t *leafhdr = (xfs_dir2_leaf_hdr_t*) bp;
									bp = (void*) (leafhdr+1);

									uint32_t forw=0;
									uint32_t backw=0;

									if (j<(links_at_level[l-1]-1)) forw = n_address - dirblkfsbs;
									if (j>0) backw = n_address + dirblkfsbs;

									INT_SET(leafhdr->info.forw, ARCH_CONVERT, forw);
									INT_SET(leafhdr->info.back, ARCH_CONVERT, backw);
									if ( l==height )
										INT_SET(leafhdr->info.magic, ARCH_CONVERT, XFS_DIR2_LEAFN_MAGIC);
									else
										INT_SET(leafhdr->info.magic, ARCH_CONVERT, XFS_DA_NODE_MAGIC);
									INT_SET(leafhdr->info.pad, ARCH_CONVERT, 0);

									INT_SET(leafhdr->count, ARCH_CONVERT, nlinks[l][j]);
									INT_SET(leafhdr->stale, ARCH_CONVERT, height-l);

									int k;
									for (k=0; k<nlinks[l][j]; k++)
									{
										ASSERT(*p_hashlist);

										uint32_t *hashval = (uint32_t*) bp;
										bp = (void*) (hashval+1);

										uint32_t *address = (uint32_t*) bp;
										bp = (void*) (address+1);

										ASSERT( ( (char*)bp - bbuf ) <= dirblocksize );
										ASSERT( (( (char*)bp - bbuf )%8) == 0 );

										INT_SET(*hashval, ARCH_CONVERT, (*p_hashlist)->hashval);
										INT_SET(*address, ARCH_CONVERT, (*p_hashlist)->address);

										if ((k+1)==nlinks[l][j])
											n_hashval = (*p_hashlist)->hashval;

										p_hashlist = &(*p_hashlist)->next;
									}

									add_to_hashlist(p_hashlist_z, 
											n_hashval, 
											n_address);

									if (!noaction)
										pwrite64(xfs_fd, bbuf, dirblocksize, b*blocksize);

									memset(bbuf, 0, dirblocksize);
								}

								ASSERT(!(*p_hashlist));

								struct hashlist *l_hashlist = hashlist;
								while (l_hashlist)
								{
									struct hashlist *h = l_hashlist;

									l_hashlist = l_hashlist->next;
									free (h);
								}

								hashlist = hashlist_z;
								p_hashlist = &hashlist;
							}

							ASSERT(al_block==-1);
							free(al_blocks);

							err = add_dirblock (&dir_frags, 1);
							if (err)
							{
								fprintf(stderr, _("Cannot find free block for multiple block directory\n"
										  "       with multiple leaf block: Not enough space\n"));
								retval = -ENOSPC; exit(-retval);
							}
							nfrag = dir_frags.fr_nfrags-1;

							b = frags[nfrag].fr_start + frags[nfrag].fr_length - dirblkfsbs;

							bp = (void*) bbuf;

							xfs_dir2_free_hdr_t *freehdr = (xfs_dir2_free_hdr_t*) bp;
							bp = (void*) (freehdr+1);

							int nused = 0;
							int nvalid = 0;
							int firstdb = 0;

							INT_SET(freehdr->magic, ARCH_CONVERT, XFS_DIR2_FREE_MAGIC);

							for (j=0; j<blockcount; j++)
							{
								int thisfieldsize = 2;
								if ( (dirblocksize - ((char*) bp - bbuf))<thisfieldsize )
								{
									INT_SET(freehdr->firstdb, ARCH_CONVERT, firstdb);
									INT_SET(freehdr->nvalid, ARCH_CONVERT, nvalid);
									INT_SET(freehdr->nused, ARCH_CONVERT, nused);

									if (!noaction)
										pwrite64(xfs_fd, bbuf, dirblocksize, b*blocksize);
									memset(bbuf, 0, dirblocksize);

									err = add_dirblock (&dir_frags, 0);
									if (err)
									{
										fprintf(stderr, _("Cannot find free block for multiple block directory\n"
												  "       with multiple leaf block: Not enough space\n"));
										retval = -ENOSPC; exit(-retval);
									}
									nfrag = dir_frags.fr_nfrags-1;

									b = frags[nfrag].fr_start + frags[nfrag].fr_length - dirblkfsbs;

									bp = (void*) bbuf;

									xfs_dir2_free_hdr_t *freehdr = (xfs_dir2_free_hdr_t*) bp;
									bp = (void*) (freehdr+1);

									firstdb += nvalid;
									nused = 0;
									nvalid = 0;

									INT_SET(freehdr->magic, ARCH_CONVERT, XFS_DIR2_FREE_MAGIC);
								}

								uint16_t *bestfree = (uint16_t*) bp;
								bp = (void*) (bestfree+1);

								ASSERT( ( (char*)bp - bbuf ) <= dirblocksize );
								INT_SET(*bestfree, ARCH_CONVERT, bests[j]);

								nused++;
								nvalid++;
							}

							INT_SET(freehdr->firstdb, ARCH_CONVERT, firstdb);
							INT_SET(freehdr->nvalid, ARCH_CONVERT, nvalid);
							INT_SET(freehdr->nused, ARCH_CONVERT, nused);

							if (!noaction)
								pwrite64(xfs_fd, bbuf, dirblocksize, b*blocksize);
							free (bbuf);
						}
						else
						{
							if (verbose>=2)
								fprintf(stderr, _("\twith single block leaf (leaflen = %d)\n"),
										leaflen);

							int err = add_dirblock (&dir_frags, countu);
							if (err)
							{
								fprintf(stderr, _("Cannot find free block for multiple block directory\n"
										  "       with single leaf block: Not enough space\n"));
								retval = -ENOSPC; exit(-retval);
							}

							struct any_file_fragment *frags = dir_frags.fr_frags;
							uint32_t nfrag = 0;

							uint64_t b = frags[nfrag].fr_start + frags[nfrag].fr_length - dirblkfsbs;
							char *bbuf = malloc(dirblocksize);
							if (!bbuf)
							{
								fprintf(stderr, _("Not enough memory\n"));
								retval = -ENOMEM; exit(-retval);
							}
							memset(bbuf, 0, dirblocksize);

							void *bp = (void*) bbuf;

							xfs_dir2_data_hdr_t *hdr = (xfs_dir2_data_hdr_t *)bp;
							INT_SET(hdr->magic, ARCH_CONVERT, XFS_DIR2_DATA_MAGIC);

							int j;
							for (j=0; j<XFS_DIR2_DATA_FD_COUNT; j++)
							{
								INT_SET(hdr->bestfree[j].offset, ARCH_CONVERT, 0);
								INT_SET(hdr->bestfree[j].length, ARCH_CONVERT, 0);
							}

							bp = (void*) (hdr+1);
							struct hashlist *hashlist = NULL;
							struct hashlist **p_hashlist = &hashlist;

							uint32_t count = 0;
							uint32_t stale = 0;

							uint32_t blockcount = 0;
							uint16_t *bests = malloc(sizeof(uint16_t)*countu);

							for (dirent = info->si_inode_table[i].i_info.dir->d_dirent; dirent; dirent = dirent->d_next)
							{
								int thisfieldsize = (11 + strlen(dirent->d_name) +7)/8*8;
								if ( (dirblocksize - ((char*) bp - bbuf))<thisfieldsize )
								{
									uint32_t unused_len = (dirblocksize - ((char*)bp - bbuf))/8;

									if ( unused_len>0 )
									{
										void *bbp = bp;

										fill_unused(bbuf, &bp, unused_len);

										INT_SET(hdr->bestfree[0].offset, ARCH_CONVERT, (char*)bbp - bbuf);
										INT_SET(hdr->bestfree[0].length, ARCH_CONVERT, unused_len*8);

										//stale++;
									}

									bests[blockcount] = unused_len*8;

									blockcount++;
									ASSERT(blockcount<=countu);

									if (!noaction)
										pwrite64(xfs_fd, bbuf, dirblocksize, b*blocksize);

									memset(bbuf, 0, dirblocksize);

									err = add_dirblock (&dir_frags, 0);
									if (err)
									{
										fprintf(stderr, _("Cannot find free block for multiple block directory\n"
												  "       with single leaf block: Not enough space\n"));
										retval = -ENOSPC; exit(-retval);
									}
									nfrag = dir_frags.fr_nfrags-1;

									b = frags[nfrag].fr_start + frags[nfrag].fr_length - dirblkfsbs;

									bp = (void*) bbuf;

									hdr = (xfs_dir2_data_hdr_t *)bp;
									INT_SET(hdr->magic, ARCH_CONVERT, XFS_DIR2_DATA_MAGIC);

									for (j=0; j<XFS_DIR2_DATA_FD_COUNT; j++)
									{
										INT_SET(hdr->bestfree[j].offset, ARCH_CONVERT, 0);
										INT_SET(hdr->bestfree[j].length, ARCH_CONVERT, 0);
									}

									bp = (void*) (hdr+1);
								}

								void *bbp = bp;

								fill_direntry(bbuf, &bp, dirent, inode_map[dirent->d_inode]);

								add_to_hashlist(p_hashlist, 
										libxfs_da_hashname(dirent->d_name, strlen(dirent->d_name)), 
										((char*)bbp - bbuf + blockcount*dirblocksize)/8);

								count++;
							}
							ASSERT( count == info->si_inode_table[i].i_info.dir->d_ndirents );

							uint32_t unused_len = (dirblocksize - ((char*)bp - bbuf))/8;

							if ( unused_len>0 )
							{
								void *bbp = bp;

								fill_unused(bbuf, &bp, unused_len);

								INT_SET(hdr->bestfree[0].offset, ARCH_CONVERT, (char*)bbp - bbuf);
								INT_SET(hdr->bestfree[0].length, ARCH_CONVERT, unused_len*8);

								//stale++;
							}

							bests[blockcount] = unused_len*8;

							blockcount++;
							ASSERT(blockcount==countu);

							if (!noaction)
								pwrite64(xfs_fd, bbuf, dirblocksize, b*blocksize);

							memset(bbuf, 0, dirblocksize);

							err = add_dirblock (&dir_frags, 1);
							if (err)
							{
								fprintf(stderr, _("Cannot find free block for multiple block directory\n"
										  "       with single leaf block: Not enough space\n"));
								retval = -ENOSPC; exit(-retval);
							}
							nfrag = dir_frags.fr_nfrags-1;

							b = frags[nfrag].fr_start + frags[nfrag].fr_length - dirblkfsbs;

							bp = (void*) bbuf;

							xfs_dir2_leaf_hdr_t *leafhdr = (xfs_dir2_leaf_hdr_t*) bp;
							bp = (void*) (leafhdr+1);

							INT_SET(leafhdr->info.forw, ARCH_CONVERT, 0);
							INT_SET(leafhdr->info.back, ARCH_CONVERT, 0);
							INT_SET(leafhdr->info.magic, ARCH_CONVERT, XFS_DIR2_LEAF1_MAGIC);
							INT_SET(leafhdr->info.pad, ARCH_CONVERT, 0);

							INT_SET(leafhdr->count, ARCH_CONVERT, count);
							INT_SET(leafhdr->stale, ARCH_CONVERT, stale);

							while (*p_hashlist)
							{
								uint32_t *hashval = (uint32_t*) bp;
								bp = (void*) (hashval+1);

								uint32_t *address = (uint32_t*) bp;
								bp = (void*) (address+1);

								ASSERT( ( (char*)bp - bbuf ) <= dirblocksize );
								ASSERT( (( (char*)bp - bbuf )%8) == 0 );

								INT_SET(*hashval, ARCH_CONVERT, (*p_hashlist)->hashval);
								INT_SET(*address, ARCH_CONVERT, (*p_hashlist)->address);

								p_hashlist = &(*p_hashlist)->next;
							}

							struct hashlist *l_hashlist = hashlist;
							while (l_hashlist)
							{
								struct hashlist *h = l_hashlist;

								l_hashlist = l_hashlist->next;
								free (h);
							}

							int32_t hole = dirblocksize - ((char*)bp - bbuf) - 4 - 2*blockcount;

							ASSERT(hole>=0);

							bp = (char*)bp + hole;

							for (j=0; j<blockcount; j++)
							{
								uint16_t *bestfree = (uint16_t*) bp;
								bp = (void*) (bestfree+1);

								INT_SET(*bestfree, ARCH_CONVERT, bests[j]);
							}

							uint32_t *bestcount = (uint32_t*) bp;
							bp = (void*) (bestcount+1);

							INT_SET(*bestcount, ARCH_CONVERT, blockcount);

							ASSERT( ( (char*)bp - bbuf ) == dirblocksize );

							if (!noaction)
								pwrite64(xfs_fd, bbuf, dirblocksize, b*blocksize);
							free (bbuf);
						}
					}
					else
					{
						if(verbose>=2)
							fprintf(stderr, _("single block directory (ulen=%d, leaflen=%d)\n"),
									ulen, leaflen);

						int err = add_dirblock (&dir_frags, 0);
						if (err)
						{
							fprintf(stderr, _("Cannot find free block for single block directory: Not enough space\n"));
							retval = -ENOSPC; exit(-retval);
						}

						struct any_file_fragment *frag = dir_frags.fr_frags;

						uint64_t b = frag[0].fr_start;
						char *bbuf = malloc(dirblocksize);
						if (!bbuf)
						{
							fprintf(stderr, _("Not enough memory\n"));
							{ retval = -ENOMEM; exit(-retval); }
						}
						memset(bbuf, 0, dirblocksize);

						void *bp = (void*) bbuf;
						xfs_dir2_data_hdr_t *hdr = (xfs_dir2_data_hdr_t *)bp;
						INT_SET(hdr->magic, ARCH_CONVERT, XFS_DIR2_BLOCK_MAGIC);

						int j;
						for (j=0; j<XFS_DIR2_DATA_FD_COUNT; j++)
						{
							INT_SET(hdr->bestfree[j].offset, ARCH_CONVERT, 0);
							INT_SET(hdr->bestfree[j].length, ARCH_CONVERT, 0);
						}

						bp = (void*) (hdr+1);
						struct hashlist *hashlist = NULL;
						struct hashlist **p_hashlist = &hashlist;

						uint32_t count = info->si_inode_table[i].i_info.dir->d_ndirents;
						uint32_t stale = 0;

						for (dirent = info->si_inode_table[i].i_info.dir->d_dirent; dirent; dirent = dirent->d_next)
						{
							void *bbp = bp;

							fill_direntry(bbuf, &bp, dirent, inode_map[dirent->d_inode]);

							add_to_hashlist(p_hashlist, 
									libxfs_da_hashname(dirent->d_name, strlen(dirent->d_name)), 
									((char*)bbp - bbuf)/8);
						}

						uint32_t unused_len = (dirblocksize - ((char*)bp - bbuf))/8 - count - 1;

						if ( unused_len>0 )
						{
							void *bbp = bp;

							fill_unused(bbuf, &bp, unused_len);

							INT_SET(hdr->bestfree[0].offset, ARCH_CONVERT, (char*)bbp - bbuf);
							INT_SET(hdr->bestfree[0].length, ARCH_CONVERT, unused_len*8);

							//stale++;
						}

						while (*p_hashlist)
						{
							uint32_t *hashval = (uint32_t*) bp;
							bp = (void*) (hashval+1);

							uint32_t *address = (uint32_t*) bp;
							bp = (void*) (address+1);

							ASSERT( ( (char*)bp - bbuf ) <= dirblocksize );
							ASSERT( (( (char*)bp - bbuf )%8) == 0 );

							INT_SET(*hashval, ARCH_CONVERT, (*p_hashlist)->hashval);
							INT_SET(*address, ARCH_CONVERT, (*p_hashlist)->address);

							p_hashlist = &(*p_hashlist)->next;
						}

						struct hashlist *l_hashlist = hashlist;
						while (l_hashlist)
						{
							struct hashlist *h = l_hashlist;

							l_hashlist = l_hashlist->next;
							free (h);
						}

						uint32_t *p_count = (uint32_t*) bp;
						bp = (void*) (p_count+1);

						uint32_t *p_stale = (uint32_t*) bp;
						bp = (void*) (p_stale+1);

						ASSERT( ( (char*)bp - bbuf ) == dirblocksize );

						INT_SET(*p_count, ARCH_CONVERT, count);
						INT_SET(*p_stale, ARCH_CONVERT, stale);

						if (!noaction)
							pwrite64(xfs_fd, bbuf, dirblocksize, b*blocksize);
						free (bbuf);
					}

					uint64_t nblocks = write_extentlist( 
							&dir_frags, 
							inode, 
							isize, 
							p_inode,
							countu,
							countleaf );
					free (dir_frags.fr_frags);

					INT_SET(inode->di_size, ARCH_CONVERT, nblocks*blocksize);
				}
				else
				{
					/*Short (local) directory*/
					if (verbose>=2)
						fprintf(stderr, _("Short (local) Directory\n"));

					uint16_t offset = 0x30;
					
					INT_SET(inode->di_format, ARCH_CONVERT, XFS_DINODE_FMT_LOCAL);
					INT_SET(inode->di_size, ARCH_CONVERT, size);

					INT_SET(inode->di_nblocks, ARCH_CONVERT, 0);
					INT_SET(inode->di_nextents, ARCH_CONVERT, 0);
					
				        xfs_dir2_sf_hdr_t *hdr = p_inode;
					void *entr = (char*) p_inode + 6 + (i8count?4:0);
					
					INT_SET(hdr->count, ARCH_CONVERT, info->si_inode_table[i].i_info.dir->d_ndirents-2);
					INT_SET(hdr->i8count, ARCH_CONVERT, i8count);
					if (!i8count)
						INT_SET(*(uint32_t*)&hdr->parent, ARCH_CONVERT, inode_map[parent]);
					else
						INT_SET(*(uint64_t*)&hdr->parent, ARCH_CONVERT, inode_map[parent]);

					for (dirent = info->si_inode_table[i].i_info.dir->d_dirent; dirent; dirent = dirent->d_next)
					{
						if ( strcmp(dirent->d_name, "..")==0 )
							continue;
						if ( strcmp(dirent->d_name, ".")==0 )
							continue;

						uint8_t *namelen = (uint8_t*) entr;
						INT_SET( *namelen, ARCH_CONVERT, strlen(dirent->d_name) );
						entr = (void*) (namelen+1);
						
						uint16_t *p_offset = (uint16_t*) entr;
						INT_SET( *p_offset, ARCH_CONVERT, offset);
						entr = (void*) (p_offset+1);
						
						memcpy(entr, dirent->d_name, strlen(dirent->d_name));
						entr = (void*) ((char*)entr + strlen(dirent->d_name));
						
						if (!i8count)
						{
							uint32_t *inumber = (uint32_t*) entr;
							INT_SET( *inumber, ARCH_CONVERT, inode_map[dirent->d_inode]);
							entr = (void*) (inumber+1);
						}
						else
						{
							uint64_t *inumber = (uint64_t*) entr;
							INT_SET( *inumber, ARCH_CONVERT, inode_map[dirent->d_inode]);
							entr = (void*) (inumber+1);
						}

						offset += (11 + strlen(dirent->d_name) +7)/8*8;

						ASSERT( ( (char*)entr - ibuf ) <= isize );
					}
					
				}
			} else
			{
				/*Device*/
				if (verbose>=2)
					fprintf(stderr, _("Device\n"));
				INT_SET(inode->di_format, ARCH_CONVERT, XFS_DINODE_FMT_DEV);
				unsigned long minor;
				unsigned long major;
				minor = info->si_inode_table[i].
					i_info.device & ( (1U<<20) - 1 );
				major = info->si_inode_table[i].
					i_info.device>>20;
				uint32_t dev = IRIX_MKDEV(major, minor);
				
				uint32_t *p_dev = p_inode;
				INT_SET(*p_dev, ARCH_CONVERT, dev);
			}

			if (!noaction)
				pwrite64(xfs_fd, ibuf, isize, ANYFS_INO(inode_map[i])*isize);
		}

		free(ibuf);

		progress_close(&progress);
	}

	if (verbose)
		printf (_("Filling free XFS inodes\n"));

	{
		char *ibuf = malloc(isize);
		if (!ibuf)
		{
			fprintf(stderr, _("Not enough memory\n"));
			retval = -ENOMEM; exit(-retval);
		}

		xfs_dinode_core_t *inode = NULL;

		memset(ibuf, 0, isize);

		inode = (xfs_dinode_core_t *)ibuf;

		INT_SET(inode->di_magic, ARCH_CONVERT, XFS_DINODE_MAGIC);
		INT_SET(inode->di_version, ARCH_CONVERT, 1);
		INT_SET(inode->di_format, ARCH_CONVERT, XFS_DINODE_FMT_EXTENTS);
		INT_SET(inode->di_aformat, ARCH_CONVERT, XFS_DINODE_FMT_EXTENTS);
			
		uint32_t *p_next_unlinked = (uint32_t*) (inode+1);
		INT_SET(*p_next_unlinked, ARCH_CONVERT, 0xFFFFFFFF);

		for (agno = 0; agno < agcount; agno++) {

			uint32_t ino_numrecs = agi_array[agno].agi_count/64;

			uint32_t freecount_val = agi_array[agno].agi_freecount;

			for (i = 0; i < freecount_val; i++) {
				uint32_t ino = agno * agsize * blocksize/isize +
				       	ANYFS_INO(inode_map[1]) + ino_numrecs*64 - 1 - i;

				if (!noaction)
					pwrite64(xfs_fd, ibuf, isize, ino*isize);
			}
		}

		free(ibuf);
	}

	{
		uint16_t inopblock = blocksize / isize;
		int log_ag = -1;
	        if (loginternal)
			log_ag = logstart >> xfs_agblklog;

		for (i=0; i<agcount; i++)
		{
			if (i==log_ag) continue;

			for (j=begresv; j<rbegresv; j++)
			{
				if ( (i*agsize + j)>dblocks ) break;
				clear_bit ( i*agsize + j, xfs_block_bitmap );
			}

			for (j=(rbegresv + agi_array[i].agi_count/inopblock); 
					j<(rbegresv + agi_array[i].agi_count/inopblock + yet_resv); j++)
			{
				if ( (i*agsize + j)>dblocks ) break;
				clear_bit ( i*agsize + j, xfs_block_bitmap );
			}
		}

		if (loginternal)
			for (i=logblocks; i<(logblocks + maxbtreelevel*2); i++)
				clear_bit ( ANYFS_BLKNO(logstart) + i, xfs_block_bitmap );
	}
	
	if (label)
		strncpy(sbp->sb_fname, label, sizeof(sbp->sb_fname));
	sbp->sb_magicnum = XFS_SB_MAGIC;
	sbp->sb_blocksize = blocksize;
	sbp->sb_dblocks = dblocks;
	sbp->sb_rblocks = rtblocks;
	sbp->sb_rextents = rtextents;
	platform_uuid_generate(&uuid);
	platform_uuid_copy(&sbp->sb_uuid, &uuid);
	sbp->sb_logstart = logstart;
	sbp->sb_rootino = sbp->sb_rbmino = sbp->sb_rsumino = NULLFSINO;
	sbp->sb_rextsize = rtextblocks;
	sbp->sb_agblocks = (xfs_agblock_t)agsize;
	sbp->sb_agcount = (xfs_agnumber_t)agcount;
	sbp->sb_rbmblocks = nbmblocks;
	sbp->sb_logblocks = (xfs_extlen_t)logblocks;
	sbp->sb_sectsize = (__uint16_t)sectorsize;
	sbp->sb_inodesize = (__uint16_t)isize;
	sbp->sb_inopblock = (__uint16_t)(blocksize / isize);
	sbp->sb_sectlog = (__uint8_t)sectorlog;
	sbp->sb_inodelog = (__uint8_t)inodelog;
	sbp->sb_inopblog = (__uint8_t)(blocklog - inodelog);
	sbp->sb_rextslog =
		(__uint8_t)(rtextents ?
			libxfs_highbit32((unsigned int)rtextents) : 0);
	sbp->sb_inprogress = 1;	/* mkfs is in progress */
	sbp->sb_imax_pct = imflag ? imaxpct : XFS_DFL_IMAXIMUM_PCT;
	sbp->sb_icount = 0;
	sbp->sb_ifree = 0;

	for (agno = 0; agno < agcount; agno++) {
		sbp->sb_icount += agi_array[agno].agi_count;
		sbp->sb_ifree += agi_array[agno].agi_freecount;
	}

	sbp->sb_fdblocks = dblocks - agcount * XFS_PREALLOC_BLOCKS(mp) -
		(loginternal ? logblocks : 0);

	sbp->sb_fdblocks = 0;

	uint64_t b;
	for (b = 0; b < dblocks; b++) {
		if ( !test_bit(b, xfs_block_bitmap) ) sbp->sb_fdblocks++;
	}

	sbp->sb_frextents = 0;	/* will do a free later */
	sbp->sb_uquotino = sbp->sb_gquotino = 0;
	sbp->sb_qflags = 0;
	sbp->sb_unit = dsunit;
	sbp->sb_width = dswidth;
	if (dirversion == 2)
		sbp->sb_dirblklog = dirblocklog - blocklog;
	if (logversion == 2) {	/* This is stored in bytes */
		lsunit = (lsunit == 0) ? 1 : XFS_FSB_TO_B(mp, lsunit);
		sbp->sb_logsunit = lsunit;
	} else
		sbp->sb_logsunit = 0;
	if (iaflag) {
		sbp->sb_inoalignmt = XFS_INODE_BIG_CLUSTER_SIZE >> blocklog;
		iaflag = sbp->sb_inoalignmt != 0;
	} else
		sbp->sb_inoalignmt = 0;
	if (lsectorsize != BBSIZE || sectorsize != BBSIZE) {
		sbp->sb_logsectlog = (__uint8_t)lsectorlog;
		sbp->sb_logsectsize = (__uint16_t)lsectorsize;
	} else {
		sbp->sb_logsectlog = 0;
		sbp->sb_logsectsize = 0;
	}
	sbp->sb_features2 = XFS_SB_VERSION2_MKFS(0, attrversion == 2, 0);
	sbp->sb_versionnum = XFS_SB_VERSION_MKFS(
			iaflag, dsunit != 0, extent_flagging,
			dirversion == 2, logversion == 2, attrversion == 1,
			(sectorsize != BBSIZE || lsectorsize != BBSIZE),
			sbp->sb_features2 != 0);

	/*
	 * Zero out the beginning of the device, to obliterate any old
	 * filesystem signatures out there.  This should take care of
	 * swap (somewhere around the page size), jfs (32k),
	 * ext[2,3] and reiserfs (64k) - and hopefully all else.
	 */
	for (i=0; i<min_t(unsigned, WHACK_SIZE/bsize, dblocks); i++)
	{
		if ( !test_bit(i, xfs_block_bitmap) )
		{
			buf = libxfs_getbuf(xi.ddev, XFS_FSB_TO_DADDR( mp, XFS_BLKNO(i) ), bsize);
			bzero(XFS_BUF_PTR(buf), bsize);
			if (!noaction) 
			{
				libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
				libxfs_purgebuf(buf);
			}
			else libxfs_putbuf(buf);
		}
	}

	/*
	 * If the data area is a file, then grow it out to its final size
	 * so that the reads for the end of the device in the mount code
	 * will succeed.
	 */
	if (xi.disfile && ftruncate64(xi.dfd, dblocks * blocksize) < 0) {
		fprintf(stderr, _("%s: Growing the data section failed\n"),
			progname);
		exit(1);
	}

	/*
	 * Zero out the end of the device, to obliterate any
	 * old MD RAID (or other) metadata at the end of the device.
 	 * (MD sb is ~64k from the end, take out a wider swath to be sure)
	 */
	if (!xi.disfile) {
		for (i=1; i<=min_t(unsigned, WHACK_SIZE/bsize, dblocks); i++)
		{
			if ( !test_bit(dblocks - i, xfs_block_bitmap) )
			{
				buf = libxfs_getbuf(xi.ddev, XFS_FSB_TO_DADDR( mp, XFS_BLKNO(dblocks - i) ), bsize);
				bzero(XFS_BUF_PTR(buf), bsize);
				if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
				else libxfs_putbuf(buf);
			}
		}
	}

	/*
	 * Zero the log if there is one.
	 */
	if (loginternal)
		xi.logdev = xi.ddev;
	if (xi.logdev && !noaction)
		libxfs_log_clear(xi.logdev, XFS_FSB_TO_DADDR(mp, logstart),
			(xfs_extlen_t)XFS_FSB_TO_BB(mp, logblocks),
			&sbp->sb_uuid, logversion, lsunit, XLOG_FMT);

	mp = libxfs_mount(mp, sbp, xi.ddev, xi.logdev, xi.rtdev, 1);
	if (mp == NULL) {
		fprintf(stderr, _("%s: filesystem failed to initialize\n"),
			progname);
		exit(1);
	}

	sbp->sb_rootino = inode_map[1];
	sbp->sb_rbmino = inode_map[1]+1;
	sbp->sb_rsumino = inode_map[1]+2;

	if (verbose)
		printf (_("Starting building XFS BTrees\n"));

	{
		struct progress_struct progress;

		if (qflag)
			memset(&progress, 0, sizeof(progress));
		else
			progress_init(&progress, _("building btrees at each AG: "),
					agcount);

		for (agno = 0; agno < agcount; agno++) {
			progress_update(&progress, agno);

			void *bp;
			int inolevel = 0;
			{
				/*
				 * INO btree root block
				 */

				uint32_t ino_numrecs = agi_array[agno].agi_count/64;

				int max_lv_recs_per_block = (blocksize-16)/16;
				int leaves = (ino_numrecs + max_lv_recs_per_block - 1)/max_lv_recs_per_block;

				int max_recs_per_block = (blocksize-16)/8;

				int height;
				int *links_at_level;
				int **nlinks;

				calc_links_at_levels(&links_at_level, &nlinks,
						&height, leaves, max_recs_per_block);
				inolevel = height+1;

				int pblocks = 0;

				if (height)
					for (j=0; j<=height-1; j++)
						pblocks += links_at_level[j];

				int blocks_for_tree = leaves + pblocks - 1;
				if (!leaves) blocks_for_tree = 0;

				sbp->sb_fdblocks -= blocks_for_tree;

				if (!height)
				{
					buf = libxfs_getbuf(mp->m_dev,
							XFS_AGB_TO_DADDR(mp, agno, XFS_IBT_BLOCK(mp)),
							bsize);
					block = XFS_BUF_TO_SBLOCK(buf);
					bzero(block, blocksize);
					INT_SET(block->bb_magic, ARCH_CONVERT, XFS_IBT_MAGIC);
					INT_SET(block->bb_level, ARCH_CONVERT, 0);
					INT_SET(block->bb_numrecs, ARCH_CONVERT, ino_numrecs);
					INT_SET(block->bb_leftsib, ARCH_CONVERT, NULLAGBLOCK);
					INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);

					bp = (void*) (block+1);

					for (b=0; b<ino_numrecs; b++)
					{
						uint32_t *start_ino = (uint32_t*) bp;
						bp = (void*) (start_ino+1);

						uint32_t *freecount = (uint32_t*) bp;
						bp = (void*) (freecount+1);

						uint64_t *free = (uint64_t*) bp;
						bp = (void*) (free+1);

						uint32_t freecount_val;
						uint64_t free_val;
						if (b==(ino_numrecs-1))
						{
							freecount_val = agi_array[agno].agi_freecount;
							free_val = ~(((uint64_t)1<<(64-agi_array[agno].agi_freecount))-1);
						}
						else
						{
							freecount_val = 0;
							free_val = 0;
						}

						INT_SET(*start_ino, ARCH_CONVERT, inode_map[1] + b*64);
						INT_SET(*freecount, ARCH_CONVERT, freecount_val);
						INT_SET(*free, ARCH_CONVERT, free_val);
					}

					if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
					else libxfs_putbuf(buf);
				}
				else
				{
					int bno = find_next_zero_bit(
							xfs_block_bitmap,
							dblocks,
							agno*agsize);
					if ( (bno - agno*agsize) >= agsize )
					{
						fprintf(stderr, _("Cannot find free block for INO btree: not enough space\n"));
						retval = -ENOSPC; exit(-retval);
					}
					set_bit ( bno, xfs_block_bitmap );

					int recs_at_block = (blocksize - 16)/16;

					int n = ino_numrecs;
					int recs_at_block_0 = floorf( (float)n/leaves );
					int recs_at_block_1 = recs_at_block_0+1;

					//int n0 = recs_at_block_0*leaves;
					int n1 = recs_at_block_1*leaves;

					int l0 = n1-n;
					/*int l1 = n-n0;*/

					int j=0;

					int recs_written = 0;
					int recs = (j<l0)?recs_at_block_0:recs_at_block_1;

					ASSERT(recs >= (recs_at_block/2));
					ASSERT(recs <= recs_at_block);

					buf = libxfs_getbuf(mp->m_dev,
							XFS_FSB_TO_DADDR( mp, XFS_BLKNO(bno) ),
							bsize);
					block = XFS_BUF_TO_SBLOCK(buf);
					bzero(block, blocksize);
					INT_SET(block->bb_magic, ARCH_CONVERT, XFS_IBT_MAGIC);
					INT_SET(block->bb_level, ARCH_CONVERT, 0);
					INT_SET(block->bb_numrecs, ARCH_CONVERT, recs);
					INT_SET(block->bb_leftsib, ARCH_CONVERT, NULLAGBLOCK);
					INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);

					struct hashlist *hashlist = NULL;
					struct hashlist **p_hashlist = &hashlist;

					int norec = 0;
					int count_leaves = 0;

					bp = (void*) (block+1);

					while (recs_written < ino_numrecs)
					{
						if (norec==recs)
						{
							uint32_t leftsib_val = bno%agsize;

							bno = find_next_zero_bit(
									xfs_block_bitmap,
									dblocks,
									agno*agsize);
							if ( (bno - agno*agsize) >= agsize )
							{
								fprintf(stderr, _("Cannot find free block for INO btree: not enough space\n"));
								retval = -ENOSPC; exit(-retval);
							}
							set_bit ( bno, xfs_block_bitmap );

							INT_SET(block->bb_rightsib, ARCH_CONVERT, bno%agsize);
							if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
							else libxfs_putbuf(buf);

							j++;
							recs = (j<l0)?recs_at_block_0:recs_at_block_1;

							ASSERT(recs >= (recs_at_block/2));
							ASSERT(recs <= recs_at_block);

							buf = libxfs_getbuf(mp->m_dev,
									XFS_FSB_TO_DADDR( mp, XFS_BLKNO(bno) ),
									bsize);
							block = XFS_BUF_TO_SBLOCK(buf);
							bzero(block, blocksize);
							INT_SET(block->bb_magic, ARCH_CONVERT, XFS_IBT_MAGIC);
							INT_SET(block->bb_level, ARCH_CONVERT, 0);
							INT_SET(block->bb_numrecs, ARCH_CONVERT, recs);
							INT_SET(block->bb_leftsib, ARCH_CONVERT, leftsib_val);
							INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);

							norec = 0;

							bp = (void*) (block+1);
						}

						if (!norec)
						{
							add_to_hashlist(p_hashlist, 
									inode_map[1] + recs_written*64,
									bno%agsize);
							count_leaves++;
						}

						uint32_t *start_ino = (uint32_t*) bp;
						bp = (void*) (start_ino+1);

						uint32_t *freecount = (uint32_t*) bp;
						bp = (void*) (freecount+1);

						uint64_t *free = (uint64_t*) bp;
						bp = (void*) (free+1);

						uint32_t freecount_val;
						uint64_t free_val;
						if (recs_written==(ino_numrecs-1))
						{
							freecount_val = agi_array[agno].agi_freecount;
							if ( !freecount_val ) free_val = 0;
							else free_val = ~(((uint64_t)1<<(64 - 
											agi_array[agno].agi_freecount))-1);
						}
						else
						{
							freecount_val = 0;
							free_val = 0;
						}

						INT_SET(*start_ino, ARCH_CONVERT, inode_map[1] + recs_written*64);
						INT_SET(*freecount, ARCH_CONVERT, freecount_val);
						INT_SET(*free, ARCH_CONVERT, free_val);

						recs_written++;
						norec++;
					}

					ASSERT(ino_numrecs==recs_written);
					ASSERT(norec==recs);
					ASSERT(count_leaves==leaves);

					if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
					else libxfs_putbuf(buf);

					if (height>1)
					{
						bno = find_next_zero_bit(
								xfs_block_bitmap,
								dblocks,
								agno*agsize);
						if ( (bno - agno*agsize) >= agsize )
						{
							fprintf(stderr, _("Cannot find free block for INO btree: not enough space\n"));
							retval = -ENOSPC; exit(-retval);
						}
						set_bit ( bno, xfs_block_bitmap );
					}
					else
						bno = agno*agsize + XFS_IBT_BLOCK(mp);

					buf = libxfs_getbuf(mp->m_dev,
							XFS_FSB_TO_DADDR( mp, XFS_BLKNO(bno) ),
							bsize);
					block = XFS_BUF_TO_SBLOCK(buf);
					bzero(block, blocksize);
					INT_SET(block->bb_magic, ARCH_CONVERT, XFS_IBT_MAGIC);
					INT_SET(block->bb_level, ARCH_CONVERT, 1);
					INT_SET(block->bb_numrecs, ARCH_CONVERT, nlinks[height][0]);
					INT_SET(block->bb_leftsib, ARCH_CONVERT, NULLAGBLOCK);
					INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);

					bp = (void*) (block+1);

					int space = blocksize - sizeof(*block);
					int space_for_use = space/8*8;
					int space_2 = space_for_use/2;

					void* bp2 = (char*) bp + space_2;

					int l;
					for (l=height; l>0; l--)
					{
						struct hashlist *hashlist_z = NULL;
						struct hashlist **p_hashlist_z = &hashlist_z;

						int j;
						for (j=0; j<links_at_level[l-1]; j++)
						{
							uint32_t n_hashval=0;

							int k;
							for (k=0; k<nlinks[l][j]; k++)
							{
								ASSERT(*p_hashlist);

								uint32_t *startino = (uint32_t*) bp;
								bp = (void*) (startino+1);

								uint32_t *ptr = (uint32_t*) bp2;
								bp2 = (void*) (ptr+1);

								ASSERT( ( (char*)bp - (char*)block ) <= space_2+sizeof(*block) );

								//printf("IBT_NODE [%d/%d] from %d, %d blocks, ptr=%d\n", k, nlinks[l][j],
								//		(*p_hashlist)->hashval, (*p_hashlist)->address,
								//		(*p_hashlist)->data);

								INT_SET(*startino, ARCH_CONVERT, (*p_hashlist)->hashval);
								INT_SET(*ptr, ARCH_CONVERT, (*p_hashlist)->address);

								if ((k+1)==nlinks[l][j])
								{
									n_hashval = (*p_hashlist)->hashval;
								}

								p_hashlist = &(*p_hashlist)->next;
							}

							add_to_hashlist(p_hashlist_z, 
									n_hashval, 
									bno%agsize);

							uint32_t backw=bno%agsize;
							if (!j) backw=0;

							if (l>1 || j<(links_at_level[l-1]-1))
							{
								if (!(l==2 && j==(links_at_level[l-1]-1)  ))
								{
									bno = find_next_zero_bit(
											xfs_block_bitmap,
											dblocks,
											agno*agsize);
									if ( (bno - agno*agsize) >= agsize )
									{
										fprintf(stderr, _("Cannot find free block for INO btree: not enough space\n"));
										retval = -ENOSPC; exit(-retval);
									}
									set_bit ( bno, xfs_block_bitmap );
								}
								else
									bno = agno*agsize + XFS_IBT_BLOCK(mp);

								INT_SET(block->bb_rightsib, ARCH_CONVERT, bno%agsize);
							}

							if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
							else libxfs_putbuf(buf);

							if (l>1 || j<(links_at_level[l-1]-1))
							{

								buf = libxfs_getbuf(mp->m_dev,
										XFS_FSB_TO_DADDR( mp, XFS_BLKNO(bno) ),
										bsize);
								block = XFS_BUF_TO_SBLOCK(buf);
								bzero(block, blocksize);

								bp = (void*) (block+1);

								bp2 = (char*) bp + space_2;

								int bb_numrecs = ( j<(links_at_level[l-1]-1) ) ? nlinks[l][j+1] :
									nlinks[l-1][0];
								int bb_level = ( j<(links_at_level[l-1]-1) ) ? height+1-l :
									height+1-l + 1;

								INT_SET(block->bb_magic, ARCH_CONVERT, XFS_IBT_MAGIC);
								INT_SET(block->bb_level, ARCH_CONVERT, bb_level);
								INT_SET(block->bb_numrecs, ARCH_CONVERT, bb_numrecs);
								INT_SET(block->bb_leftsib, ARCH_CONVERT, backw);
								INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);
							}
						}

						ASSERT(!(*p_hashlist));

						struct hashlist *l_hashlist = hashlist;
						while (l_hashlist)
						{
							struct hashlist *h = l_hashlist;

							l_hashlist = l_hashlist->next;
							free (h);
						}

						hashlist = hashlist_z;
						p_hashlist = &hashlist;
					}
				}
			}

			/*
			 * Counting level of btrees of free blocks
			 */

			uint32_t num_blocks = 0;

			uint32_t b;
			uint32_t start = 0;
			uint32_t end = 0xFFFFFFFE;

			for (b = 0; b <= agsize; b++) {
				if ( b<agsize && !test_bit(b+agno*agsize, xfs_block_bitmap) )
				{
					if (end!=(b-1))
						start = b;
					end = b;
				}
				else
				{
					if (end==(b-1))
						num_blocks++;
				}
			}

			int max_lv_recs_per_block = (blocksize-16)/8;
			int leaves = (num_blocks + max_lv_recs_per_block - 1)/max_lv_recs_per_block;

			int max_recs_per_block = (blocksize-16)/12;

			int height;
			int *links_at_level;
			int **nlinks;

			calc_links_at_levels(&links_at_level, &nlinks,
					&height, leaves, max_recs_per_block);

			int pblocks = 0;

			if (height)
				for (j=0; j<=height-1; j++)
					pblocks += links_at_level[j];

			int blocks_for_trees = (leaves + pblocks - 1)*2;
			if (!leaves) blocks_for_trees = 0;
			//printf("agno %d\n", agno);
			//printf("%d = (%d+%d-1)*2\n", blocks_for_trees, leaves, pblocks);

			num_blocks = 0;

			uint32_t freeblks = 0;
			uint32_t longest = 0;

			struct hashlist *reserv_for_tree = NULL;
			struct hashlist *bno_list = NULL;
			struct hashlist *cnum_list = NULL;

			struct hashlist **preserv_for_tree = &reserv_for_tree;
			struct hashlist **pbno_list = &bno_list;
			struct hashlist **pcnum_list = &cnum_list;

			int unreserved = blocks_for_trees;

			start = 0;
			end = 0xFFFFFFFE;

			for (b = 0; b <= agsize; b++) {
				if ( b<agsize && !test_bit(b+agno*agsize, xfs_block_bitmap) )
				{
					if (end!=(b-1))
						start = b;
					end = b;
					if (unreserved)
					{
						if ( (end-start+1)==unreserved )
						{
							add_to_hashlist(preserv_for_tree,
									start,
									end-start+1);
							unreserved = 0;
							end = b-1;
						}
					}
					else
						freeblks++;
				}
				else
				{
					if (end==(b-1))
					{
						if (unreserved)
						{
							add_to_hashlist(preserv_for_tree,
									start,
									end-start+1);
							unreserved -= end-start+1;

							add_to_hashlist(pcnum_list,
									0,
									start);
							/*FIXME: do adding to the end*/
							add_to_hashlist(pbno_list,
									start,
									0);
							num_blocks++;
						}
						else
						{
							add_to_hashlist(pcnum_list,
									end-start+1,
									start);
							/*FIXME: do adding to the end*/
							add_to_hashlist(pbno_list,
									start,
									end-start+1);
							num_blocks++;

							if ( (end-start+1)>longest )
								longest = end-start+1;
						}
					}
				}
			}

			/*
			 * AG header block: freespace
			 */
			buf = libxfs_getbuf(mp->m_dev,
					XFS_AG_DADDR(mp, agno, XFS_AGF_DADDR(mp)),
					XFS_FSS_TO_BB(mp, 1));
			agf = XFS_BUF_TO_AGF(buf);
			bzero(agf, sectorsize);
			if (agno == agcount - 1)
				agsize = dblocks - (xfs_drfsbno_t)(agno * agsize);
			INT_SET(agf->agf_magicnum, ARCH_CONVERT, XFS_AGF_MAGIC);
			INT_SET(agf->agf_versionnum, ARCH_CONVERT, XFS_AGF_VERSION);
			INT_SET(agf->agf_seqno, ARCH_CONVERT, agno);
			INT_SET(agf->agf_length, ARCH_CONVERT, (xfs_agblock_t)agsize);
			INT_SET(agf->agf_roots[XFS_BTNUM_BNOi], ARCH_CONVERT,
					XFS_BNO_BLOCK(mp));
			INT_SET(agf->agf_roots[XFS_BTNUM_CNTi], ARCH_CONVERT,
					XFS_CNT_BLOCK(mp));
			INT_SET(agf->agf_levels[XFS_BTNUM_BNOi], ARCH_CONVERT, height+1);
			INT_SET(agf->agf_levels[XFS_BTNUM_CNTi], ARCH_CONVERT, height+1);
			INT_SET(agf->agf_flfirst, ARCH_CONVERT, 0);
			INT_SET(agf->agf_fllast, ARCH_CONVERT, XFS_AGFL_SIZE(mp) - 1);
			INT_SET(agf->agf_flcount, ARCH_CONVERT, 0);
			nbmblocks = (xfs_extlen_t)(agsize - XFS_PREALLOC_BLOCKS(mp));
			INT_SET(agf->agf_freeblks, ARCH_CONVERT, freeblks);
			INT_SET(agf->agf_longest, ARCH_CONVERT, longest);

			if (XFS_MIN_FREELIST(agf, mp) > worst_freelist)
				worst_freelist = XFS_MIN_FREELIST(agf, mp);
			if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
			else libxfs_putbuf(buf);

			/*
			 * AG header block: inodes
			 */
			buf = libxfs_getbuf(mp->m_dev,
					XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR(mp)),
					XFS_FSS_TO_BB(mp, 1));
			agi = XFS_BUF_TO_AGI(buf);
			bzero(agi, sectorsize);
			INT_SET(agi->agi_magicnum, ARCH_CONVERT, XFS_AGI_MAGIC);
			INT_SET(agi->agi_versionnum, ARCH_CONVERT, XFS_AGI_VERSION);
			INT_SET(agi->agi_seqno, ARCH_CONVERT, agno);
			INT_SET(agi->agi_length, ARCH_CONVERT, (xfs_agblock_t)agsize);
			INT_SET(agi->agi_count, ARCH_CONVERT, agi_array[agno].agi_count);
			INT_SET(agi->agi_root, ARCH_CONVERT, XFS_IBT_BLOCK(mp));
			INT_SET(agi->agi_level, ARCH_CONVERT, inolevel);
			INT_SET(agi->agi_freecount, ARCH_CONVERT, agi_array[agno].agi_freecount);
			INT_SET(agi->agi_newino, ARCH_CONVERT, NULLAGINO);
			INT_SET(agi->agi_dirino, ARCH_CONVERT, NULLAGINO);
			for (c = 0; c < XFS_AGI_UNLINKED_BUCKETS; c++)
				INT_SET(agi->agi_unlinked[c], ARCH_CONVERT, NULLAGINO);
			if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
			else libxfs_putbuf(buf);

			/*
			 * BNO btree root block
			 */
			if (!blocks_for_trees)
			{
				buf = libxfs_getbuf(mp->m_dev,
						XFS_AGB_TO_DADDR(mp, agno, XFS_BNO_BLOCK(mp)),
						bsize);
				block = XFS_BUF_TO_SBLOCK(buf);
				bzero(block, blocksize);
				INT_SET(block->bb_magic, ARCH_CONVERT, XFS_ABTB_MAGIC);
				INT_SET(block->bb_level, ARCH_CONVERT, 0);
				INT_SET(block->bb_numrecs, ARCH_CONVERT, num_blocks);
				INT_SET(block->bb_leftsib, ARCH_CONVERT, NULLAGBLOCK);
				INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);

				void *bp = (void*) (block+1);

				while (*pbno_list)
				{
					uint32_t *startblock = (uint32_t*) bp;
					bp = (void*) (startblock+1);

					uint32_t *blockcount = (uint32_t*) bp;
					bp = (void*) (blockcount+1);

					ASSERT( ( (char*)bp - (char*)block ) <= blocksize );

					INT_SET(*startblock, ARCH_CONVERT, (*pbno_list)->hashval);
					INT_SET(*blockcount, ARCH_CONVERT, (*pbno_list)->address);

					pbno_list = &(*pbno_list)->next;
				}

				if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
				else libxfs_putbuf(buf);
			}
			else
			{
				preserv_for_tree = &reserv_for_tree;
				int bno = (*preserv_for_tree)->hashval;

				/*
				 * FIXME:
				 * Unreadable code:
				 * here hashval is start of block
				 * address -- size of free fragment
				 */
				(*preserv_for_tree)->hashval++;
				(*preserv_for_tree)->address--;

				if (!(*preserv_for_tree)->address)
				{
					struct hashlist *h = (*preserv_for_tree)->next;
					free(*preserv_for_tree);
					(*preserv_for_tree)=h;
				}

				int recs_at_block = (blocksize - 16)/8;

				int n = num_blocks;
				int recs_at_block_0 = floorf( (float)n/leaves );
				int recs_at_block_1 = recs_at_block_0+1;

				ASSERT(recs_at_block_0 >= (recs_at_block/2));
				ASSERT(recs_at_block_1 <= recs_at_block);

				//int n0 = recs_at_block_0*leaves;
				int n1 = recs_at_block_1*leaves;

				int l0 = n1-n;
				/*int l1 = n-n0;*/

				int j=0;

				int recs_written = 0;
				int recs = (j<l0)?recs_at_block_0:recs_at_block_1;

				buf = libxfs_getbuf(mp->m_dev,
						XFS_FSB_TO_DADDR( mp, bno + (agno<<xfs_agblklog) ),
						bsize);
				block = XFS_BUF_TO_SBLOCK(buf);
				bzero(block, blocksize);
				INT_SET(block->bb_magic, ARCH_CONVERT, XFS_ABTB_MAGIC);
				INT_SET(block->bb_level, ARCH_CONVERT, 0);
				INT_SET(block->bb_numrecs, ARCH_CONVERT, recs);
				INT_SET(block->bb_leftsib, ARCH_CONVERT, NULLAGBLOCK);
				INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);

				struct hashlist *hashlist = NULL;
				struct hashlist **p_hashlist = &hashlist;

				int norec = 0;
				int count_leaves = 0;

				bp = (void*) (block+1);

				while (*pbno_list)
				{
					if (norec==recs)
					{
						uint32_t leftsib_val = bno;

						bno = (*preserv_for_tree)->hashval;

						(*preserv_for_tree)->hashval++;
						(*preserv_for_tree)->address--;

						if (!(*preserv_for_tree)->address)
						{
							struct hashlist *h = (*preserv_for_tree)->next;
							free(*preserv_for_tree);
							(*preserv_for_tree)=h;
						}

						INT_SET(block->bb_rightsib, ARCH_CONVERT, bno);
						if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
						else libxfs_putbuf(buf);

						j++;
						recs = (j<l0)?recs_at_block_0:recs_at_block_1;

						buf = libxfs_getbuf(mp->m_dev,
								XFS_FSB_TO_DADDR( mp, bno + (agno<<xfs_agblklog) ),
								bsize);
						block = XFS_BUF_TO_SBLOCK(buf);
						bzero(block, blocksize);
						INT_SET(block->bb_magic, ARCH_CONVERT, XFS_ABTB_MAGIC);
						INT_SET(block->bb_level, ARCH_CONVERT, 0);
						INT_SET(block->bb_numrecs, ARCH_CONVERT, recs);
						INT_SET(block->bb_leftsib, ARCH_CONVERT, leftsib_val);
						INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);

						norec = 0;

						bp = (void*) (block+1);
					}

					if (!norec)
					{
						add_to_hashlist_data(p_hashlist, 
								(*pbno_list)->hashval,
								(*pbno_list)->address,
								bno);
						count_leaves++;
					}

					uint32_t *startblock = (uint32_t*) bp;
					bp = (void*) (startblock+1);

					uint32_t *blockcount = (uint32_t*) bp;
					bp = (void*) (blockcount+1);

					ASSERT( ( (char*)bp - (char*)block ) <= blocksize );

					//printf("BNO [%d] from %d, %d blocks\n", norec,
					//	       	(*pbno_list)->hashval, (*pbno_list)->address);
					INT_SET(*startblock, ARCH_CONVERT, (*pbno_list)->hashval);
					INT_SET(*blockcount, ARCH_CONVERT, (*pbno_list)->address);

					pbno_list = &(*pbno_list)->next;

					recs_written++;
					norec++;
				}

				ASSERT(num_blocks==recs_written);
				ASSERT(norec==recs);
				ASSERT(count_leaves==leaves);

				if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
				else libxfs_putbuf(buf);

				if (height>1)
				{
					ASSERT(*preserv_for_tree);
					bno = (*preserv_for_tree)->hashval;

					(*preserv_for_tree)->hashval++;
					(*preserv_for_tree)->address--;

					if (!(*preserv_for_tree)->address)
					{
						struct hashlist *h = (*preserv_for_tree)->next;
						free(*preserv_for_tree);
						(*preserv_for_tree)=h;
					}
				}
				else
					bno = XFS_BNO_BLOCK(mp);

				buf = libxfs_getbuf(mp->m_dev,
						XFS_FSB_TO_DADDR( mp, bno + (agno<<xfs_agblklog) ),
						bsize);
				block = XFS_BUF_TO_SBLOCK(buf);
				bzero(block, blocksize);
				INT_SET(block->bb_magic, ARCH_CONVERT, XFS_ABTB_MAGIC);
				INT_SET(block->bb_level, ARCH_CONVERT, 1);
				INT_SET(block->bb_numrecs, ARCH_CONVERT, nlinks[height][0]);
				INT_SET(block->bb_leftsib, ARCH_CONVERT, NULLAGBLOCK);
				INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);

				bp = (void*) (block+1);

				int space = blocksize - sizeof(*block);
				int space_for_use = space/12*12;
				int space_2_3 = space_for_use/3*2;

				void* bp2 = (char*) bp + space_2_3;

				int l;
				for (l=height; l>0; l--)
				{
					struct hashlist *hashlist_z = NULL;
					struct hashlist **p_hashlist_z = &hashlist_z;

					int j;
					for (j=0; j<links_at_level[l-1]; j++)
					{
						uint32_t n_hashval=0;
						uint32_t n_address=0;

						int k;
						for (k=0; k<nlinks[l][j]; k++)
						{
							ASSERT(*p_hashlist);

							uint32_t *startblock = (uint32_t*) bp;
							bp = (void*) (startblock+1);

							uint32_t *blockcount = (uint32_t*) bp;
							bp = (void*) (blockcount+1);

							uint32_t *ptr = (uint32_t*) bp2;
							bp2 = (void*) (ptr+1);

							ASSERT( ( (char*)bp - (char*)block ) <= space_2_3+sizeof(*block) );

							//printf("BNO_NODE [%d/%d] from %d, %d blocks, ptr=%d\n", k, nlinks[l][j],
							//		(*p_hashlist)->hashval, (*p_hashlist)->address,
							//		(*p_hashlist)->data);

							INT_SET(*startblock, ARCH_CONVERT, (*p_hashlist)->hashval);
							INT_SET(*blockcount, ARCH_CONVERT, (*p_hashlist)->address);
							INT_SET(*ptr, ARCH_CONVERT, (*p_hashlist)->data);

							if ((k+1)==nlinks[l][j])
							{
								n_hashval = (*p_hashlist)->hashval;
								n_address = (*p_hashlist)->address;
							}

							p_hashlist = &(*p_hashlist)->next;
						}

						add_to_hashlist_data(p_hashlist_z, 
								n_hashval, 
								n_address,
								bno);

						uint32_t backw=bno;
						if (!j) backw=0;

						if (l>1 || j<(links_at_level[l-1]-1))
						{
							if (!(l==2 && j==(links_at_level[l-1]-1)  ))
							{
								bno = (*preserv_for_tree)->hashval;

								(*preserv_for_tree)->hashval++;
								(*preserv_for_tree)->address--;

								if (!(*preserv_for_tree)->address)
								{
									struct hashlist *h = (*preserv_for_tree)->next;
									free(*preserv_for_tree);
									(*preserv_for_tree)=h;
								}
							}
							else
								bno = XFS_BNO_BLOCK(mp);

							INT_SET(block->bb_rightsib, ARCH_CONVERT, bno);
						}

						if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
						else libxfs_putbuf(buf);

						if (l>1 || j<(links_at_level[l-1]-1))
						{

							buf = libxfs_getbuf(mp->m_dev,
									XFS_FSB_TO_DADDR( mp, bno + (agno<<xfs_agblklog) ),
									bsize);
							block = XFS_BUF_TO_SBLOCK(buf);
							bzero(block, blocksize);

							bp = (void*) (block+1);

							bp2 = (char*) bp + space_2_3;

							int bb_numrecs = ( j<(links_at_level[l-1]-1) ) ? nlinks[l][j+1] :
								nlinks[l-1][0];
							int bb_level = ( j<(links_at_level[l-1]-1) ) ? height+1-l :
								height+1-l + 1;

							INT_SET(block->bb_magic, ARCH_CONVERT, XFS_ABTB_MAGIC);
							INT_SET(block->bb_level, ARCH_CONVERT, bb_level);
							INT_SET(block->bb_numrecs, ARCH_CONVERT, bb_numrecs);
							INT_SET(block->bb_leftsib, ARCH_CONVERT, backw);
							INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);
						}
					}

					ASSERT(!(*p_hashlist));

					struct hashlist *l_hashlist = hashlist;
					while (l_hashlist)
					{
						struct hashlist *h = l_hashlist;

						l_hashlist = l_hashlist->next;
						free (h);
					}

					hashlist = hashlist_z;
					p_hashlist = &hashlist;
				}
			}

			struct hashlist *l_hashlist = bno_list;
			while (l_hashlist)
			{
				struct hashlist *h = l_hashlist;

				l_hashlist = l_hashlist->next;
				free (h);
			}

			/*
			 * CNT btree root block
			 */
			if (!blocks_for_trees)
			{
				buf = libxfs_getbuf(mp->m_dev,
						XFS_AGB_TO_DADDR(mp, agno, XFS_CNT_BLOCK(mp)),
						bsize);
				block = XFS_BUF_TO_SBLOCK(buf);
				bzero(block, blocksize);
				INT_SET(block->bb_magic, ARCH_CONVERT, XFS_ABTC_MAGIC);
				INT_SET(block->bb_level, ARCH_CONVERT, 0);
				INT_SET(block->bb_numrecs, ARCH_CONVERT, num_blocks);
				INT_SET(block->bb_leftsib, ARCH_CONVERT, NULLAGBLOCK);
				INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);

				bp = (void*) (block+1);

				while (*pcnum_list)
				{
					uint32_t *startblock = (uint32_t*) bp;
					bp = (void*) (startblock+1);

					uint32_t *blockcount = (uint32_t*) bp;
					bp = (void*) (blockcount+1);

					ASSERT( ( (char*)bp - (char*)block ) <= blocksize );

					INT_SET(*startblock, ARCH_CONVERT, (*pcnum_list)->address);
					INT_SET(*blockcount, ARCH_CONVERT, (*pcnum_list)->hashval);

					pcnum_list = &(*pcnum_list)->next;
				}

				if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
				else libxfs_putbuf(buf);
			}
			else
			{
				preserv_for_tree = &reserv_for_tree;
				int bno = (*preserv_for_tree)->hashval;

				/*
				 * FIXME:
				 * Unreadable code:
				 * here hashval is start of block
				 * address -- size of free fragment
				 */
				(*preserv_for_tree)->hashval++;
				(*preserv_for_tree)->address--;

				if (!(*preserv_for_tree)->address)
				{
					struct hashlist *h = (*preserv_for_tree)->next;
					free(*preserv_for_tree);
					(*preserv_for_tree)=h;
				}

				int recs_at_block = (blocksize - 16)/8;

				int n = num_blocks;
				int recs_at_block_0 = floorf( (float)n/leaves );
				int recs_at_block_1 = recs_at_block_0+1;

				ASSERT(recs_at_block_0 >= (recs_at_block/2));
				ASSERT(recs_at_block_1 <= recs_at_block);

				//int n0 = recs_at_block_0*leaves;
				int n1 = recs_at_block_1*leaves;

				int l0 = n1-n;
				/*int l1 = n-n0;*/

				int j=0;

				int recs_written = 0;
				int recs = (j<l0)?recs_at_block_0:recs_at_block_1;

				buf = libxfs_getbuf(mp->m_dev,
						XFS_FSB_TO_DADDR( mp, bno + (agno<<xfs_agblklog) ),
						bsize);
				block = XFS_BUF_TO_SBLOCK(buf);
				bzero(block, blocksize);
				INT_SET(block->bb_magic, ARCH_CONVERT, XFS_ABTC_MAGIC);
				INT_SET(block->bb_level, ARCH_CONVERT, 0);
				INT_SET(block->bb_numrecs, ARCH_CONVERT, recs);
				INT_SET(block->bb_leftsib, ARCH_CONVERT, NULLAGBLOCK);
				INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);

				struct hashlist *hashlist = NULL;
				struct hashlist **p_hashlist = &hashlist;

				int norec = 0;
				int count_leaves = 0;

				bp = (void*) (block+1);

				while (*pcnum_list)
				{
					if (norec==recs)
					{
						uint32_t leftsib_val = bno;

						bno = (*preserv_for_tree)->hashval;

						(*preserv_for_tree)->hashval++;
						(*preserv_for_tree)->address--;

						if (!(*preserv_for_tree)->address)
						{
							struct hashlist *h = (*preserv_for_tree)->next;
							free(*preserv_for_tree);
							(*preserv_for_tree)=h;
						}

						INT_SET(block->bb_rightsib, ARCH_CONVERT, bno);
						if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
						else libxfs_putbuf(buf);

						j++;
						recs = (j<l0)?recs_at_block_0:recs_at_block_1;

						buf = libxfs_getbuf(mp->m_dev,
								XFS_FSB_TO_DADDR( mp, bno + (agno<<xfs_agblklog) ),
								bsize);
						block = XFS_BUF_TO_SBLOCK(buf);
						bzero(block, blocksize);
						INT_SET(block->bb_magic, ARCH_CONVERT, XFS_ABTC_MAGIC);
						INT_SET(block->bb_level, ARCH_CONVERT, 0);
						INT_SET(block->bb_numrecs, ARCH_CONVERT, recs);
						INT_SET(block->bb_leftsib, ARCH_CONVERT, leftsib_val);
						INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);

						norec = 0;

						bp = (void*) (block+1);
					}

					if (!norec)
					{
						add_to_hashlist_data(p_hashlist, 
								(*pcnum_list)->hashval,
								(*pcnum_list)->address,
								bno);
						count_leaves++;
					}

					uint32_t *startblock = (uint32_t*) bp;
					bp = (void*) (startblock+1);

					uint32_t *blockcount = (uint32_t*) bp;
					bp = (void*) (blockcount+1);

					ASSERT( ( (char*)bp - (char*)block ) <= blocksize );

					//printf("CNT [%d] from %d, %d blocks\n", norec,
					//	       	(*pcnum_list)->address, (*pcnum_list)->hashval);
					INT_SET(*startblock, ARCH_CONVERT, (*pcnum_list)->address);
					INT_SET(*blockcount, ARCH_CONVERT, (*pcnum_list)->hashval);

					pcnum_list = &(*pcnum_list)->next;

					recs_written++;
					norec++;
				}

				ASSERT(num_blocks==recs_written);
				ASSERT(norec==recs);
				ASSERT(count_leaves==leaves);

				if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
				else libxfs_putbuf(buf);

				if (height>1)
				{
					ASSERT(*preserv_for_tree);
					bno = (*preserv_for_tree)->hashval;

					(*preserv_for_tree)->hashval++;
					(*preserv_for_tree)->address--;

					if (!(*preserv_for_tree)->address)
					{
						struct hashlist *h = (*preserv_for_tree)->next;
						free(*preserv_for_tree);
						(*preserv_for_tree)=h;
					}
				}
				else
					bno = XFS_CNT_BLOCK(mp);

				buf = libxfs_getbuf(mp->m_dev,
						XFS_FSB_TO_DADDR( mp, bno + (agno<<xfs_agblklog) ),
						bsize);
				block = XFS_BUF_TO_SBLOCK(buf);
				bzero(block, blocksize);
				INT_SET(block->bb_magic, ARCH_CONVERT, XFS_ABTC_MAGIC);
				INT_SET(block->bb_level, ARCH_CONVERT, 1);
				INT_SET(block->bb_numrecs, ARCH_CONVERT, nlinks[height][0]);
				INT_SET(block->bb_leftsib, ARCH_CONVERT, NULLAGBLOCK);
				INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);

				bp = (void*) (block+1);

				int space = blocksize - sizeof(*block);
				int space_for_use = space/12*12;
				int space_2_3 = space_for_use/3*2;

				void* bp2 = (char*) bp + space_2_3;

				int l;
				for (l=height; l>0; l--)
				{
					struct hashlist *hashlist_z = NULL;
					struct hashlist **p_hashlist_z = &hashlist_z;

					int j;
					for (j=0; j<links_at_level[l-1]; j++)
					{
						uint32_t n_hashval=0;
						uint32_t n_address=0;

						int k;
						for (k=0; k<nlinks[l][j]; k++)
						{
							ASSERT(*p_hashlist);

							uint32_t *startblock = (uint32_t*) bp;
							bp = (void*) (startblock+1);

							uint32_t *blockcount = (uint32_t*) bp;
							bp = (void*) (blockcount+1);

							uint32_t *ptr = (uint32_t*) bp2;
							bp2 = (void*) (ptr+1);

							ASSERT( ( (char*)bp - (char*)block ) <= space_2_3+sizeof(*block) );

							INT_SET(*blockcount, ARCH_CONVERT, (*p_hashlist)->hashval);
							INT_SET(*startblock, ARCH_CONVERT, (*p_hashlist)->address);
							INT_SET(*ptr, ARCH_CONVERT, (*p_hashlist)->data);

							if ((k+1)==nlinks[l][j])
							{
								n_hashval = (*p_hashlist)->hashval;
								n_address = (*p_hashlist)->address;
							}

							p_hashlist = &(*p_hashlist)->next;
						}

						add_to_hashlist_data(p_hashlist_z, 
								n_hashval, 
								n_address,
								bno);

						uint32_t backw=bno;
						if (!j) backw=0;

						if (l>1 || j<(links_at_level[l-1]-1))
						{
							if (!(l==2 && j==(links_at_level[l-1]-1)  ))
							{
								bno = (*preserv_for_tree)->hashval;

								(*preserv_for_tree)->hashval++;
								(*preserv_for_tree)->address--;

								if (!(*preserv_for_tree)->address)
								{
									struct hashlist *h = (*preserv_for_tree)->next;
									free(*preserv_for_tree);
									(*preserv_for_tree)=h;
								}
							}
							else
								bno = XFS_CNT_BLOCK(mp);

							INT_SET(block->bb_rightsib, ARCH_CONVERT, bno);
						}

						if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
						else libxfs_putbuf(buf);

						if (l>1 || j<(links_at_level[l-1]-1))
						{

							buf = libxfs_getbuf(mp->m_dev,
									XFS_FSB_TO_DADDR( mp, bno + (agno<<xfs_agblklog) ),
									bsize);
							block = XFS_BUF_TO_SBLOCK(buf);
							bzero(block, blocksize);

							bp = (void*) (block+1);

							bp2 = (char*) bp + space_2_3;

							int bb_numrecs = ( j<(links_at_level[l-1]-1) ) ? nlinks[l][j+1] :
								nlinks[l-1][0];
							int bb_level = ( j<(links_at_level[l-1]-1) ) ? height+1-l :
								height+1-l + 1;

							INT_SET(block->bb_magic, ARCH_CONVERT, XFS_ABTC_MAGIC);
							INT_SET(block->bb_level, ARCH_CONVERT, bb_level);
							INT_SET(block->bb_numrecs, ARCH_CONVERT, bb_numrecs);
							INT_SET(block->bb_leftsib, ARCH_CONVERT, backw);
							INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);
						}
					}

					ASSERT(!(*p_hashlist));

					struct hashlist *l_hashlist = hashlist;
					while (l_hashlist)
					{
						struct hashlist *h = l_hashlist;

						l_hashlist = l_hashlist->next;
						free (h);
					}

					hashlist = hashlist_z;
					p_hashlist = &hashlist;
				}
			}

			l_hashlist = cnum_list;
			while (l_hashlist)
			{
				struct hashlist *h = l_hashlist;

				l_hashlist = l_hashlist->next;
				free (h);
			}
		}

		progress_close(&progress);
	}

	for (agno = 0; agno < agcount; agno++) {
		/*
		 * Superblock.
		 */
		buf = libxfs_getbuf(xi.ddev,
				XFS_AG_DADDR(mp, agno, XFS_SB_DADDR),
				XFS_FSS_TO_BB(mp, 1));
		bzero(XFS_BUF_PTR(buf), sectorsize);
		libxfs_xlate_sb(XFS_BUF_PTR(buf), sbp, -1, XFS_SB_ALL_BITS);
		if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
		else libxfs_putbuf(buf);
	}

	/*
	 * Touch last block, make fs the right size if it's a file.
	 */
	buf = libxfs_getbuf(mp->m_dev,
		(xfs_daddr_t)XFS_FSB_TO_BB(mp, dblocks - 1LL), bsize);
	bzero(XFS_BUF_PTR(buf), blocksize);
	if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
	else libxfs_putbuf(buf);

	/*
	 * Make sure we can write the last block in the realtime area.
	 */
	if (mp->m_rtdev && rtblocks > 0) {
		buf = libxfs_getbuf(mp->m_rtdev,
				XFS_FSB_TO_BB(mp, rtblocks - 1LL), bsize);
		bzero(XFS_BUF_PTR(buf), blocksize);
		if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
		else libxfs_putbuf(buf);
	}

	/*
	 * BNO, CNT free block list
	 */
	if (!noaction)
	for (agno = 0; agno < agcount; agno++) {
		xfs_alloc_arg_t	args;
		xfs_trans_t	*tp;

		bzero(&args, sizeof(args));
		args.tp = tp = libxfs_trans_alloc(mp, 0);
		args.mp = mp;
		args.agno = agno;
		args.alignment = 1;
		args.pag = &mp->m_perag[agno];
		if ((c = libxfs_trans_reserve(tp, worst_freelist, 0, 0, 0, 0)))
			res_failed(c);
		libxfs_alloc_fix_freelist(&args, 0);
		libxfs_trans_commit(tp, 0, NULL);
	}

	/*
	 * Dump all inodes and buffers before marking us all done.
	 * Need to drop references to inodes we still hold, first.
	 */
	libxfs_rtmount_destroy(mp);
	libxfs_icache_purge();
	libxfs_bcache_purge();

	/*
	 * Mark the filesystem ok.
	 */
	buf = libxfs_getsb(mp, LIBXFS_EXIT_ON_FAILURE);
	(XFS_BUF_TO_SBP(buf))->sb_inprogress = 0;
	if (!noaction) libxfs_writebuf(buf, LIBXFS_EXIT_ON_FAILURE);
	else libxfs_putbuf(buf);

	libxfs_umount(mp);
	if (xi.rtdev)
		libxfs_device_close(xi.rtdev);
	if (xi.logdev && xi.logdev != xi.ddev)
		libxfs_device_close(xi.logdev);
	libxfs_device_close(xi.ddev);

	return 0;
}

static void
conflict(
	char		opt,
	char		*tab[],
	int		oldidx,
	int		newidx)
{
	fprintf(stderr, _("Cannot specify both -%c %s and -%c %s\n"),
		opt, tab[oldidx], opt, tab[newidx]);
	usage();
}


static void
illegal(
	char		*value,
	char		*opt)
{
	fprintf(stderr, _("Illegal value %s for -%s option\n"), value, opt);
	usage();
}

static int
ispow2(
	unsigned int	i)
{
	return (i & (i - 1)) == 0;
}

static void
reqval(
	char		opt,
	char		*tab[],
	int		idx)
{
	fprintf(stderr, _("-%c %s option requires a value\n"), opt, tab[idx]);
	usage();
}

static void
respec(
	char		opt,
	char		*tab[],
	int		idx)
{
	fprintf(stderr, "-%c ", opt);
	if (tab)
		fprintf(stderr, "%s ", tab[idx]);
	fprintf(stderr, _("option respecified\n"));
	usage();
}

static void
unknown(
	char		opt,
	char		*s)
{
	fprintf(stderr, _("unknown option -%c %s\n"), opt, s);
	usage();
}

/*
 * isdigits -- returns 1 if string contains nothing but [0-9], 0 otherwise
 */
int
isdigits(
	char		*str)
{
	int		i;
	int		n = strlen(str);

	for (i = 0; i < n; i++) {
		if (!isdigit((int)str[i]))
			return 0;
	}
	return 1;
}

long long
cvtnum(
	unsigned int	blocksize,
	unsigned int	sectorsize,
	char		*s)
{
	long long	i;
	char		*sp;

	i = strtoll(s, &sp, 0);
	if (i == 0 && sp == s)
		return -1LL;
	if (*sp == '\0')
		return i;

	if (*sp == 'b' && sp[1] == '\0') {
		if (blocksize)
			return i * blocksize;
		fprintf(stderr, _("blocksize not available yet.\n"));
		usage();
	}
	if (*sp == 's' && sp[1] == '\0') {
		if (sectorsize)
			return i * sectorsize;
		return i * BBSIZE;
	}
	if (*sp == 'k' && sp[1] == '\0')
		return 1024LL * i;
	if (*sp == 'm' && sp[1] == '\0')
		return 1024LL * 1024LL * i;
	if (*sp == 'g' && sp[1] == '\0')
		return 1024LL * 1024LL * 1024LL * i;
	if (*sp == 't' && sp[1] == '\0')
		return 1024LL * 1024LL * 1024LL * 1024LL * i;
	if (*sp == 'p' && sp[1] == '\0')
		return 1024LL * 1024LL * 1024LL * 1024LL * 1024LL * i;
	if (*sp == 'e' && sp[1] == '\0')
		return 1024LL * 1024LL * 1024LL * 1024LL * 1024LL * 1024LL * i;
	return -1LL;
}

void
usage( void )
{
	fprintf(stderr, _("Usage: %s\n\
/* blocksize */		[-b log=n|size=num]\n\
/* data subvol */	[-d agcount=n,agsize=n,file,name=xxx,size=num,\n\
			    (sunit=value,swidth=value|su=num,sw=num),\n\
			    sectlog=n|sectsize=num,unwritten=0|1]\n\
/* inode size */	[-i log=n|perblock=n|size=num,maxpct=n,attr=0|1|2]\n\
/* log subvol */	[-l agnum=n,internal,size=num,logdev=xxx,version=n\n\
			    sunit=value|su=num,sectlog=n|sectsize=num]\n\
/* label */		[-L label (maximum 12 characters)]\n\
/* naming */		[-n log=n|size=num,version=n]\n\
/* not create */	[-N]\n\
/* quiet */		[-q]\n\
/* realtime subvol */	[-r extsize=num,size=num,rtdev=xxx]\n\
/* sectorsize */	[-s log=n|size=num]\n\
/* verbose */		[-v]\n\
/* version */		[-V]\n\
			inode_table\n\
			devicename\n\
<devicename> is required unless -d name=xxx is given.\n\
<num> is xxx (bytes), xxxs (sectors), xxxb (fs blocks), xxxk (xxx KiB),\n\
      xxxm (xxx MiB), xxxg (xxx GiB), xxxt (xxx TiB) or xxxp (xxx PiB).\n\
<value> is xxx (512 byte blocks).\n"
"\n"
"Please READ 'man build_xfs' BEFORE using\n"
"Use option -N before real action\n"),
		progname);
	exit(1);
}
