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
#include <lib/lib.h>
#include <lib/libplctag.h>
//#include <system/tag.h>


#define MAX_SYSTEM_TAG_SIZE (30)

struct system_tag_t {
    struct impl_tag_t base_tag;

    int64_t creation_time;
};

typedef struct system_tag_t *system_tag_p;


static int abort_operation(impl_tag_p tag);
static int size_debug(impl_tag_p tag);
static int size_version(impl_tag_p tag);
static int get_status(impl_tag_p tag);
static int read_tag(impl_tag_p tag);
static int write_tag(impl_tag_p tag);
static int get_int_debug(impl_tag_p tag, int offset, int size, int64_t *val);
static int set_int_debug(impl_tag_p tag, int offset, int size, int64_t val);
static int get_int_version(impl_tag_p tag, int offset, int size, int64_t *val);
static int set_int_version(impl_tag_p tag, int offset, int size, int64_t val);
static int get_double(impl_tag_p tag, int offset, int size, double *val);
static int set_double(impl_tag_p tag, int offset, int size, double val);


static void tag_destroy(int arg_count, void **args);

/*
VTable:

    int (*abort)(impl_tag_p tag);
    int (*get_size)(impl_tag_p tag);
    int (*get_status)(impl_tag_p tag);
    int (*start_read)(impl_tag_p tag);
    int (*start_write)(impl_tag_p tag);

    int (*get_int)(impl_tag_p tag, int offset, int size, int64_t *val);
    int (*get_double)(impl_tag_p tag, int offset, int size, double *val);
    int (*set_int)(impl_tag_p tag, int offset, int size, int64_t val);
    int (*set_double)(impl_tag_p tag, int offset, int size, double val);
*/

struct impl_vtable debug_tag_vtable = {abort_operation, size_debug, get_status, read_tag, write_tag,
                                        get_int_debug, set_int_debug, get_double, set_double};

struct impl_vtable version_tag_vtable = {abort_operation, size_version, get_status, read_tag, write_tag,
                                        get_int_version, set_int_version, get_double, set_double};






impl_tag_p system_tag_create(attr attribs)
{
    system_tag_p tag = NULL;
    const char *name = attr_get_str(attribs, "name", NULL);

    pdebug(DEBUG_INFO,"Starting.");

    /* check the name, if none given, punt. */
    if(!name || str_length(name) < 1) {
        pdebug(DEBUG_ERROR, "System tag name is empty or missing!");
        return NULL;
    }

    pdebug(DEBUG_DETAIL,"Creating special tag %s", name);

    /*
     * allocate memory for the new tag.  Do this first so that
     * we have a vehicle for returning status.
     */

    tag = rc_alloc(sizeof(struct system_tag_t), tag_destroy, 0);
    if(!tag) {
        pdebug(DEBUG_ERROR,"Unable to allocate memory for system tag!");
        return NULL;
    }

    tag->creation_time = time_ms();

    /*
     * Set the vtable up depending on the name.
     */
    if(str_cmp_i(name,"debug") == 0) {
        tag->base_tag.vtable = &debug_tag_vtable;
    } else if(str_cmp_i(name,"version") == 0) {
        tag->base_tag.vtable = &version_tag_vtable;
    } else {
        pdebug(DEBUG_WARN,"Unknown tag %s!",name);
        tag_destroy(1, (void**)&tag);
        return NULL;
    }

    pdebug(DEBUG_INFO,"Done");

    return (impl_tag_p)tag;
}


int abort_operation(impl_tag_p tag)
{
    (void)tag;

    return PLCTAG_STATUS_OK;
}


int get_status(impl_tag_p arg)
{
    system_tag_p tag = (system_tag_p)arg;

    /* fake some pending set up time. */
    if(time_ms() < (tag->creation_time + 100)) {
        return PLCTAG_STATUS_PENDING;
    } else {
        return PLCTAG_STATUS_OK;
    }
}


int read_tag(impl_tag_p tag)
{
    (void)tag;

    return PLCTAG_STATUS_OK;
}


int write_tag(impl_tag_p tag)
{
    (void)tag;

    return PLCTAG_STATUS_OK;
}


int size_debug(impl_tag_p tag)
{
    (void)tag;

    return sizeof(int32_t);
}


int size_version(impl_tag_p tag)
{
    (void)tag;

    return 3 * sizeof(int32_t);
}


int get_int_debug(impl_tag_p tag, int offset, int size, int64_t *val)
{
    (void)tag;

    if(!val) {
        return PLCTAG_ERR_NULL_PTR;
    }

    if(offset != 0) {
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    if(size != 4) {
        return PLCTAG_ERR_NOT_IMPLEMENTED;
    }

    *val = (int64_t)get_debug_level();

    return PLCTAG_STATUS_OK;
}


int set_int_debug(impl_tag_p tag, int offset, int size, int64_t val)
{
    (void)tag;

    if(!val) {
        return PLCTAG_ERR_NULL_PTR;
    }

    if(offset != 0) {
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    if(size != 4) {
        return PLCTAG_ERR_NOT_IMPLEMENTED;
    }

    set_debug_level((int)(val));

    return PLCTAG_STATUS_OK;
}


int get_int_version(impl_tag_p tag, int offset, int size, int64_t *val)
{
    (void)tag;

    if(!val) {
        return PLCTAG_ERR_NULL_PTR;
    }

    if(!(offset == 0 || offset == 4 || offset == 8)) {
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    if(size != 4) {
        return PLCTAG_ERR_NOT_IMPLEMENTED;
    }

    *val = (int64_t)VERSION_ARRAY[offset/4];

    return PLCTAG_STATUS_OK;
}

int set_int_version(impl_tag_p tag, int offset, int size, int64_t val)
{
    (void)tag;
    (void)offset;
    (void)size;
    (void)val;

    return PLCTAG_ERR_NOT_IMPLEMENTED;
}

int get_double(impl_tag_p tag, int offset, int size, double *val)
{
    (void)tag;
    (void)offset;
    (void)size;
    (void)val;

    return PLCTAG_ERR_NOT_IMPLEMENTED;
}

int set_double(impl_tag_p tag, int offset, int size, double val)
{
    (void)tag;
    (void)offset;
    (void)size;
    (void)val;

    return PLCTAG_ERR_NOT_IMPLEMENTED;
}




void tag_destroy(int arg_count, void **args)
{
    (void)arg_count;
    (void)args;

    /* nothing to do.   rc_dec will clean up the memory. */
}


