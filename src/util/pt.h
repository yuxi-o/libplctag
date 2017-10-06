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

/*
 * Test code for Protothread implementation.
 *
 * This is based on Adam Dunkel's Protothreads.
 *
 */

#ifndef __UTIL__PT_H__
#define __UTIL__PT_H__

#include <platform.h>

#define PT_TERMINATE (0)
#define PT_RESUME (1)


/*
 * This is used like this:
 *
 * PT_FUNC(my_func)
 * PT_BODY
 *     ... some code using PT_YIELD etc. ...
 * PT_END
 *
 *
 * Then in other code, create the new PT.
 *
 * pt my_pt = pt_create(my_func,some_args);
 * ... PT runs in the background. ...
 * rc_dec(my_pt);
 */

#define PT_FUNC(name) int name(int *pt_line, void *args) {

#define PT_BODY \
    switch(*pt_line) {                                                 \
        case 0:

#define PT_EXIT \
    do { *pt_line = 0; return PT_TERMINATE; } while(0)

#define PT_END \
    default: \
        break; \
    } \
    PT_EXIT; \
}

#define PT_YIELD do {*pt_line = __LINE__; return PT_RESUME; case __LINE__: } while(0)
#define PT_WAIT_WHILE(cond) do {*pt_line = __LINE__; case __LINE__: if((cond)) return PT_RESUME; } while(0)
#define PT_WAIT_UNTIL(cond) do {*pt_line = __LINE__; case __LINE__: if(!(cond)) return PT_RESUME; } while(0)


typedef rc_ptr rc_protothread;

typedef int (*pt_func)(int *pt_line, rc_ptr arg_ref);

extern rc_protothread pt_create(pt_func func, rc_ptr arg_ref);


/* needed for set up of the PT service */
extern int pt_service_init(void);
extern void pt_service_teardown(void);



#endif
