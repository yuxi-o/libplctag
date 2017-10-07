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
#include <util/refcount.h>

typedef rc_ptr rc_bytebuf;

extern rc_bytebuf bytebuf_create(int initial_cap);
extern int bytebuf_set_cursor(rc_bytebuf buf, int cursor);
extern int bytebuf_put(rc_bytebuf buf, uint8_t data);
extern int bytebuf_get(rc_bytebuf buf, uint8_t *data);
extern int bytebuf_size(rc_bytebuf buf);
extern uint8_t *bytebuf_get_buffer(rc_bytebuf buf);

#endif
