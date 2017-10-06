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


#include <lib/libplctag.h>
#include <platform.h>
#include <util/refcount.h>
#include <util/debug.h>
#include <util/queue.h>
#include <util/refcount.h>
#include <util/vector.h>





inline queue_ref queue_create(int capacity, int max_inc)
{
    return RC_CAST(queue_ref, vector_create(capacity, max_inc));
}



inline int queue_length(queue_ref q)
{
    return vector_length(RC_CAST(vector_ref, q));
}



inline int queue_put_impl(queue_ref q, rc_ref data_ref)
{
    vector_ref vec = RC_CAST(vector_ref, q);

    return vector_put(vec, vector_length(vec), data_ref);
}



inline rc_ref queue_get(queue_ref q)
{
    vector_ref vec = RC_CAST(vector_ref, q);

    return vector_remove(vec, 0);
}

