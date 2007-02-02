/*
 *      archieve2_files_descr.c
 *      CopyRight (C) 2006, Nikolaj Krivchenkov aka unDEFER <undefer@gmail.com>
 *      
 *      From bzip2recover.c (part of bzip2):
 *      Copyright (C) 1996-2005 Julian R Seward.
 */

/*--
  This program is bzip2recover, a program to attempt data 
  salvage from damaged files created by the accompanying
  bzip2-1.0.3 program.

  Copyright (C) 1996-2005 Julian R Seward.  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.

  2. The origin of this software must not be misrepresented; you must 
     not claim that you wrote the original software.  If you use this 
     software in a product, an acknowledgment in the product 
     documentation would be appreciated but is not required.

  3. Altered source versions must be plainly marked as such, and must
     not be misrepresented as being the original software.

  4. The name of the author may not be used to endorse or promote 
     products derived from this software without specific prior written 
     permission.

  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  Julian Seward, Cambridge, UK.
  jseward@bzip.org
  bzip2/libbzip2 version 1.0.3 of 15 February 2005
--*/

/*--
  This program is a complete hack and should be rewritten
  properly.  It isn't very complicated.
--*/

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include "anysurrect.h"
#include "archieve2_files_descr.h"

/* This program records bit locations in the file to be recovered.
   That means that if 64-bit ints are not supported, we will not
   be able to recover .bz2 files over 512MB (2^32 bits) long.
   On GNU supported platforms, we take advantage of the 64-bit
   int support to circumvent this problem.  Ditto MSVC.

   This change occurred in version 1.0.2; all prior versions have
   the 512MB limitation.
*/
#ifdef __GNUC__
   typedef  unsigned long long int  MaybeUInt64;
#  define MaybeUInt64_FMT "%Lu"
#else
#ifdef _MSC_VER
   typedef  unsigned __int64  MaybeUInt64;
#  define MaybeUInt64_FMT "%I64u"
#else
   typedef  unsigned int   MaybeUInt64;
#  define MaybeUInt64_FMT "%u"
#endif
#endif

typedef  unsigned int   UInt32;
typedef  int            Int32;
typedef  unsigned char  UChar;
typedef  char           Char;
typedef  unsigned char  Bool;
#define True    ((Bool)1)
#define False   ((Bool)0)


#define BZ_MAX_FILENAME 2000

Char inFileName[BZ_MAX_FILENAME];
Char outFileName[BZ_MAX_FILENAME];
Char progName[]="anysurrect";

MaybeUInt64 bytesOut = 0;
MaybeUInt64 bytesIn  = 0;


/*---------------------------------------------------*/
/*--- Header bytes                                ---*/
/*---------------------------------------------------*/

#define BZ_HDR_B 0x42                         /* 'B' */
#define BZ_HDR_Z 0x5a                         /* 'Z' */
#define BZ_HDR_h 0x68                         /* 'h' */
#define BZ_HDR_0 0x30                         /* '0' */
 
/*---------------------------------------------*/
void mallocFail ( Int32 n )
{
   fprintf ( stderr,
             "%s: malloc failed on request for %d bytes.\n",
            progName, n );
   fprintf ( stderr, "%s: warning: output file(s) may be incomplete.\n",
             progName );
   exit ( 1 );
}

/*---------------------------------------------------*/
/*--- Bit stream I/O                              ---*/
/*---------------------------------------------------*/

typedef
   struct {
      Int32  buffer;
      Int32  buffLive;
   }
   BitStream;


/*---------------------------------------------*/
BitStream* bsOpenReadStream ( )
{
   BitStream *bs = malloc ( sizeof(BitStream) );
   if (bs == NULL) mallocFail ( sizeof(BitStream) );
   bs->buffer = 0;
   bs->buffLive = 0;
   return bs;
}

/*---------------------------------------------*/
/*--
   Returns 0 or 1, or 2 to indicate EOF.
--*/
Int32 bsGetBit ( BitStream* bs )
{
   if (bs->buffLive > 0) {
      bs->buffLive --;
      return ( ((bs->buffer) >> (bs->buffLive)) & 0x1 );
   } else {
      Int32 retVal = READ_INTCHAR ( "byte" );
      if ( retVal == EOF )
         return 2;
      bs->buffLive = 7;
      bs->buffer = retVal;
      return ( ((bs->buffer) >> 7) & 0x1 );
   }
}


/*---------------------------------------------*/
void bsClose ( BitStream* bs )
{
   free ( bs );
}


/*---------------------------------------------------*/
/*---                                             ---*/
/*---------------------------------------------------*/

/* This logic isn't really right when it comes to Cygwin. */
#ifdef _WIN32
#  define  BZ_SPLIT_SYM  '\\'  /* path splitter on Windows platform */
#else
#  define  BZ_SPLIT_SYM  '/'   /* path splitter on Unix platform */
#endif

#define BLOCK_HEADER_HI  0x00003141UL
#define BLOCK_HEADER_LO  0x59265359UL

#define BLOCK_ENDMARK_HI 0x00001772UL
#define BLOCK_ENDMARK_LO 0x45385090UL

/* Increase if necessary.  However, a .bz2 file with > 50000 blocks
   would have an uncompressed size of at least 40GB, so the chances
   are low you'll need to up this.
*/
#define BZ_MAX_HANDLED_BLOCKS 50000

MaybeUInt64 bStart [BZ_MAX_HANDLED_BLOCKS];
MaybeUInt64 bEnd   [BZ_MAX_HANDLED_BLOCKS];
MaybeUInt64 rbStart[BZ_MAX_HANDLED_BLOCKS];
MaybeUInt64 rbEnd  [BZ_MAX_HANDLED_BLOCKS];

char * archieve_BZIP2_surrect()
{
   BitStream*  bsIn;
   Int32       b, currBlock, rbCtr;
   MaybeUInt64 bitsRead;

   UInt32      buffHi, buffLo;

   EX_STRING("magic", "BZh");
   fd_seek(0, SEEK_SET);

   bsIn = bsOpenReadStream ( );

   bitsRead = 0;
   buffHi = buffLo = 0;
   currBlock = 0;
   bStart[currBlock] = 0;

   rbCtr = 0;

   while (True) {
      b = bsGetBit ( bsIn );
      bitsRead++;
      if ( (bitsRead - bStart[currBlock]) > 1000*1024*8 )
      {
	      bitsRead = bStart[currBlock];
	      break;
      }
      if (b == 2) {
         if (bitsRead >= bStart[currBlock] &&
            (bitsRead - bStart[currBlock]) >= 40) {
            bEnd[currBlock] = bitsRead-1;
            if (currBlock > 0)
	    {
	       bitsRead = bStart[currBlock];
	       currBlock--;
	    }
         } else
		 currBlock--;
         break;
      }
      buffHi = (buffHi << 1) | (buffLo >> 31);
      buffLo = (buffLo << 1) | (b & 1);
      if ( ( (buffHi & 0x0000ffff) == BLOCK_HEADER_HI 
             && buffLo == BLOCK_HEADER_LO)
           || 
           ( (buffHi & 0x0000ffff) == BLOCK_ENDMARK_HI 
             && buffLo == BLOCK_ENDMARK_LO)
         ) {
         if (bitsRead > 49) {
            bEnd[currBlock] = bitsRead-49;
         } else {
            bEnd[currBlock] = 0;
         }
         if (currBlock > 0 &&
	     (bEnd[currBlock] - bStart[currBlock]) >= 130) {
	    rbStart[rbCtr] = bStart[currBlock];
            rbEnd[rbCtr] = bEnd[currBlock];
            rbCtr++;
         }
         if (currBlock >= BZ_MAX_HANDLED_BLOCKS)
            return ERROR_VALUE;
         currBlock++;

         bStart[currBlock] = bitsRead;
      }
   }

   bsClose ( bsIn );

   if (currBlock>0)
   {
	   any_size_t size = (bEnd[currBlock]+40+49-1)/8;
	   fd_seek(size, SEEK_SET);
	   return "archieve/BZIP2";
   }
   
   return ERROR_VALUE;
}

