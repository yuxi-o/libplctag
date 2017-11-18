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


static int debug_op_dispatch(tag_p tag, void *plc_arg, tag_operation op);
static int debug_abort(void* plc, tag_p tag);
static int debug_read(void* plc, tag_p tag);
static int debug_status(void* plc, tag_p tag);
static int debug_write(void* plc, tag_p tag);

static int version_op_dispatch(tag_p tag, void *plc_arg, tag_operation op);
static int version_abort(void* plc, tag_p tag);
static int version_read(void* plc, tag_p tag);
static int version_status(void* plc, tag_p tag);
static int version_write(void* plc, tag_p tag);



int system_tag_create(tag_p tag)
{
    attr attribs = NULL;
    const char *name = NULL;
    bytebuf_p data = NULL;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO,"Starting.");

    attribs = tag_get_attribs(tag);
    if(!attribs) {
        pdebug(DEBUG_WARN,"Tag passed with NULL attributes!");
        return PLCTAG_ERR_NO_DATA;
    }

    /* check the name, if none given, punt. */
    name = attr_get_str(attribs,"name",NULL);
    if(!name || str_length(name) < 1) {
        pdebug(DEBUG_ERROR, "System tag name is empty or missing!");
        return PLCTAG_ERR_BAD_PARAM;
    }

    pdebug(DEBUG_DETAIL,"Creating special tag %s", name);

    /* create a data buffer for the tag. */
    data = bytebuf_create(12,0x10,0x3210,0x76543210,0x3210,0x7654321);
    if(!data) {
        pdebug(DEBUG_ERROR,"Unable to allocate new byte buf for data!");
        return PLCTAG_ERR_NO_MEM;
    }

    rc = tag_set_bytebuf(tag, data);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set data buffer in tag!");
        return PLCTAG_ERR_CREATE;
    }

    /*
     * Set the operation dispatcher up depending on the name.
     */
    if(str_cmp_i(name,"debug") == 0) {
        rc = tag_set_impl_op_func(tag, debug_op_dispatch);
    } else if(str_cmp_i(name,"version") == 0) {
        rc = tag_set_impl_op_func(tag, version_op_dispatch);
    } else {
        pdebug(DEBUG_WARN,"Unknown tag %s!",name);
        return PLCTAG_ERR_UNSUPPORTED;
    }


    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set implementation operation function in tag!");
        return PLCTAG_ERR_CREATE;
    }

    rc = tag_set_impl_data(tag, NULL);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set implementation data in tag!");
        return PLCTAG_ERR_CREATE;
    }

    pdebug(DEBUG_INFO,"Done");

    return rc;
}




/***********************************************************************
 ************************** Debug Functions ****************************
 **********************************************************************/


int debug_op_dispatch(tag_p tag, void *plc_arg, tag_operation op)
{
    int rc = PLCTAG_STATUS_OK;

    switch(op) {
        case TAG_OP_ABORT:
            rc = debug_abort(plc_arg, tag);
            break;

        case TAG_OP_READ:
            rc = debug_read(plc_arg, tag);
            break;

        case TAG_OP_STATUS:
            rc = debug_status(plc_arg, tag);
            break;

        case TAG_OP_WRITE:
            rc = debug_write(plc_arg, tag);
            break;

        default:
            pdebug(DEBUG_WARN,"Operation %d not implemented!",op);
            rc = PLCTAG_ERR_NOT_IMPLEMENTED;
            break;
    }

    return rc;
}



int debug_abort(void* plc, tag_p tag)
{
    (void)plc;
    (void)tag;

    return PLCTAG_STATUS_OK;
}


int debug_read(void* plc, tag_p tag)
{
    int rc;
    int32_t debug_level;
    bytebuf_p data;

    (void)plc;

    /* check the tag data buffer. */
    data = tag_get_bytebuf(tag);
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
    rc = bytebuf_marshal(data, BB_I32, debug_level);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error writing the debug level into the tag data!");
        return rc;
    }

    pdebug(DEBUG_INFO,"Debug level %d",(int)debug_level);

    return PLCTAG_STATUS_OK;
}


int debug_status(void* plc, tag_p tag)
{
    (void)plc;
    (void)tag;

    return PLCTAG_STATUS_OK;
}


int debug_write(void* plc, tag_p tag)
{
    int rc;
    int32_t debug_level;
    bytebuf_p data;

    (void)plc;

    data = tag_get_bytebuf(tag);
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
    rc = bytebuf_unmarshal(data, BB_I32, &debug_level);
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

int version_op_dispatch(tag_p tag, void *plc_arg, tag_operation op)
{
    int rc = PLCTAG_STATUS_OK;

    switch(op) {
        case TAG_OP_ABORT:
            rc = version_abort(plc_arg, tag);
            break;

        case TAG_OP_READ:
            rc = version_read(plc_arg, tag);
            break;

        case TAG_OP_STATUS:
            rc = version_status(plc_arg, tag);
            break;

        case TAG_OP_WRITE:
            rc = version_write(plc_arg, tag);
            break;

        default:
            pdebug(DEBUG_WARN,"Operation %d not implemented!",op);
            rc = PLCTAG_ERR_NOT_IMPLEMENTED;
            break;
    }

    return rc;
}



int version_abort(void* plc, tag_p tag)
{
    (void)plc;
    (void)tag;

    return PLCTAG_STATUS_OK;
}


int version_read(void* plc, tag_p tag)
{
    int rc;
    bytebuf_p data;

    (void)plc;

    /* check the tag data buffer. */
    data = tag_get_bytebuf(tag);
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
    rc = bytebuf_marshal(data, BB_I32, VERSION_ARRAY[0], BB_I32, VERSION_ARRAY[1], BB_I32, VERSION_ARRAY[2]);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error writing the version info into the tag data!");
        return rc;
    }

    return PLCTAG_STATUS_OK;
}


int version_status(void* plc, tag_p tag)
{
    (void)plc;
    (void)tag;

    return PLCTAG_STATUS_OK;
}


int version_write(void* plc, tag_p tag)
{
    (void)plc;
    (void)tag;

    return PLCTAG_ERR_NOT_IMPLEMENTED;
}



