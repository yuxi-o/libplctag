/***************************************************************************
 *   Copyright (C) 2016 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
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


#ifndef __UTIL__REFCOUNT_H__
#define __UTIL__REFCOUNT_H__ 1

#include <platform.h>

#define RC_MAKE_TYPE(name) typedef struct { int *counter; } name

#define RC_MAKE_NULL(type) ((type){.counter = NULL})



//~ #define RC_REF_NULL ((rc_ref){.counter = NULL})

#define _RC_CAST_type(t) t
#define _RC_CAST_val(v) v
#define RC_CAST(type, val) ((type){.counter = (val).counter})

//~ #define RC_MOVE(rDest, rSrc) do {(rDest).counter = (rSrc).counter; (rSrc).counter = NULL; } while(0)
//~ #define RC_STRONG_COPY(rDest, rSrc) do {(rDest).counter = (rSrc).counter; rc_strong(rDest); } while(0)
//~ #define RC_WEAK_COPY(rDest, rSrc) do {(rDest).counter = (rSrc).counter; } while(0)


RC_MAKE_TYPE(rc_ref);

#define RC_REF_NULL RC_MAKE_NULL(rc_ref)


#define rc_make_ref(data, cleanup_func) rc_make_ref_impl(__func__, __LINE__, data, cleanup_func)
extern rc_ref rc_make_ref_impl(const char *func, int line_num, void *data, void (*cleanup_func)(void *));

#define rc_strong(ref) rc_strong_impl(__func__, __LINE__, RC_CAST(rc_ref, ref))
extern rc_ref rc_strong_impl(const char *func, int line_num, rc_ref ref);

#define rc_weak(ref) rc_weak_impl(__func__, __LINE__, RC_CAST(rc_ref, ref))
extern rc_ref rc_weak_impl(const char *func, int line_num, rc_ref ref);

#define rc_release(ref) rc_release_impl(__func__, __LINE__, RC_CAST(rc_ref, ref))
extern rc_ref rc_release_impl(const char *func, int line_num, rc_ref ref);

#define rc_deref(ref) rc_deref_impl(__func__, __LINE__, RC_CAST(rc_ref, ref))
extern void *rc_deref_impl(const char *func, int line_num, rc_ref ref);




#endif
