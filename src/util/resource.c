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

#include <lib/libplctag.h>
#include <platform.h>
#include <util/debug.h>
#include <util/hashtable.h>
#include <util/refcount.h>



static volatile hashtable_p resource_by_name = NULL;
static volatile hashtable_p name_by_resource = NULL;
static volatile mutex_p resource_mutex = NULL;


static void resource_data_cleanup(void *arg);



/* FIXME - this does allocation each time a resource is retrieved. */
void * resource_get(const char *name)
{
    void * resource = NULL;
    int name_len = 0;

    if(!name) {
        pdebug(DEBUG_WARN,"Called with null name!");
        return NULL;
    }

    pdebug(DEBUG_DETAIL,"Starting with name %s", name);

    name_len = str_length(name) + 1;

    critical_block(resource_mutex) {
        resource = hashtable_get(resource_by_name, (void *)name, name_len);
        if(resource) {
            /* get a strong reference if we can. */
            resource = rc_inc(resource); /* FIXME - this locks another mutex, can we get inversion? */

            if(!resource) {
                /* clean out the entry */
                hashtable_remove(resource_by_name, (void *)name, name_len);
            }
        }
    }

    pdebug(DEBUG_DETAIL,"Resource%s found!",(resource ? "": " not"));

    return resource;
}



int resource_put(const char *name, void * resource)
{
    int name_len = 0;
    int rc = PLCTAG_STATUS_OK;
    void *tmp_ref = NULL;

    if(!name) {
        pdebug(DEBUG_WARN,"Called with null name!");
        return PLCTAG_ERR_NULL_PTR;
    }

    pdebug(DEBUG_DETAIL,"Starting with name %s", name);

    name_len = str_length(name) + 1;

    /* set up clean up function on resource. */
    tmp_ref = rc_make_ref(resource, resource_data_cleanup);
    if(!tmp_ref) {
        pdebug(DEBUG_WARN,"Unable to add cleanup function to resource!");
        return PLCTAG_ERR_CREATE;
    }

    critical_block(resource_mutex) {
        char *name_copy = str_dup(name);

        if(!name_copy) {
            pdebug(DEBUG_ERROR,"Unable to copy name!");
            rc = PLCTAG_ERR_NO_MEM;
            break;
        }

        rc = hashtable_put(resource_by_name, (void*)name, name_len, resource);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Error inserting resource, %s",plc_tag_decode_error(rc));

            mem_free(name_copy);

            break;
        }

        rc = hashtable_put(name_by_resource, &resource, sizeof(resource), name_copy);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Error inserting name, %s!", plc_tag_decode_error(rc));

            mem_free(name_copy);

            hashtable_remove(resource_by_name, (void*)name, name_len);

            break;
        }
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}



//~ int resource_remove(const char *prefix, const char *name)
//~ {
    //~ int name_len = str_length(prefix) + str_length(name) + 1;
    //~ char *resource_name = mem_alloc(name_len);
    //~ int rc = PLCTAG_STATUS_OK;
    //~ void * resource = NULL;

    //~ pdebug(DEBUG_DETAIL,"Starting with prefix %s and name %s", prefix, name);

    //~ if(!resource_name) {
        //~ pdebug(DEBUG_ERROR,"Unable to allocate new resource name string!");
        //~ return PLCTAG_ERR_NO_MEM;
    //~ }

    //~ snprintf(resource_name, name_len, "%s%s", prefix, name);

    //~ critical_block(resource_mutex) {
        //~ resource = hashtable_remove(resources, resource_name, name_len);
    //~ }

    //~ rc_release(resource);

    //~ mem_free(resource_name);

    //~ pdebug(DEBUG_DETAIL,"Done.");

    //~ return rc;
//~ }




void resource_data_cleanup(void *arg)
{
    if(!arg) {
        pdebug(DEBUG_WARN, "Null argument!");
        return;
    }

    critical_block(resource_mutex) {
        const char *name = hashtable_get(name_by_resource, &arg, sizeof(arg));

        if(!name) {
            pdebug(DEBUG_WARN,"Bad state, no name entry found for resource!");
            break;
        }

        hashtable_remove(name_by_resource, &arg, sizeof(arg));
        hashtable_remove(resource_by_name, (void*)name, str_length(name)+1);

        mem_free(name);
    }
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
    resource_by_name = hashtable_create(200); /* MAGIC */
    if(!resource_by_name) {
        pdebug(DEBUG_ERROR,"Unable to allocate a hashtable!");
        mutex_destroy((mutex_p*)&resource_mutex);

        return PLCTAG_ERR_CREATE;
    }

    /* create the hashtable in which we will have the resource names stored. */
    name_by_resource = hashtable_create(200); /* MAGIC */
    if(!name_by_resource) {
        pdebug(DEBUG_ERROR,"Unable to allocate a hashtable!");

        hashtable_destroy(resource_by_name);
        mutex_destroy((mutex_p*)&resource_mutex);

        return PLCTAG_ERR_CREATE;
    }

    pdebug(DEBUG_INFO,"Finished initializing Resource utility.");

    return rc;
}


void resource_service_teardown(void)
{
    pdebug(DEBUG_INFO,"Tearing down Resource utility.");

    pdebug(DEBUG_INFO,"Tearing down resource hashtable.");

    hashtable_destroy(resource_by_name);
    hashtable_destroy(name_by_resource);

    pdebug(DEBUG_INFO,"Tearing down resource mutex.");

    mutex_destroy((mutex_p*)&resource_mutex);

    pdebug(DEBUG_INFO,"Done.");
}

