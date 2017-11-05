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


#include <platform.h>
#include <util/attr.h>
#include <util/debug.h>
#include <util/hashtable.h>
#include <util/refcount.h>
#include <ab/ab.h>
#include <ab/logix/logix.h>



impl_plc_p ab_plc_create(attr attribs)
{
    const char *plc = attr_get_str(attribs,"plc", attr_get_str(attribs, "cpu", "NONE"));

    if(str_cmp_i(plc,"lgx") == 0 || str_cmp_i(plc,"logix") == 0 || str_cmp_i(plc,"controllogix") == 0 || str_cmp_i(plc,"contrologix") == 0 || str_cmp_i(plc,"compactlogix") == 0) {
        return logix_tag_create(attribs);
    }

    return NULL;
}




int ab_init()
{
    pdebug(DEBUG_INFO,"Starting to set up AB protocol resources.");

    pdebug(DEBUG_INFO,"Done.");

    return PLCTAG_STATUS_OK;
}


void ab_teardown(void)
{
    pdebug(DEBUG_INFO,"Starting to teardown AB protocol resources.");

    pdebug(DEBUG_INFO,"Done.");
}



