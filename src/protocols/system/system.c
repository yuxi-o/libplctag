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
#include <util/refcount.h>
#include <util/resource.h>
#include <lib/lib.h>
#include <lib/libplctag.h>


#define MAX_SYSTEM_TAG_SIZE (30)

struct system_tag_t {
    struct plc_t base_plc;

    int64_t creation_time;
};

typedef struct system_tag_t *system_tag_p;



static void plc_destroy(void *tag_arg, int arg_count, void **args);


static int debug_abort(plc_p plc, tag_p tag);
static int debug_read(plc_p plc, tag_p tag);
static int debug_status(plc_p plc, tag_p tag);
static int debug_write(plc_p plc, tag_p tag);

static int version_abort(plc_p plc, tag_p tag);
static int version_read(plc_p plc, tag_p tag);
static int version_status(plc_p plc, tag_p tag);
static int version_write(plc_p plc, tag_p tag);



plc_p system_plc_create(tag_p tag)
{
    plc_p plc = NULL;
    attr attribs = NULL;
    const char *name = NULL;
    bytebuf_p data = NULL;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO,"Starting.");

    attribs = tag_get_attribs(tag);
    if(!attribs) {
        pdebug(DEBUG_WARN,"Tag passed with NULL attributes!");
        return NULL;
    }

    /* check the name, if none given, punt. */
    name = attr_get_str(attribs,"name",NULL);
    if(!name || str_length(name) < 1) {
        pdebug(DEBUG_ERROR, "System tag name is empty or missing!");
        return NULL;
    }

    pdebug(DEBUG_DETAIL,"Creating special tag %s", name);

    /*
     * allocate memory for the new PLC.  Do this first so that
     * we have a vehicle for returning status.
     */

    plc = rc_alloc(sizeof(struct plc_t), plc_destroy);
    if(!plc) {
        pdebug(DEBUG_ERROR,"Unable to allocate memory for system plc!");
        return NULL;
    }

    /* create a data buffer for the tag. */
    data = bytebuf_create(12,0x10,0x3210,0x76543210,0x3210,0x7654321);
    if(!data) {
        pdebug(DEBUG_ERROR,"Unable to allocate new byte buf for data!");
        rc_dec(plc);
        return NULL;
    }

    rc = tag_set_data(tag, data);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set data buffer in tag!");
        rc_dec(plc);
        return NULL;
    }

    /*
     * Set the vtable up depending on the name.
     */
    if(str_cmp_i(name,"debug") == 0) {
        plc->tag_abort = debug_abort;
        plc->tag_read = debug_read;
        plc->tag_status = debug_status;
        plc->tag_write = debug_write;
    } else if(str_cmp_i(name,"version") == 0) {
        plc->tag_abort = version_abort;
        plc->tag_read = version_read;
        plc->tag_status = version_status;
        plc->tag_write = version_write;
    } else {
        pdebug(DEBUG_WARN,"Unknown tag %s!",name);
        rc_dec(plc);
        return NULL;
    }

    pdebug(DEBUG_INFO,"Done");

    return plc;
}




/***********************************************************************
 ************************** Debug Functions ****************************
 **********************************************************************/

int debug_abort(plc_p plc, tag_p tag)
{
    (void)plc;
    (void)tag;

    return PLCTAG_STATUS_OK;
}


int debug_read(plc_p plc, tag_p tag)
{
    int rc;
    int32_t debug_level;
    bytebuf_p data;

    (void)plc;

    /* check the tag data buffer. */
    data = tag_get_data(tag);
    if(!data) {
        pdebug(DEBUG_WARN,"No data buffer in tag!");
        return PLCTAG_ERR_NO_DATA;
    }

    /* get the debug level */
    debug_level = (int32_t)get_debug_level();

    rc = bytebuf_set_cursor(data, 0);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error setting tag data cursor!");
        return rc;
    }

    /* get the new level */
    rc = bytebuf_set_int32(data, debug_level);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error writing the debug level into the tag data!");
        return rc;
    }

    pdebug(DEBUG_INFO,"Debug level %d",(int)debug_level);

    return PLCTAG_STATUS_OK;
}


int debug_status(plc_p plc, tag_p tag)
{
    (void)plc;
    (void)tag;

    return PLCTAG_STATUS_OK;
}


int debug_write(plc_p plc, tag_p tag)
{
    int rc;
    int32_t debug_level;
    bytebuf_p data;

    (void)plc;

    data = tag_get_data(tag);
    if(!data) {
        pdebug(DEBUG_WARN,"Tag has no data!");
        return PLCTAG_ERR_NO_DATA;
    }

    rc = bytebuf_set_cursor(data, 0);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error setting tag data cursor!");
        return rc;
    }

    /* get the new level */
    rc = bytebuf_get_int32(data, &debug_level);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error getting new debug level!");
        return rc;
    }

    pdebug(DEBUG_INFO,"Old debug level %d, setting to %d",(int)get_debug_level(), (int)debug_level);

    /* set the debug level */
    set_debug_level((int)debug_level);

    return PLCTAG_STATUS_OK;
}


/***********************************************************************
 ************************* Version Functions ***************************
 **********************************************************************/

int version_abort(plc_p plc, tag_p tag)
{
    (void)plc;
    (void)tag;

    return PLCTAG_STATUS_OK;
}


int version_read(plc_p plc, tag_p tag)
{
    int rc;
    bytebuf_p data;

    (void)plc;

    /* check the tag data buffer. */
    data = tag_get_data(tag);
    if(!data) {
        pdebug(DEBUG_WARN,"No data buffer in tag!");
        return PLCTAG_ERR_NO_DATA;
    }

    rc = bytebuf_set_cursor(data, 0);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error setting tag data cursor!");
        return rc;
    }

    /* get the version info */
    for(int i=0; i < 3; i++) {
        rc = bytebuf_set_int32(data, (int32_t)VERSION_ARRAY[i]);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Error writing the version info into the tag data!");
            return rc;
        }
    }

    return PLCTAG_STATUS_OK;
}


int version_status(plc_p plc, tag_p tag)
{
    (void)plc;
    (void)tag;

    return PLCTAG_STATUS_OK;
}


int version_write(plc_p plc, tag_p tag)
{
    (void)plc;
    (void)tag;

    return PLCTAG_ERR_NOT_IMPLEMENTED;
}


/***********************************************************************
 ************************** Helper Functions ***************************
 **********************************************************************/

void plc_destroy(void *tag_arg, int arg_count, void **args)
{
    (void)tag_arg;
    (void)arg_count;
    (void)args;

    /* nothing to do.   rc_dec will clean up the memory. */
}


