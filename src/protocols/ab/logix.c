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
#include <util/resource.h>
#include <ab/ab.h>
#include <ab/logix.h>



#define PLC_RESOURCE_PREFIX "AB Logix PLC"


typedef rc_ptr rc_plc;

typedef struct {
    struct impl_tag_t base_tag;

    int read_requested;
    int write_requested;
    int abort_requested;

    rc_plc plc;

} logix_tag_t;

typedef logix_tag_t *logix_tag_p;

/*
We need to implement the following functions:

struct impl_vtable {
    int (*abort)(impl_tag_p tag);
    int (*get_size)(impl_tag_p tag);
    int (*get_status)(impl_tag_p tag);
    int (*start_read)(impl_tag_p tag);
    int (*start_write)(impl_tag_p tag);

    int (*get_int)(impl_tag_p tag, int offset, int size, int64_t *val);
    int (*set_int)(impl_tag_p tag, int offset, int size, int64_t val);
    int (*get_double)(impl_tag_p tag, int offset, int size, double *val);
    int (*set_double)(impl_tag_p tag, int offset, int size, double val);
};

*/



static void tag_destroy(void *arg);
static rc_plc find_plc(attr attribs);
static rc_plc create_plc(attr attribs);


static int abort_operation(impl_tag_p impl_tag);
static int get_size(impl_tag_p impl_tag);
static int get_status(impl_tag_p impl_tag);
static int start_read(impl_tag_p impl_tag);
static int start_write(impl_tag_p impl_tag);
static int get_int(impl_tag_p impl_tag, int offset, int size, int64_t *val);
static int set_int(impl_tag_p impl_tag, int offset, int size, int64_t val);
static int get_double(impl_tag_p impl_tag, int offset, int size, double *val);
static int set_double(impl_tag_p impl_tag, int offset, int size, double val);


static struct impl_vtable logix_vtable = { abort_operation, get_size, get_status, start_read, start_write,
                                           get_int, set_int, get_double, set_double };

rc_impl_tag logix_tag_create(attr attribs)
{
    logix_tag_p tag = NULL;
    rc_plc plc = NULL;
    rc_impl_tag result = NULL;

    /* FIXME */
    (void)attribs;

    pdebug(DEBUG_INFO,"Starting.");

    /* build the new tag. */
    tag = mem_alloc(sizeof(logix_tag_t));
    if(!tag) {
        pdebug(DEBUG_ERROR,"Unable to allocate memory for new tag!");
        return NULL;
    }

    /* set up the vtable for this kind of tag. */
    tag->base_tag.vtable = &logix_vtable;

    /* find the PLC we need for this.   This will create it if does not exist. */
    plc = find_plc(attribs);

    if(!plc || !rc_deref(plc)) {
        pdebug(DEBUG_WARN,"Unable to get PLC!");
        tag_destroy(tag);
        return NULL;
    }

    tag->plc = plc;

    /* wrap the tag in a reference. */
    result = rc_make_ref(tag, tag_destroy);
    if(!result || !rc_deref(result)) {
        pdebug(DEBUG_WARN,"Unable to create RC wrapper!");
        tag_destroy(tag);
        result = NULL;
    }

    pdebug(DEBUG_INFO,"Done.");

    return result;
}





/***********************************************************************
 *********************** Implementation Functions **********************
 **********************************************************************/

void tag_destroy(void *arg)
{
    logix_tag_p tag = arg;

    pdebug(DEBUG_INFO,"Starting.");

    if(!tag) {
        pdebug(DEBUG_WARN,"Destructor called with null pointer!");
        return;
    }

    mem_free(tag);

    pdebug(DEBUG_WARN,"Done.");
}

int abort_operation(impl_tag_p impl_tag)
{
    logix_tag_p tag = (logix_tag_p)impl_tag;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    tag->abort_requested = 1;

    return PLCTAG_STATUS_OK;
}

int get_size(impl_tag_p impl_tag)
{
    logix_tag_p tag = (logix_tag_p)impl_tag;
    int size = 0;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }


    return size;
}

int get_status(impl_tag_p impl_tag) {
    logix_tag_p tag = (logix_tag_p)impl_tag;
    int status = PLCTAG_STATUS_OK;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    return status;
}

int start_read(impl_tag_p impl_tag) {
    logix_tag_p tag = (logix_tag_p)impl_tag;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    tag->read_requested = 1;

    return PLCTAG_STATUS_OK;
}

int start_write(impl_tag_p impl_tag) {
    logix_tag_p tag = (logix_tag_p)impl_tag;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    tag->write_requested = 1;

    return PLCTAG_STATUS_OK;
}

int get_int(impl_tag_p impl_tag, int offset, int size, int64_t *val) {
    logix_tag_p tag = (logix_tag_p)impl_tag;
    int rc = PLCTAG_STATUS_OK;

    (void)offset;
    (void)size;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    *val = 1;

    return rc;
}

int set_int(impl_tag_p impl_tag, int offset, int size, int64_t val) {
    logix_tag_p tag = (logix_tag_p)impl_tag;
    int rc = PLCTAG_STATUS_OK;

    (void)offset;
    (void)size;
    (void)val;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    return rc;
}

int get_double(impl_tag_p impl_tag, int offset, int size, double *val) {
    logix_tag_p tag = (logix_tag_p)impl_tag;
    int rc = PLCTAG_STATUS_OK;

    (void)offset;
    (void)size;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    *val = 1.0;

    return rc;
}

int set_double(impl_tag_p impl_tag, int offset, int size, double val) {
    logix_tag_p tag = (logix_tag_p)impl_tag;
    int rc = PLCTAG_STATUS_OK;

    (void)offset;
    (void)size;
    (void)val;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    return rc;
}



rc_plc find_plc(attr attribs)
{
    const char *path = attr_get_str(attribs,"path","NONE");
    rc_plc plc = resource_get(PLC_RESOURCE_PREFIX, path);

    pdebug(DEBUG_INFO,"Starting with path %s", path);

    if(!plc) {
        pdebug(DEBUG_DETAIL,"Might need to create new PLC.");

        /* better try to make a PLC. */
        plc = create_plc(attribs);

        /* FIXME - check the return code! */
        if(resource_put(PLC_RESOURCE_PREFIX, path, plc) == PLCTAG_ERR_DUPLICATE) {
            /* oops hit race condition, need to dump this, there is one already. */
            pdebug(DEBUG_DETAIL,"Oops! Hit race condition and someone else created PLC already!");

            rc_release(plc);
            plc = resource_get(PLC_RESOURCE_PREFIX, path);
        }
    }

    return plc;
}


rc_plc create_plc(attr attribs)
{
    rc_plc plc = NULL;

    (void) attribs;

    /* FIXME */

    return plc;
}
