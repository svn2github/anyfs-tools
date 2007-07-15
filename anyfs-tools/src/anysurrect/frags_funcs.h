/*
 * =====================================================================================
 * 
 *        Filename:  frags_funcs.h
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

#ifndef	_FRAGS_FUNCS_H
#define _FRAGS_FUNCS_H

#include "any.h"

extern any_off_t	cur_offset;

struct frags_list {
	struct any_file_fragment frag;
	signed long	offnext;
	long whole;
	unsigned long	num_frags;
	any_size_t	size;
};

extern unsigned long *block_bitmap;
extern struct frags_list *frags_list;
extern struct frags_list *file_template_frags_list;
extern struct frags_list *file_frags_list;

int create_frags_list(unsigned long *block_bitmap,
		unsigned long blocks,
		struct frags_list **pfrags_list);

struct frags_list *addblock_to_frags_list(struct frags_list **pfrags_list_begin,
		struct frags_list *pfrags_list, unsigned long block);

#define NEXT_FRAG_WA_OFS(frag, offnext) 		\
	(void*)( (offnext) ? (char*)frag + offnext : 0 )

#define NEXT_FRAG_OFS(frag, offnext) 		\
	( frag = (typeof (frag)) NEXT_FRAG_WA_OFS(frag, offnext) )

#define NEXT_FRAG(frag) NEXT_FRAG_OFS(frag, frag->offnext)
#define NEXT_FRAG_WA(frag) NEXT_FRAG_WA_OFS(frag, frag->offnext)

int copy_frags_list(struct frags_list *from, struct frags_list **pto);

int free_frags_list(struct frags_list *frags_list);


int cut_frags(struct frags_list **pfrags_list, unsigned long from,
		unsigned long blocks);

int pick_frags(struct frags_list **pfrags_list, unsigned long from,
		unsigned long blocks);

int frags_list2array (struct frags_list *frags_list, 
		struct any_file_frags **pfile_frags);

#endif /*_FRAGS_FUNCS_H*/

