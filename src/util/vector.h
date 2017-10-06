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


typedef rc_ptr rc_vector;



extern rc_vector vector_create(int capacity, int max_inc);
extern int vector_length(rc_vector vec);
extern int vector_put(rc_vector vec, int index, rc_ptr ref);
extern rc_ptr vector_get(rc_vector vec, int index);
extern rc_ptr vector_remove(rc_vector vec, int index);



#endif
