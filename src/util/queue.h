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

#ifndef __UTIL_QUEUE_H__
#define __UTIL_QUEUE_H__


/*
 * The elements stored in this queue must be created via rc_alloc.
 * The queue uses references to keep the memory around.
 */

#include <platform.h>
#include <util/refcount.h>


typedef struct queue_t *queue_p;

extern queue_p queue_create(int capacity, int max_inc, rc_ref_type ref_type);
extern int queue_length(queue_p vec);
extern int queue_put(queue_p vec, void *data);
extern void *queue_get(queue_p vec);



#endif
