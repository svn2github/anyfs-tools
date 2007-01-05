/*
 *  scan_pes.c
 *
 *  THIS FILE WAS MODIFIED FOR ANYFS-TOOLS BY
 *	 Nikolay (unDEFER) Krivchenkov <undefer@gmail.com> (March 2006)
 *
 *  Copyright (C) Thomas Östreich - June 2001
 *
 *  This file is part of transcode, a video stream processing tool
 *      
 *  transcode is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  transcode is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include "any.h"
#include "anysurrect.h"

#include "aux_pes.h"

#include "tc_defaults.h"
#include "magic.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define BUFFER_SIZE 262144
static uint8_t buffer[BUFFER_SIZE];

static seq_info_t si;

static int mpeg_version=0;

static int unit_ctr=0, seq_ctr=0;

static uint16_t id;
    
static uint32_t stream[256], track[TC_MAX_AUD_TRACKS];

static int tot_seq_ctr=0, tot_unit_ctr=0;
static unsigned int tot_bitrate=0, min_bitrate=(unsigned int)-1, max_bitrate=0;

static int ref_pts=0;

static int show_seq_info=0;

static int cmp_32_bits(char *buf, long x)
{
  
    if ((uint8_t)buf[0] != ((x >> 24) & 0xff))
	return 0;
    if ((uint8_t)buf[1] != ((x >> 16) & 0xff))
	return 0;
    if ((uint8_t)buf[2] != ((x >>  8) & 0xff))
	return 0;
    if ((uint8_t)buf[3] != ((x      ) & 0xff))
	return 0;
  
  // OK found it
  return 1;
}
  
void unit_summary()
{
    int n;

    int pes_total=0;
    
#ifdef DEBUG
    fprintf(stderr, "------------- presentation unit [%d] ---------------\n", unit_ctr);
#endif

    for(n=0; n<256; ++n) {
#ifdef DEBUG
	if(stream[n] && n != 0xba) fprintf(stderr, "stream id [0x%x] %6d\n", n, stream[n]);
#endif
	
	if(n != 0xba) pes_total+=stream[n];
	stream[n]=0; //reset or next unit
    }
    
#ifdef DEBUG
    fprintf(stderr, "%d packetized elementary stream(s) PES packets found\n", pes_total); 
    
    fprintf(stderr, "presentation unit PU [%d] contains %d MPEG video sequence(s)\n", unit_ctr, seq_ctr);
    if (seq_ctr) {
    fprintf(stderr, "Average Bitrate is %u. Min Bitrate is %u, max is %u (%s)\n", 
	((tot_bitrate*400)/1000)/seq_ctr, min_bitrate*400/1000, max_bitrate*400/1000,
	(max_bitrate==min_bitrate)?"CBR":"VBR");
    }
#endif

    ++tot_unit_ctr;
    tot_seq_ctr+=seq_ctr;

#ifdef DEBUG
    fprintf(stderr, "---------------------------------------------------\n");
#endif

    //reset counters
    seq_ctr=0;
    show_seq_info=0;
}

static int mpeg1_skip_table[16] = {
  1, 0xffff,      5,     10, 0xffff, 0xffff, 0xffff, 0xffff,
  0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
};

/*------------------------------------------------------------------
 *
 * full source scan mode:
 * 
 *------------------------------------------------------------------*/

any_size_t scan_pes(int verbose)
{
  int n, has_pts_dts=0;
  unsigned long i_pts, i_dts;
  
  uint8_t * buf;
  uint8_t * end;
  uint8_t * tmp1=NULL;
  uint8_t * tmp2=NULL;
  int complain_loudly;
  
  any_ssize_t pack_header_last=0, pack_header_ctr=0, pack_header_pos=0, pack_header_inc=0;
  
  char scan_buf[256];
  
  complain_loudly = 1;
  buf = buffer;
  
    for(n=0; n<256; ++n) stream[n]=0;
    
    do {
      end = buf + fd_read (buf, buffer + BUFFER_SIZE - buf);
      buf = buffer;
      
      //scan buffer
      while (buf + 4 <= end) {
	
	// check for valid start code
	if (buf[0] || buf[1] || (buf[2] != 0x01)) {
	  if (complain_loudly) {
	
#ifdef DEBUG
	    if ((buf[0] == 0) && (buf[1] == 0) && (buf[2] == 0))
	      fprintf (stderr, "incorrect zero-byte padding detected - ignored\n");
#endif
	    
	    complain_loudly = 0;
	    
	    if (!pack_header_ctr) return 0;
	    pack_header_pos = fd_seek(0, SEEK_CUR) - (end - buf);
	    return pack_header_pos;
	  }
	  buf++;
	  continue;
	}// check for valid start code 
	
	
	id = buf[3] &  0xff;


	switch (buf[3]) {
	  
	case 0xb9:	/* program end code */

#ifdef DEBUG
	    fprintf(stderr, "found program end code [0x%x]\n", buf[3] & 0xff);
#endif

	    goto summary;
	  
	case 0xba:	/* pack header */

	    pack_header_pos = fd_seek(0, SEEK_CUR) - (end - buf);
	    pack_header_inc = pack_header_pos - pack_header_last;

#ifdef DEBUG
	    if (pack_header_inc==0) {
		fprintf(stderr, "found first packet header at stream offset 0x%#x\n", 0);
	    } else {
		if((pack_header_inc-((pack_header_inc>>11)<<11))) printf ("pack header out of sequence at %#lx (+%#lx)\n", pack_header_ctr, pack_header_inc);
	    }
#endif
	    
	    pack_header_last=pack_header_pos;
	    ++pack_header_ctr;
	    ++stream[id];
		
	    /* skip */
	    if ((buf[4] & 0xc0) == 0x40)	        /* mpeg2 */
		tmp1 = buf + 14 + (buf[13] & 7);
	    else if ((buf[4] & 0xf0) == 0x20)	        /* mpeg1 */
		tmp1 = buf + 12;
	    else if (buf + 5 > end)
		goto copy;
	    else {
#ifdef DEBUG
		fprintf (stderr, "weird pack header\n");
#endif
		/*import_exit(1); :ME_FIXED:*/
		return 0;
	    }
	    
	    if (tmp1 > end)
		goto copy;
	    buf = tmp1;
	    break;
	    
	    
	case 0xbd:	/* private stream 1 */
	  
#ifdef DEBUG
	  if(!stream[id]) fprintf(stderr, "found %s stream [0x%x]\n", "private_stream_1", buf[3] & 0xff);
#endif

	  ++stream[id];
	  
	  tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
	  if (tmp2 > end)
	    goto copy;
	  if ((buf[6] & 0xc0) == 0x80)	/* mpeg2 */
	    tmp1 = buf + 9 + buf[8];
	  else {	/* mpeg1 */
	    for (tmp1 = buf + 6; *tmp1 == 0xff; tmp1++)
	      if (tmp1 == buf + 6 + 16) {
#ifdef DEBUG
		fprintf (stderr, "too much stuffing\n");
#endif
		buf = tmp2;
		break;
	      }
	    if ((*tmp1 & 0xc0) == 0x40)
	      tmp1 += 2;
	    tmp1 += mpeg1_skip_table [*tmp1 >> 4];
	  }
	  
#ifdef DEBUG
	  if(verbose & TC_DEBUG) fprintf(stderr, "[0x%x] (sub_id=0x%02x)\n", buf[3] & 0xff, *tmp1);
#endif
	  
	  if((*tmp1-0x80) >= 0 && (*tmp1-0x80)<TC_MAX_AUD_TRACKS && !track[*tmp1-0x80] ) {
#ifdef DEBUG
	    fprintf(stderr, "found AC3 audio track %d [0x%x]\n", *tmp1-0x80, *tmp1);
#endif
	    track[*tmp1-0x80]=1;
	  }
	  
	  buf = tmp2;
	  
	  break;
	  
	case 0xbf:

#ifdef DEBUG
	  if(!stream[id]) fprintf(stderr, "found %s [0x%x]\n", "navigation pack", buf[3] & 0xff);
#endif
	  
	  ++stream[id];
	  
	  tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
	  if (tmp2 > end)
	    goto copy;
	  
	  buf = tmp2;
	  
	  break;
	  
	case 0xbe:
	  
#ifdef DEBUG
	  if(!stream[id]) fprintf(stderr, "found %s stream [0x%x]\n", "padding", buf[3] & 0xff);
#endif
	  
	  ++stream[id];
	  
	  tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
	  if (tmp2 > end)
	    goto copy;
	  
	  buf = tmp2;
	  
	  break;
	  
	case 0xbb:
	  
#ifdef DEBUG
	  if(!stream[id]) fprintf(stderr, "found %s stream [0x%x]\n", "unknown", buf[3] & 0xff);
#endif
	  
	  ++stream[id];
	  
	  tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
	  if (tmp2 > end)
	    goto copy;
	  
	  buf = tmp2;
	  
	  break;
	  
	  
	  //MPEG audio, maybe more???
	  
	case 0xc0:
	case 0xc1:
	case 0xc2:
	case 0xc3:
	case 0xc4:
	case 0xc5:
	case 0xc6:
	case 0xc7:
	case 0xc8:
	case 0xc9:
	case 0xca:
	case 0xcb:
	case 0xcc:
	case 0xcd:
	case 0xce:
	case 0xcf:
	case 0xd0:
	case 0xd1:
	case 0xd2:
	case 0xd3:
	case 0xd4:
	case 0xd5:
	case 0xd6:
	case 0xd7:
	case 0xd8:
	case 0xd9:
	case 0xda:
	case 0xdb:
	case 0xdc:
	case 0xdd:
	case 0xde:
	case 0xdf:

#ifdef DEBUG
	    if(!stream[id]) fprintf(stderr, "found %s track %d [0x%x]\n", "ISO/IEC 13818-3 or 11172-3 MPEG audio", (buf[3] & 0xff) - 0xc0, buf[3] & 0xff);
#endif
		
	    ++stream[id];

	    tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
	    if (tmp2 > end)
		goto copy;
	    if ((buf[6] & 0xc0) == 0x80)	/* mpeg2 */
		tmp1 = buf + 9 + buf[8];
	    else {	/* mpeg1 */
		for (tmp1 = buf + 6; *tmp1 == 0xff; tmp1++)
		    if (tmp1 == buf + 6 + 16) {
#ifdef DEBUG
			fprintf (stderr, "too much stuffing\n");
#endif
		buf = tmp2;
		break;
		    }
		if ((*tmp1 & 0xc0) == 0x40)
		    tmp1 += 2;
		tmp1 += mpeg1_skip_table [*tmp1 >> 4];
	    }
	    
	    buf = tmp2;
	    
	    break;
	
	case 0xe0:	/* video */
	case 0xe1:	/* video */
	case 0xe2:	/* video */
	case 0xe3:	/* video */
	case 0xe4:	/* video */
	case 0xe5:	/* video */
	case 0xe6:	/* video */
	case 0xe7:	/* video */
	case 0xe8:	/* video */
	case 0xe9:	/* video */

	    id = buf[3] &  0xff;
	    
	    tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
	    if (tmp2 > end)
		goto copy;
	    if ((buf[6] & 0xc0) == 0x80) {
		/* mpeg2 */
		tmp1 = buf + 9 + buf[8];
		
#ifdef DEBUG
		if(!stream[id]) fprintf(stderr, "found %s stream [0x%x]\n", "ISO/IEC 13818-2 or 11172-2 MPEG video", buf[3] & 0xff);
#endif
		++stream[id];

		mpeg_version=2;

		// get pts time stamp:
		memcpy(scan_buf, &buf[6], 16);
		has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);
		
		if(has_pts_dts) {
		  
		  if(!show_seq_info) {
		    for(n=0; n<100; ++n) {
		    
		      if(cmp_32_bits(buf+n, TC_MAGIC_M2V)) {
			stats_sequence(buf+n+4, &si);
			show_seq_info=1;
			break;
		      }
		    }
		  }
		  for(n=0; n<100; ++n) {
		    if(cmp_32_bits(buf+n, TC_MAGIC_M2V)) {
		      stats_sequence_silent(buf+n+4, &si);
		      if (si.brv>max_bitrate) max_bitrate=si.brv;
		      if (si.brv<min_bitrate) min_bitrate=si.brv;
		      tot_bitrate += si.brv;
		      break;
		    }
		  }

		  if( ref_pts != 0 && i_pts < ref_pts) {
		    
		    unit_summary();
		    unit_ctr++;
		  }
		  ref_pts=i_pts;
		  ++seq_ctr;
		}
		
	    } else {
	      /* mpeg1 */
	      
#ifdef DEBUG
	      if(!stream[id]) fprintf(stderr, "found %s stream [0x%x]\n", "MPEG-1 video", buf[3] & 0xff);
#endif
	      ++stream[id];

	      mpeg_version=1;
	      
	      if(!show_seq_info) {
		for(n=0; n<100; ++n) {
		  
		  if(cmp_32_bits(buf+n, TC_MAGIC_M2V)) {
		    stats_sequence(buf+n+4, &si);
		    show_seq_info=1;
		  }
		}
	      }

	      // get pts time stamp:
	      memcpy(scan_buf, &buf[6], 16);
	      has_pts_dts=get_pts_dts(scan_buf, &i_pts, &i_dts);
	      
	      if(has_pts_dts) {
		
		if( ref_pts != 0 && i_pts < ref_pts) {
		  
		  //unit_summary();
		  unit_ctr++;
		}
		ref_pts=i_pts;

		++seq_ctr;
	      }
	      
	      for (tmp1 = buf + 6; *tmp1 == 0xff; tmp1++)
		if (tmp1 == buf + 6 + 16) {
		  fprintf (stderr, "too much stuffing\n");
		  buf = tmp2;
		  break;
		}
	      if ((*tmp1 & 0xc0) == 0x40)
		tmp1 += 2;
	      tmp1 += mpeg1_skip_table [*tmp1 >> 4];
	    }
	    
	    buf = tmp2;
	    break;


	case 0xb3:
	    
#ifdef DEBUG
	    fprintf(stderr, "found MPEG sequence start code [0x%x]\n", buf[3] & 0xff);
	    fprintf(stderr, "(%s) looks like an elementary stream - not program stream\n", __FILE__);
#endif
	    
	    stats_sequence(&buf[4], &si);
	    
	    if (!pack_header_ctr) return 0;
	    pack_header_pos = fd_seek(0, SEEK_CUR) - (end - buf);
	    return pack_header_pos;
	    
	    break;

	  
	default:
	    if (buf[3] < 0xb9) {
#ifdef DEBUG
		fprintf(stderr, "(%s) looks like an elementary stream - not program stream\n", __FILE__);
#endif
		
		if (!pack_header_ctr) return 0;
		pack_header_pos = fd_seek(0, SEEK_CUR) - (end - buf);
		return pack_header_pos;
	    }
	    
	    /* skip */
	    tmp1 = buf + 6 + (buf[4] << 8) + buf[5];
	    if (tmp1 > end)
		goto copy;
	    buf = tmp1;
	    break;
	    
	} //start code selection
      } //scan buffer
      
      if (buf < end) {
      copy:
	  /* we only pass here for mpeg1 ps streams */
	  memmove (buffer, buf, end - buf);
      }
      buf = buffer + (end - buf);
      
    } while (end == buffer + BUFFER_SIZE);
    
#ifdef DEBUG
    fprintf(stderr, "end of stream reached\n");
#endif

 summary:

    unit_summary();

#ifdef DEBUG
    fprintf(stderr, "(%s) detected a total of %d presentation unit(s) PU and %d sequence(s)\n", __FILE__, tot_unit_ctr, tot_seq_ctr);
#endif

    if (!pack_header_ctr) return 0;
    return fd_seek(0, SEEK_CUR);
}
