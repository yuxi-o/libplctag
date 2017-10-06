/***************************************************************************
 *   Copyright (C) 2017 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library/Lesser General Public License as*
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef __UTIL_VECTOR_H__
#define __UTIL_VECTOR_H__ 1


/*
 * The elements stored in this vector must be created via rc_alloc.
 * The vector uses references to keep the memory around.
 */

#include <platform.h>
#include <util/refcount.h>



RC_MAKE_TYPE(vector_ref);

extern vector_ref vector_create(int capacity, int max_inc);
extern int vector_length(vector_ref vec);

#define vector_put(vec, index, ref) vector_put_impl(vec, index, RC_CAST(rc_ref,ref))
extern int vector_put_impl(vector_ref vec, int index, rc_ref ref);

extern rc_ref vector_get(vector_ref vec, int index);
extern rc_ref vector_remove(vector_ref vec, int index);

//~ #define RC_VECTOR_NULL RC_MAKE_NULL(vector_ref)
#define RC_VECTOR_NULL (RC_CAST(vector_ref, RC_REF_NULL))


#endif
