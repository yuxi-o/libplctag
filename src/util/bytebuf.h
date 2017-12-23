/***************************************************************************
 *   Copyright (C) 2017 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
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

#ifndef __UTIL_BYTEBUF_H__
#define __UTIL_BYTEBUF_H__

#include <stdint.h>
#include <util/macros.h>
#include <util/refcount.h>


typedef struct bytebuf_t *bytebuf_p;

extern bytebuf_p bytebuf_create(int initial_cap, uint32_t int16_bo, uint32_t int32_bo, uint32_t int64_bo, uint32_t float32_bo, uint32_t float64_bo);
extern int bytebuf_set_cursor(bytebuf_p buf, int cursor);
extern int bytebuf_get_cursor(bytebuf_p buf);
extern int bytebuf_set_capacity(bytebuf_p buf, int capacity);
extern int bytebuf_get_size(bytebuf_p buf);
extern uint8_t *bytebuf_get_buffer(bytebuf_p buf);
extern int bytebuf_reset(bytebuf_p buf);
extern int bytebuf_destroy(bytebuf_p buf);

/* data accessors */
typedef enum {BB_I8, BB_U8, BB_I16, BB_U16, BB_I32, BB_U32, BB_I64, BB_U64, BB_F32, BB_F64, BB_BYTES } bytebuf_arg_type;

#define bytebuf_marshal(buf, ...) bytebuf_marshal_impl(buf, COUNT_NARG(__VA_ARGS__), __VA_ARGS__)
extern int bytebuf_marshal_impl(bytebuf_p buf, int arg_count, ...);

#define bytebuf_unmarshal(buf, ...) bytebuf_unmarshal_impl(buf, COUNT_NARG(__VA_ARGS__), __VA_ARGS__)
extern int bytebuf_unmarshal_impl(bytebuf_p buf, int arg_count, ...);



#endif
