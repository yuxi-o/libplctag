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




/*
 * All implementations must "subclass" this in some manner.
 */

typedef struct impl_tag_t *impl_tag_p;

struct impl_vtable {
	int (*abort)(impl_tag_p tag);
	int (*get_status)(impl_tag_p tag);
	int (*start_read)(impl_tag_p tag);
	int (*start_write)(impl_tag_p tag);

	int (*get_size)(impl_tag_p tag);

	int8_t (*get_int8)(impl_tag_p tag, int offset);
	int16_t (*get_int16)(impl_tag_p tag, int offset);
	int32_t (*get_int32)(impl_tag_p tag, int offset);
	int64_t (*get_int64)(impl_tag_p tag, int offset);
	float (*get_float32)(impl_tag_p tag, int offset);
	double (*get_float64)(impl_tag_p tag, int offset);

	int (*set_int8)(impl_tag_p tag, int offset, int8_t val);
	int (*set_int16)(impl_tag_p tag, int offset, int16_t val);
	int (*set_int32)(impl_tag_p tag, int offset, int32_t val);
	int (*set_int64)(impl_tag_p tag, int offset, int64_t val);
	int (*set_float32)(impl_tag_p tag, int offset, float val);
	int (*set_float64)(impl_tag_p tag, int offset, double val);
};

struct impl_tag_t {
	struct impl_vtable *vtable;
};



/* the following may need to be used where the tag is already mapped or is not yet mapped */
extern int lib_init(void);
extern void lib_teardown(void);

//~ extern int plc_tag_abort_mapped(plc_tag_p tag);
//~ extern int plc_tag_destroy_mapped(plc_tag_p tag);
//~ extern int plc_tag_status_mapped(plc_tag_p tag);


#endif
