/* find_next_zero_bit.h: fallback find next zero bit implementation
 *
 * Based on find_next_bit.c
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ANY_FIND_NEXT_ZERO_BIT_H
#define _ANY_FIND_NEXT_ZERO_BIT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int find_next_zero_bit(const unsigned long *addr, int size, int offset);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /*_ANY_FIND_NEXT_ZERO_BIT_H*/
