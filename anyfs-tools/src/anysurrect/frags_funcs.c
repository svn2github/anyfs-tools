/*
 * =====================================================================================
 * 
 *        Filename:  frags_funcs.c
 * 
 *     Description:  
 * 
 *         Version:  1.0
 *         Created:  01.05.2007 20:29:07 MSD
 *        Revision:  none
 *        Compiler:  gcc
 * 
 *          Author:  Nikolaj Krivchenkov aka unDEFER (), undefer@gmail.com
 *         Company:  
 * 
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <bitops.h>
#include <assert.h>
#include "any.h"
#include "anysurrect_malloc.h"
#include "anysurrect_io.h"
#include "frags_funcs.h"

int create_frags_list(unsigned long *block_bitmap,
		unsigned long blocks,
		struct frags_list **pfrags_list)
{
	unsigned long block=0;
	struct frags_list *new;
	struct frags_list *prev = NULL;
	struct frags_list **pfrags_list_begin = pfrags_list;
	
	unsigned long blocks_in_list = 0;
	for (; block<blocks; block++)
	{
		unsigned long start = block;
		unsigned long length = 0;
		for (; block<blocks && !test_bit(block, block_bitmap); 
				block++, length++);
		if (length)
		{
			new = malloc(sizeof(struct frags_list));
			if ( !new )
			{
				fprintf(stderr, _("Not enough memory\n"));
				exit(1);
			}
			(*pfrags_list) = (void*)((char*)new - (char*)prev);
			prev = new;

			new->frag.fr_start =  start;
			new->frag.fr_length = length;
			new->offnext = 0;
			new->whole = -1;
			new->size = 0;
			pfrags_list = (void*)&new->offnext;

			blocks_in_list += length;
		}
	}

	(*pfrags_list_begin)->size = blocks_in_list << get_log2blocksize();

	return 0;
}

struct frags_list *addblock_to_frags_list(struct frags_list **pfrags_list_begin,
		struct frags_list *pfrags_list, unsigned long block)
{
	struct frags_list *new;
	struct frags_list *prev = pfrags_list;
	signed long *offnext =
		pfrags_list ? &pfrags_list->offnext : 
		(void*) pfrags_list_begin;
	
	if (!pfrags_list || ( pfrags_list->frag.fr_start ?
				( pfrags_list->frag.fr_start +
				  pfrags_list->frag.fr_length ) != block : 
				block ) )
	{
		new = malloc(sizeof(struct frags_list));
		if ( !new )
		{
			fprintf(stderr, _("Not enough memory\n"));
			exit(1);
		}
		(*offnext) = (signed long)((char*)new - (char*)prev);
		prev = new;

		new->frag.fr_start  = block;
		new->frag.fr_length = 1;
		new->offnext = 0;
		new->whole = -1;
		new->size = 0;

		pfrags_list = (void*)new;
		offnext	= (void*)&new->offnext;
	}
	else
		pfrags_list->frag.fr_length++;

	(*pfrags_list_begin)->size += get_blocksize();

	return pfrags_list;
}

int copy_frags_list(struct frags_list *from, struct frags_list **pto)
{
	struct frags_list *new;
	struct frags_list *prev = NULL;

	assert(from);
	if (from->whole!=-1)
	{
		struct frags_list *whole = 
			anysurrect_malloc(sizeof(struct frags_list)*from->num_frags,
					COPY_FRAGS_MALLOC_BUFFER);
		if (!whole)
		{
			fprintf(stderr, _("Not enough memory\n"));
			exit(1);
		}

		memcpy(whole, from - from->whole,
				sizeof(struct frags_list)*from->num_frags);

		(*pto) = whole + from->whole;

		return 0;
	}

	struct frags_list *from_orig = from;
	
	unsigned long num_frags = 0;
	
	for (; from; NEXT_FRAG(from) )
		num_frags++;

	from = from_orig;

	struct frags_list *whole = 
		malloc(sizeof(struct frags_list)*num_frags);
	if (!whole)
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}
	
	struct frags_list *whole_part =  whole;

	for (; from; NEXT_FRAG(from) )
	{
		new = whole_part++;

		(*pto) = (void*)((char*)new - (char*)prev);
		prev = new;

		new->frag = from->frag;
		new->offnext = 0;
		new->whole = new - whole;
		new->num_frags = num_frags;
		new->size = from->size;
		pto = (void*)&new->offnext;
	}

	return 0;
}

int free_frags_list(struct frags_list *frags_list)
{
	signed long offnext = 0;

	if (frags_list->whole!=-1)
	{
		anysurrect_free(frags_list - frags_list->whole,
				COPY_FRAGS_MALLOC_BUFFER);
		return 0;
	}

	for (; frags_list; NEXT_FRAG_OFS(frags_list, offnext) )
	{
		offnext = frags_list->offnext;
		free(frags_list);
	}

	return 0;
}


int cut_frags(struct frags_list **pfrags_list, unsigned long from,
		unsigned long blocks)
{
	unsigned long block = 0;
	unsigned long fr_length;
	unsigned long fr_start;
	any_size_t    old_size = 0;
	if (*pfrags_list) old_size = (*pfrags_list)->size;

	struct frags_list *frags_list = *pfrags_list;

	struct frags_list *frag_begin_cut = NULL;
	unsigned long old_fbc_length = 0;
	unsigned long new_fbc_length = 0;
	
	struct frags_list *frag_end_cut = NULL;
	unsigned long old_fec_start = 0;
	unsigned long new_fec_start = 0;
	unsigned long new_fec_length = 0;

	if (!blocks) return 0;
	
	for (; frags_list; NEXT_FRAG(frags_list))
	{
		fr_length = frags_list->frag.fr_length;
		if ( (block + fr_length)>from ) {
			frag_begin_cut = frags_list;
			new_fbc_length = from - block;
			break;
		}
		
		block += fr_length;
	}
	
	for (; frags_list; NEXT_FRAG(frags_list))
	{
		fr_length = frags_list->frag.fr_length;
		if ( (block + fr_length)>(from+blocks) ) {
			fr_start = frags_list->frag.fr_start;
			frag_end_cut = frags_list;
			new_fec_start = fr_start + from+blocks-block;
			new_fec_length = fr_start + fr_length
				- new_fec_start;
			break;
		}
		
		block += fr_length;
	}

	assert(frag_begin_cut);

	old_fbc_length = frag_begin_cut->frag.fr_length;
	frag_begin_cut->frag.fr_length = new_fbc_length;

	if (frag_end_cut)
	{
		old_fec_start = frag_end_cut->frag.fr_start;
		frag_end_cut->frag.fr_start = new_fec_start;
		frag_end_cut->frag.fr_length = new_fec_length;
	}

	if (frag_begin_cut!=frag_end_cut)
	{
		frags_list = NEXT_FRAG_WA(frag_begin_cut);
		signed long offnext = 0;

		for (; frags_list!=frag_end_cut;
				NEXT_FRAG_OFS(frags_list, offnext))
		{
			offnext = frags_list->offnext;
			if (frags_list->whole==-1)
				free(frags_list);
		}

		frag_begin_cut->offnext = (char*)frag_end_cut - 
			(char*)frag_begin_cut;
	}
	else
	{
		if ( (old_fbc_length != new_fbc_length) && new_fbc_length )
		{
			fprintf(stderr, "It is a bug. Please send bugreport to undefer@gmail.com\n");
			fprintf(stderr, "from=%lu, blocks=%lu\n", from, blocks);
			fprintf(stderr, "%lu != %lu\n", old_fbc_length, new_fbc_length);
			exit(1);
		}
	}

	while ( (*pfrags_list) && !(*pfrags_list)->frag.fr_length )
	{
		struct frags_list *next = NEXT_FRAG_WA( (*pfrags_list) );
		if ((*pfrags_list)->whole==-1)
			anysurrect_free(*pfrags_list,
					COPY_FRAGS_MALLOC_BUFFER);
		else if (!next)
			anysurrect_free( (*pfrags_list) - (*pfrags_list)->whole,
				      COPY_FRAGS_MALLOC_BUFFER );
		(*pfrags_list) = next;
	}

	if (*pfrags_list)
	{
		(*pfrags_list)->size = old_size - 
			( blocks << get_log2blocksize() );
	}
	return 0;
}

int pick_frags(struct frags_list **pfrags_list, unsigned long from,
		unsigned long blocks)
{
	unsigned long new_blocks_in_list = 0;
	unsigned long block = 0;
	unsigned long fr_length;
	unsigned long fr_start;
	
	struct frags_list *frags_list = *pfrags_list;

	struct frags_list *start_frags_list = frags_list;

	struct frags_list *frag_begin_pick = NULL;
	unsigned long old_fbp_start = 0;
	unsigned long new_fbp_start = 0;
	unsigned long new_fbp_length = 0;
	
	struct frags_list *frag_end_pick = NULL;
	unsigned long new_fep_length = 0;
	
	for (; frags_list; NEXT_FRAG(frags_list) )
	{
		fr_length = frags_list->frag.fr_length;
		if ( (block + fr_length)>from ) {
			fr_start = frags_list->frag.fr_start;
			frag_begin_pick = frags_list;
			new_fbp_start = fr_start + from-block;
			new_fbp_length = fr_start + fr_length
				- new_fbp_start;
			break;
		}
		
		block += fr_length;
	}
	
	for (; frags_list; NEXT_FRAG(frags_list) )
	{
		fr_length = frags_list->frag.fr_length;
		if ( (block + fr_length)>(from+blocks) ) {
			frag_end_pick = frags_list;
			new_fep_length = from+blocks - block;
			break;
		}
		
		block += fr_length;
		new_blocks_in_list += fr_length;
	}

	if (!frag_begin_pick || !frag_end_pick)
	{
		return 0;
		fprintf(stderr, "Error in pick_frags\n");
		exit(1);
	}

	old_fbp_start = frag_begin_pick->frag.fr_start;
	frag_begin_pick->frag.fr_start = new_fbp_start;
	frag_begin_pick->frag.fr_length = new_fbp_length;

	frag_end_pick->frag.fr_length = new_fep_length;

	if (frag_begin_pick == frag_end_pick)
	{
		new_fep_length -= old_fbp_start - new_fbp_start;
		new_blocks_in_list += old_fbp_start + new_fep_length - new_fbp_start;
	}
	else
	{
		new_blocks_in_list -= new_fep_length - old_fbp_start;
		new_blocks_in_list += new_fep_length;
	}

	frags_list = start_frags_list;
	signed long offnext = 0;

	for (; frags_list!=frag_begin_pick; 
			NEXT_FRAG_OFS(frags_list, offnext) )
	{
		offnext = frags_list->offnext;
		if (frags_list->whole==-1)
			free(frags_list);
	}
	
	frags_list = NEXT_FRAG_WA(frag_end_pick);
	offnext = 0;

	for (; frags_list; NEXT_FRAG_OFS(frags_list, offnext) )
	{
		offnext = frags_list->offnext;
		if (frags_list->whole==-1)
			free(frags_list);
	}

	*pfrags_list = frag_begin_pick;
	frag_end_pick->offnext = 0;

	frag_begin_pick->size = new_blocks_in_list << get_log2blocksize();
	
	return 0;
}

int frags_list2array (struct frags_list *frags_list, 
		struct any_file_frags **pfile_frags)
{
	struct frags_list *frags_list_p = frags_list;
	struct any_file_fragment *file_frag;
	
	(*pfile_frags) = calloc (sizeof(struct any_file_frags), 1);
	if (!(*pfile_frags))
	{
		fprintf(stderr, _("Not enough memory\n"));
		exit(1);
	}
	
	for (; frags_list_p; NEXT_FRAG(frags_list_p) )
		(*pfile_frags)->fr_nfrags++;

	(*pfile_frags)->fr_frags = 
		malloc( sizeof(struct any_file_fragment) *
				(*pfile_frags)->fr_nfrags );
		
	file_frag = (*pfile_frags)->fr_frags;
	
	for (; frags_list; NEXT_FRAG(frags_list), 
			file_frag++)
		(*file_frag) = frags_list->frag;

	return 0;
}
