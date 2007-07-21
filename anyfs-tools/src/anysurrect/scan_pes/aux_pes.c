/*
 *  aux_pes.c
 *
 *  THIS FILE WAS MODIFIED FOR ANYFS-TOOLS BY
 *	Nikolay (unDEFER) Krivchenkov <undefer@gmail.com> (March 2006)
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

#include "tc_defaults.h"

#include <math.h>
#include <stdio.h>

#include "aux_pes.h"

#if 0 // EMS not used
static int cmp_32_bits(char *buf, long x)
{
  
    if (buf[0] != ((x >> 24) & 0xff))
	return 0;
    if (buf[1] != ((x >> 16) & 0xff))
	return 0;
    if (buf[2] != ((x >>  8) & 0xff))
	return 0;
    if (buf[3] != ((x      ) & 0xff))
	return 0;
  
  // OK found it
  return 1;
}
#endif

unsigned int anyfs_stream_read_int16(unsigned char *s)
{ 
  unsigned int a, b, result;

  a = s[0];
  b = s[1];

  result = (a << 8) | b;
  return result;
}

int anyfs_stats_sequence_silent(uint8_t * buffer, seq_info_t *seq_info)
{
  
  int horizontal_size;
  int vertical_size;
  int aspect_ratio_information;
  int frame_rate_code;
  int bit_rate_value;
  
  vertical_size = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
  horizontal_size = ((vertical_size >> 12) + 15) & ~15;
  vertical_size = ((vertical_size & 0xfff) + 15) & ~15;

  aspect_ratio_information = buffer[3] >> 4;
  frame_rate_code = buffer[3] & 15;
  bit_rate_value = (buffer[4] << 10) | (buffer[5] << 2) | (buffer[6] >> 6);
  if(aspect_ratio_information < 0 || aspect_ratio_information>15) {
    fprintf(stderr, "error: ****** invalid MPEG sequence header detected (%d/%d|%d/%d) ******\n", 
	    aspect_ratio_information, 16, frame_rate_code, 16);
    return(-1);
  }
  
  if(frame_rate_code < 0 || frame_rate_code>15) {
    fprintf(stderr, "error: ****** invalid MPEG sequence header detected (%d/%d|%d/%d) ******\n", 
	    frame_rate_code, 16, aspect_ratio_information, 8);
    return(-1);
  }
  
  //fill out user structure
  
  seq_info->w = horizontal_size;
  seq_info->h = vertical_size;
  seq_info->ari = aspect_ratio_information;
  seq_info->frc = frame_rate_code;
  seq_info->brv = bit_rate_value;
  
  return(0);
  
}
int anyfs_stats_sequence(uint8_t * buffer, seq_info_t *seq_info)
{
  
  int horizontal_size;
  int vertical_size;
  int aspect_ratio_information;
  int frame_rate_code;
  int bit_rate_value;
  int vbv_buffer_size_value;
  int constrained_parameters_flag;
  int load_intra_quantizer_matrix;
  int load_non_intra_quantizer_matrix;
  
  vertical_size = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
  horizontal_size = ((vertical_size >> 12) + 15) & ~15;
  vertical_size = ((vertical_size & 0xfff) + 15) & ~15;

  aspect_ratio_information = buffer[3] >> 4;
  frame_rate_code = buffer[3] & 15;
  bit_rate_value = (buffer[4] << 10) | (buffer[5] << 2) | (buffer[6] >> 6);
  vbv_buffer_size_value = ((buffer[6] << 5) | (buffer[7] >> 3)) & 0x3ff;
  constrained_parameters_flag = buffer[7] & 4;
  load_intra_quantizer_matrix = buffer[7] & 2;
  if (load_intra_quantizer_matrix)
    buffer += 64;
  load_non_intra_quantizer_matrix = buffer[7] & 1;

  if(aspect_ratio_information < 0 || aspect_ratio_information>15) {
    printf("error: ****** invalid MPEG sequence header detected (%d/%d|%d/%d) ******\n", aspect_ratio_information, 16, frame_rate_code, 16);
    return(-1);
  }
  
  if(frame_rate_code < 0 || frame_rate_code>15) {
    printf("error: ****** invalid MPEG sequence header detected (%d/%d|%d/%d) ******\n", frame_rate_code, 16, aspect_ratio_information, 8);
    return(-1);
  }
  
#ifdef DEBUG
  printf("\tsequence: %dx%d %s, %s fps, %5.0f kbps, VBV %d kB%s%s%s\n", horizontal_size, vertical_size,
		  aspect_ratio_information_str [aspect_ratio_information],
		  frame_rate_str [frame_rate_code],
		  bit_rate_value * 400.0 / 1000.0,
		  2 * vbv_buffer_size_value,
		  constrained_parameters_flag ? " , CP":"",
		  load_intra_quantizer_matrix ? " , Custom Intra Matrix":"",
		  load_non_intra_quantizer_matrix ? " , Custom Non-Intra Matrix":"");
#endif
  
  
  //fill out user structure
  
  seq_info->w = horizontal_size;
  seq_info->h = vertical_size;
  seq_info->ari = aspect_ratio_information;
  seq_info->frc = frame_rate_code;
  seq_info->brv = bit_rate_value;
  
  return(0);
  
}

int anyfs_get_pts_dts(char *buffer, unsigned long *pts, unsigned long *dts)
{
  unsigned int pes_header_bytes = 0;
  unsigned int pts_dts_flags;
  int pes_header_data_length;

  int has_pts_dts=0;

  unsigned int ptr=0;
  
  /* drop first 8 bits */
  ++ptr;
  pts_dts_flags = (buffer[ptr++] >> 6) & 0x3;
  pes_header_data_length = buffer[ptr++];

  switch(pts_dts_flags)
    
    {

    case 2:
      
      *pts = (buffer[ptr++] >> 1) & 7;  //low 4 bits (7==1111) 
      *pts <<= 15;
      *pts |= (anyfs_stream_read_int16(&buffer[ptr]) >> 1);
      ptr+=2;
      *pts <<= 15;
      *pts |= (anyfs_stream_read_int16(&buffer[ptr]) >> 1);
      ptr+=2;

      pes_header_bytes += 5;

      has_pts_dts=1;

      break;

    case 3:
      
      *pts = (buffer[ptr++] >> 1) & 7;  //low 4 bits (7==1111) 
      *pts <<= 15;
      *pts |= (anyfs_stream_read_int16(&buffer[ptr]) >> 1);
      ptr+=2;
      *pts <<= 15;
      *pts |= (anyfs_stream_read_int16(&buffer[ptr]) >> 1);
      ptr+=2;
      
      *dts = (buffer[ptr++] >> 1) & 7;  
      *dts <<= 15;
      *dts |= (anyfs_stream_read_int16(&buffer[ptr]) >> 1);
      ptr+=2;
      *dts <<= 15;
      *dts |= (anyfs_stream_read_int16(&buffer[ptr]) >> 1);
      ptr+=2;

      pes_header_bytes += 10;

      has_pts_dts=1;      

      break;
      
    default:

      has_pts_dts=0;            
      *dts=*pts=0;
      break;
    }
  
  return(has_pts_dts);
}
