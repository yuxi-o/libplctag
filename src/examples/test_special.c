/***************************************************************************
 *   Copyright (C) 2017 by Kyle Hayes                                      *
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
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#include <stdio.h>
#include "../lib/libplctag.h"
#include <platform.h>


void test_version(void)
{
    tag_id tag = PLC_TAG_NULL;
    int i;
    int ver[3] = {0,};
    int rc = PLCTAG_STATUS_OK;

    fprintf(stderr,"Testing version tag.\n");

    tag = plc_tag_create("plc=system&name=version&debug=4", 0);

    if(!tag) {
        fprintf(stderr,"ERROR: Could not create tag!\n");
        return;
    }

    while((rc = plc_tag_status(tag)) == PLCTAG_STATUS_PENDING) {
        sleep_ms(10);
    }

    if(rc != PLCTAG_STATUS_OK) {
        fprintf(stderr,"Unable to open version tag!");
        plc_tag_destroy(tag);
        return;
    }

    plc_tag_read(tag, 0);

    for(i=0; i < 3; i++) {
        ver[i] = (int)plc_tag_get_int32(tag,i*4);
    }

    fprintf(stderr,"Library version %d.%d.%d\n", ver[0],ver[1],ver[2]);

    plc_tag_destroy(tag);
}



void test_debug(void)
{
    tag_id tag = PLC_TAG_NULL;
    int old_debug, new_debug;
    int rc = PLCTAG_STATUS_OK;

    fprintf(stderr,"Testing debug tag.\n");

    tag = plc_tag_create("plc=system&name=debug&debug=4", 0);

    if(!tag) {
        fprintf(stderr,"ERROR: Could not create tag!\n");
        return;
    }

    while((rc = plc_tag_status(tag)) == PLCTAG_STATUS_PENDING) {
        sleep_ms(10);
    }

    if(rc != PLCTAG_STATUS_OK) {
        fprintf(stderr,"Unable to open debug tag!");
        plc_tag_destroy(tag);
        return;
    }

    plc_tag_read(tag, 0);
    rc = plc_tag_status(tag);
    if(rc != PLCTAG_STATUS_OK) {
        fprintf(stderr,"Unable to write debug tag!");
        plc_tag_destroy(tag);
        return;
    }

    old_debug = plc_tag_get_int32(tag, 0);

    fprintf(stderr,"Current debug level is %d\n",old_debug);

    new_debug = (old_debug == 4 ? 3 : 4);

    plc_tag_set_int32(tag, 0, new_debug);

    plc_tag_write(tag, 0);

    rc = plc_tag_status(tag);
    if(rc != PLCTAG_STATUS_OK) {
        fprintf(stderr,"Unable to write debug tag!");
        plc_tag_destroy(tag);
        return;
    }

    plc_tag_read(tag, 0);

    rc = plc_tag_status(tag);
    if(rc != PLCTAG_STATUS_OK) {
        fprintf(stderr,"Unable to read debug tag!");
        plc_tag_destroy(tag);
        return;
    }

    new_debug = plc_tag_get_int32(tag,0);

    fprintf(stderr,"Now debug level is %d\n",new_debug);

    new_debug = old_debug;

    plc_tag_set_int32(tag, 0, new_debug);

    plc_tag_write(tag, 0);

    rc = plc_tag_status(tag);
    if(rc != PLCTAG_STATUS_OK) {
        fprintf(stderr,"Unable to write debug tag!");
        plc_tag_destroy(tag);
        return;
    }

    plc_tag_read(tag, 0);

    rc = plc_tag_status(tag);
    if(rc != PLCTAG_STATUS_OK) {
        fprintf(stderr,"Unable to read debug tag!");
        plc_tag_destroy(tag);
        return;
    }

    new_debug = plc_tag_get_int32(tag,0);

    fprintf(stderr,"Reset debug level to %d\n",new_debug);

    plc_tag_destroy(tag);
}









int main()
{
    test_version();

    test_debug();

    return 0;
}


