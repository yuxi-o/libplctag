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

#ifndef __UTIL_BYTEORDER_H__
#define __UTIL_BYTEORDER_H__

#include <util/macros.h>

#define marshal_data(buf, buf_size, ...)  marshal_data_impl(buf, buf_size, (COUNT_NARG(__VA_ARGS__)-2), __VA_ARGS__)
extern int  marshal_data_impl(uint8_t *buf, size_t buf_size, int arg_count, ...);

#define unmarshal_data(buf, buf_size, ...)  unmarshal_data_impl(buf, buf_size, (COUNT_NARG(__VA_ARGS__)-2), __VA_ARGS__)
extern int  unmarshal_data_impl(uint8_t *buf, size_t buf_size, int arg_count, ...);

#endif
