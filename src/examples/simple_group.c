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
#include <string.h>
#include "../lib/libplctag.h"
#include "utils.h"


#define TAG_STR_TEMPLATE "plc=AB ControlLogix&path=10.206.1.39,1,5&read_group=test_group&elem_count=1&tag=TestBIGArray[%d]&debug=4"
#define DATA_TIMEOUT (1000)
#define ELEM_SIZE (4)


#define NUM_TAGS (20)
tag_id tags[NUM_TAGS] = {0,};

int create_tags(void)
{
    char tag_string[128] = {0,};
    int rc = PLCTAG_STATUS_OK;

    for(int i=0; i < NUM_TAGS; i++) {
        memset(tag_string, 0, sizeof(tag_string));

        snprintf(tag_string, sizeof(tag_string),TAG_STR_TEMPLATE, i);

        printf("Creating tag with tag string: %s\n",tag_string);

        /* create the tag */
        tags[i] = plc_tag_create(tag_string, 0);

        /* everything OK? */
        if(tags[i] < 0) {
            fprintf(stderr,"ERROR: Could not create tag %d! error %s\n", i, plc_tag_decode_error(tags[i]));
            return tags[i];
        }
    }

    do {
        sleep_ms(1);

        rc = PLCTAG_STATUS_OK;

        for(int i=0; i < NUM_TAGS; i++) {
            int tmp_rc = plc_tag_status(tags[i]);

            if(tmp_rc != PLCTAG_STATUS_OK) {
                rc = tmp_rc;
            }
        }
    } while(rc == PLCTAG_STATUS_PENDING);

    return PLCTAG_STATUS_OK;
}


void destroy_tags(void)
{
    for(int i=0; i < NUM_TAGS; i++) {
        plc_tag_destroy(tags[i]);
    }
}

int main(int argc, char **argv)
{
    int rc = 0;
    int i = 0;
    int num_elements = 0;

    (void)argc;
    (void)argv;

    /* create the tags */
    rc = create_tags();

    /* everything OK? */
    if(rc != PLCTAG_STATUS_OK) {
        fprintf(stderr,"ERROR: Could not create tags! error %s\n", plc_tag_decode_error(rc));
        return 0;
    }

    /* get the data.  Test by requesting the first one. */
    rc = plc_tag_read(tags[0], DATA_TIMEOUT);
    if(rc != PLCTAG_STATUS_OK) {
        fprintf(stderr,"ERROR: Unable to read the data! Got error code %d: %s\n",rc, plc_tag_decode_error(rc));
        return 0;
    }

    /* print out the data */
    for(int t=0; t < NUM_TAGS; t++) {
        num_elements = (plc_tag_get_size(tags[t])/ELEM_SIZE);
        for(i=0; i < num_elements; i++) {
            printf("tag[%d] data[%d]=%d\n",t,i,plc_tag_get_int32(tags[t],(i*ELEM_SIZE)));
        }
    }

    /* we are done */
    destroy_tags();

    return 0;
}


