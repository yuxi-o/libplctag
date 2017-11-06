/***************************************************************************
 *   Copyright (C) 2015 by OmanTek                                         *
 *   Author Kyle Hayes  kylehayes@omantek.com                              *
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

/**************************************************************************
 * CHANGE LOG                                                             *
 *                                                                        *
 * 2012-02-23  KRH - Created file.                                        *
 *                                                                        *
 **************************************************************************/


#ifndef __LIBPLCTAG_TAG_H__
#define __LIBPLCTAG_TAG_H__




#include <lib/libplctag.h>
#include <platform.h>
#include <util/attr.h>
#include <util/bytebuf.h>
#include <util/refcount.h>



extern const char *VERSION;
extern const int VERSION_ARRAY[3];

typedef struct tag_t *tag_p;
//~ typedef struct plc_t *plc_p;

/*
 * All implementations must "subclass" this in some manner.
 */



//~ struct plc_t {
    //~ int (*tag_do_operation)(plc_p plc, tag_p tag, tag_operation op);
    //~ int (*tag_status)(plc_p plc, tag_p tag);
    //~ int (*tag_abort)(plc_p plc, tag_p tag);
    //~ int (*tag_read)(plc_p plc, tag_p tag);
    //~ int (*tag_write)(plc_p plc, tag_p tag);
//~ };


typedef enum {TAG_OP_NONE, TAG_OP_ABORT, TAG_OP_READ, TAG_OP_STATUS, TAG_OP_WRITE} tag_operation;

typedef int (*impl_operation_func)(tag_p tag, void *impl_data, tag_operation op);


/*
 * Internal API functions.
 *
 * These shield implementations from the interior details of generic tags.
 */

extern bytebuf_p tag_get_bytebuf(tag_p tag);
extern int tag_set_bytebuf(tag_p tag, bytebuf_p data);
extern int tag_set_impl_data(tag_p tag, void *impl_data);
extern int tag_set_impl_op_func(tag_p tag, impl_operation_func op_func);
extern attr tag_get_attribs(tag_p tag);

/* the following may need to be used where the tag is already mapped or is not yet mapped */
extern int lib_init(void);
extern void lib_teardown(void);


#endif
