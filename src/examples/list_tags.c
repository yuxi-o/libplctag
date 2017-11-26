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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../lib/libplctag.h"
#include "utils.h"


#define TAG_STRING_SIZE (200)
#define TIMEOUT_MS (5000)

void usage()
{
    printf("Usage: list_tags <PLC path>\nExample: list_tags 10.1.2.3,1,0\n");
    exit(1);
}

int main(int argc, char **argv)
{
    tag_id tag;
    int rc = PLCTAG_STATUS_OK;
    int offset = 0;
    char tag_string[TAG_STRING_SIZE] = {0,};

    if(argc < 2) {
        usage();
    }

    if(!argv[1] || strlen(argv[1]) == 0) {
        printf("Hostname or IP address must not be zero length!\n");
        usage();
    }

    snprintf(tag_string, TAG_STRING_SIZE-1,"plc=AB ControlLogix&path=%s&tag=@tags&debug=3",argv[1]);

    printf("Using tag string: %s\n", tag_string);

    tag = plc_tag_create(tag_string, TIMEOUT_MS);
    if(tag < 0) {
        printf("Unable to open tag!  Return code %s\n",plc_tag_decode_error(tag));
        usage();
    }

    rc = plc_tag_read(tag, TIMEOUT_MS);
    if(rc != PLCTAG_STATUS_OK) {
        printf("Unable to read tag!  Return code %s\n",plc_tag_decode_error(tag));
        usage();
    }

    do {
        uint32_t tag_instance_id = 0;
        uint16_t tag_name_len =0;
        uint16_t tag_type = 0;
        char tag_name[TAG_STRING_SIZE * 2] = {0,};

        tag_instance_id = plc_tag_get_int32(tag, offset);
        offset += 4;
        tag_name_len = plc_tag_get_int16(tag, offset);
        offset += 2;

        for(int i=0;i < (int)tag_name_len && i<((TAG_STRING_SIZE*2)-1);i++) {
            tag_name[i] = plc_tag_get_int8(tag,offset);
            offset++;
            tag_name[i+1] = 0;
        }

        tag_type = plc_tag_get_int16(tag, offset);
        offset += 2;

        printf("Tag name=%s, tag instance ID=%x, tag type=%x\n",tag_name, tag_instance_id, tag_type);
    } while(rc == PLCTAG_STATUS_OK && offset < plc_tag_get_size(tag));

    plc_tag_destroy(tag);

    return 0;
}
