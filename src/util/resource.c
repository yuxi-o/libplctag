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

#include <stdio.h>
#include <lib/libplctag.h>
#include <platform.h>
#include <util/debug.h>
#include <util/hashtable.h>
#include <util/refcount.h>



static volatile rc_hashtable resources = NULL;
static volatile mutex_p resource_mutex = NULL;


/* FIXME - this does allocation each time a resource is retrieved. */
rc_ptr resource_get(const char *prefix, const char *name)
{
    int name_len = str_length(prefix) + str_length(name) + 1;
    char *resource_name = mem_alloc(name_len);
    rc_ptr resource = NULL;

    pdebug(DEBUG_DETAIL,"Starting with prefix %s and name %s", prefix, name);

    if(!resource_name) {
        pdebug(DEBUG_ERROR,"Unable to allocate new resource name string!");
        return resource;
    }

    snprintf(resource_name, name_len, "%s%s", prefix, name);

    critical_block(resource_mutex) {
        resource = hashtable_get(resources, resource_name, name_len);
        if(resource) {
            /* get a strong reference if we can. */
            resource = rc_strong(resource);

            if(!resource) {
                /* clean out the entry */
                resource = hashtable_remove(resources, resource_name, name_len);
                resource = rc_release(resource);
            }
        }
    }

    mem_free(resource_name);

    pdebug(DEBUG_DETAIL,"Resource%s found!",(resource ? "": " not"));

    return resource;
}



int resource_put(const char *prefix, const char *name, rc_ptr resource)
{
    int name_len = str_length(prefix) + str_length(name) + 1;
    char *resource_name = mem_alloc(name_len);
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL,"Starting with prefix %s and name %s", prefix, name);

    if(!resource_name) {
        pdebug(DEBUG_ERROR,"Unable to allocate new resource name string!");
        return PLCTAG_ERR_NO_MEM;
    }

    snprintf(resource_name, name_len, "%s%s", prefix, name);

    critical_block(resource_mutex) {
        rc_ptr weak_resource = rc_weak(resource);
        rc = hashtable_put(resources, resource_name, name_len, weak_resource);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Error inserting resource, %s",plc_tag_decode_error(rc));
            rc_release(weak_resource);
        }
    }

    mem_free(resource_name);

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}



int resource_remove(const char *prefix, const char *name)
{
    int name_len = str_length(prefix) + str_length(name) + 1;
    char *resource_name = mem_alloc(name_len);
    int rc = PLCTAG_STATUS_OK;
    rc_ptr resource = NULL;

    pdebug(DEBUG_DETAIL,"Starting with prefix %s and name %s", prefix, name);

    if(!resource_name) {
        pdebug(DEBUG_ERROR,"Unable to allocate new resource name string!");
        return PLCTAG_ERR_NO_MEM;
    }

    snprintf(resource_name, name_len, "%s%s", prefix, name);

    critical_block(resource_mutex) {
        resource = hashtable_remove(resources, resource_name, name_len);
    }

    rc_release(resource);

    mem_free(resource_name);

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}


int resource_service_init(void)
{
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO,"Initializing Resource utility.");

    /* this is a mutex used to synchronize most activities in this protocol */
    rc = mutex_create((mutex_p*)&resource_mutex);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to create resource mutex!");
        return rc;
    }

    /* create the hashtable in which we will have the resources stored. */
    resources = hashtable_create(200); /* MAGIC */
    if(!resources || !rc_deref(resources)) {
        pdebug(DEBUG_ERROR,"Unable to allocate a hashtable!");
        return PLCTAG_ERR_CREATE;
    }

    pdebug(DEBUG_INFO,"Finished initializing Resource utility.");

    return rc;
}


void resource_service_teardown(void)
{
    pdebug(DEBUG_INFO,"Tearing down Resource utility.");

    pdebug(DEBUG_INFO,"Tearing down resource hashtable.");

    resources = rc_release(resources);

    pdebug(DEBUG_INFO,"Tearing down resource mutex.");

    mutex_destroy((mutex_p*)&resource_mutex);

    pdebug(DEBUG_INFO,"Done.");
}

