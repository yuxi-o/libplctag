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

#ifndef __UTIL_BYTEVECTOR_H__
#define __UTIL_BYTEVECTOR_H__ 1


typedef struct byte_vector_t *byte_vector_p;

extern byte_vector_p byte_vector_create(int capacity);
extern int byte_vector_length(byte_vector_p vec);
extern int byte_vector_put(byte_vector_p vec, int index, uint8_t val);
extern uint8_t byte_vector_get(byte_vector_p vec, int index);
extern int byte_vector_on_each(byte_vector_p vec, int (*callback_func)(byte_vector_p vec, int index, void **data, int arg_count, void **args), int num_args, ...);
extern void *byte_vector_remove(byte_vector_p vec, int index);
extern int byte_vector_destroy(byte_vector_p vec);



#endif
