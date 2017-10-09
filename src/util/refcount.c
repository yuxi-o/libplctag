/***************************************************************************
 *   Copyright (C) 2016 by Kyle Hayes                                      *
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


#include <lib/libplctag.h>
#include <platform.h>
#include <util/refcount.h>
#include <util/debug.h>
#include <util/hashtable.h>


#ifndef container_of
#define container_of(ptr, type, member) ((type *)((char *)(1 ? (ptr) : &((type *)0)->member) - offsetof(type, member)))
#endif



static volatile mutex_p refcount_mutex = NULL;
static volatile hashtable_p references = NULL;





/*
 * Handle clean up functions.
 */

typedef struct cleanup_t *cleanup_p;

struct cleanup_t {
    cleanup_p next;
    const char *function_name;
    int line_num;
    void (*cleanup_func)(void *arg);
};


/*
 * This is a rc struct that we use to make sure that we are able to align
 * the remaining part of the allocated block.
 */

struct refcount_t {
    lock_t lock;
    int count;
    const char *function_name;
    int line_num;
    void *data;
    cleanup_p cleaners;
};


typedef struct refcount_t *refcount_p;

static void refcount_cleanup(refcount_p rc);
static cleanup_p cleanup_entry_create(const char *func, int line_num, void (*cleanup_func)(void *));
static void cleanup_entry_destroy(cleanup_p entry);



/*
 * rc_make_ref
 *
 * Create a reference counted control for the passed data.  Return a strong
 * reference to the data.
 */

void *rc_make_ref_impl(const char *func, int line_num, void *data, void (*cleanup_func)(void *))
{
    void *result = NULL;
    refcount_p rc = NULL;
    cleanup_p cleanup = NULL;

    pdebug(DEBUG_INFO,"Starting, called from %s:%d",func, line_num);

    pdebug(DEBUG_DETAIL,"Allocating %d-byte refcount struct",(int)sizeof(struct refcount_t));

    rc = mem_alloc(sizeof(struct refcount_t));
    if(!rc) {
        pdebug(DEBUG_WARN,"Unable to allocate refcount struct!");
        return NULL;
    }

    rc->count = 0;
    rc->lock = LOCK_INIT;
    rc->data = data;

    /* store where we were called from for later. */
    rc->function_name = func;
    rc->line_num = line_num;

    /* allocate the final cleanup struct */
    cleanup = cleanup_entry_create(func, line_num, cleanup_func);

    if(!cleanup) {
        pdebug(DEBUG_ERROR,"Unable to allocate cleanup entry!");
        mem_free(rc);
        return NULL;
    }

    rc->cleaners = cleanup;

    /* now put the refcount struct into the hashtable. */
    critical_block(refcount_mutex) {
        int retcode = hashtable_put(references, (void *)&data, sizeof(void *), data);

        if(retcode == PLCTAG_STATUS_OK) {
            result = data;
        } else if(retcode == PLCTAG_ERR_DUPLICATE) {
            pdebug(DEBUG_WARN,"Pointer is already being reference counted!");
            result = NULL;
        } else {
            pdebug(DEBUG_WARN,"Unable to enter data pointer into hashtable!  Got error %s", plc_tag_decode_error(retcode));
            result = NULL;
        }
    }

    pdebug(DEBUG_INFO, "Done");

    /* return the original address if successful otherwise NULL. */
    return result;
}



/*
 * Increments the ref count if the reference is valid.
 *
 * It returns the original poiner if the passed pointer was valid.  It returns
 * NULL if the passed pointer was invalid.
 *
 * This is for usage like:
 * my_struct->some_field_ref = rc_inc(ref);
 */

void *rc_inc_impl(const char *func, int line_num, void *data)
{
    int count = 0;
    refcount_p rc = NULL;
    void *  result = NULL;

    pdebug(DEBUG_INFO,"Starting, called from %s:%d",func, line_num);

    if(!data) {
        pdebug(DEBUG_WARN,"Invalid pointer passed!");
        return result;
    }

    /* get the refcount info. */
    critical_block(refcount_mutex) {
        rc = hashtable_get(references, &data, sizeof(void *));

        if(rc) {
            if(rc->count > 0) {
                rc->count++;
                count = rc->count;
                result = data;
            } else {
                pdebug(DEBUG_WARN,"Reference is invalid!");
                result = NULL;
            }
        }
    }

    if(!result) {
        pdebug(DEBUG_DETAIL,"Invalid ref!  Unable to take strong reference.");
    } else {
        pdebug(DEBUG_DETAIL,"Ref count is %d.", count);
    }

    /* return the result pointer. */
    return result;
}



/*
 * create a new cleanup entry.   This will be called when the reference is completely release.
 * Cleanup functions are called in reverse order that they are created.
 */
int rc_register_cleanup_impl(const char *func, int line_num, void *data, void (*cleanup_func)(void *))
{
    cleanup_p entry = NULL;
    int status = PLCTAG_STATUS_OK;
    refcount_p rc = NULL;

    pdebug(DEBUG_INFO,"Starting, called from %s:%d",func, line_num);

    if(!data) {
        pdebug(DEBUG_WARN,"Null reference passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    /* get the refcount info. */
    critical_block(refcount_mutex) {
        rc = hashtable_get(references, &data, sizeof(void *));

        if(rc) {
            if(rc->count > 0) {
                entry = cleanup_entry_create(func, line_num, cleanup_func);
                if(!entry) {
                    pdebug(DEBUG_ERROR,"Unable to allocated cleanup entry!");
                    status = PLCTAG_ERR_NO_MEM;
                    break;
                }

                entry->next = rc->cleaners;
                rc->cleaners = entry;
            } else {
                pdebug(DEBUG_WARN,"Reference is invalid!");
                status = PLCTAG_ERR_BAD_DATA;
            }
        } else {
            pdebug(DEBUG_WARN,"No reference count info found for data pointer.");
            status = PLCTAG_ERR_NOT_FOUND;
        }
    }

    pdebug(DEBUG_INFO,"Done.");

    return status;
}



/*
 * Decrement the ref count.
 *
 * This is for usage like:
 * my_struct->some_field = rc_dec(rc_obj);
 *
 * Note that the final clean up function _MUST_ free the data pointer
 * passed to it.   It must clean up anything referenced by that data,
 * and the block itself using mem_free() or the appropriate function;
 */

void *rc_dec_impl(const char *func, int line_num, void *data)
{
    int count = 0;
    int invalid = 0;
    refcount_p rc = NULL;

    pdebug(DEBUG_INFO,"Starting, called from %s:%d",func, line_num);

    if(!data) {
        pdebug(DEBUG_WARN,"Null reference passed!");
        return NULL;
    }

    /* get the refcount info. */
    critical_block(refcount_mutex) {
        rc = hashtable_get(references, &data, sizeof(data));

        if(rc) {
            if(rc->count > 0) {
                rc->count--;
                count = rc->count;

                if(rc->count <= 0) {
                    /* remove it from the hashtable to make sure no one else uses it. */
                    hashtable_remove(references, &data, sizeof(data));
                }
            } else {
                pdebug(DEBUG_WARN,"Reference is invalid!");
                invalid = 1;
            }
        } else {
            pdebug(DEBUG_WARN,"No reference count info found for data pointer.");
            invalid = 1;
        }
    }

    pdebug(DEBUG_DETAIL,"Ref count is %d.", count);

    /* clean up only if both strong and weak count are zero. */
    if(rc && !invalid && count <= 0) {
        pdebug(DEBUG_DETAIL,"Calling cleanup functions.");

        refcount_cleanup(rc);
    }

    return NULL;
}




void refcount_cleanup(refcount_p rc)
{
    pdebug(DEBUG_INFO,"Starting");
    if(!rc) {
        pdebug(DEBUG_WARN,"Refcount is NULL!");
        return;
    }

    while(rc->cleaners) {
        cleanup_p entry = rc->cleaners;
        rc->cleaners = entry->next;

        /* do the clean up of the object. */
        pdebug(DEBUG_DETAIL,"Calling clean function added in %s at line %d", entry->function_name, entry->line_num);
        entry->cleanup_func(rc->data);

        cleanup_entry_destroy(entry);
    }

    /* finally done. */
    mem_free(rc);

    pdebug(DEBUG_INFO,"Done.");
}



cleanup_p cleanup_entry_create(const char *func, int line_num, void (*cleanup_func)(void*))
{
    cleanup_p entry = NULL;

    entry = mem_alloc(sizeof(struct cleanup_t));
    if(!entry) {
        pdebug(DEBUG_ERROR,"Unable to allocate new cleanup struct!");
        return NULL;
    }

    entry->cleanup_func = cleanup_func;
    entry->function_name = func;
    entry->line_num = line_num;

    return entry;
}


void cleanup_entry_destroy(cleanup_p entry)
{
    if(!entry) {
        pdebug(DEBUG_WARN,"Entry pointer is NULL!");
        return;
    }

    mem_free(entry);
}




int refcount_service_init()
{
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO,"Starting.");

    rc = mutex_create((mutex_p *)&refcount_mutex);
    if(!rc == PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR,"Unable to create refcount mutex!");
        return rc;
    }

    references = hashtable_create(512);  /* MAGIC */
    if(!references) {
        pdebug(DEBUG_ERROR,"Unable to create refcount hashtable!");
        return PLCTAG_ERR_CREATE;
    }

    pdebug(DEBUG_INFO,"Done.");

    return PLCTAG_STATUS_OK;
}




int refcount_cleanup_entry_destroy(hashtable_p table, void *key, int key_len, void **data)
{
    if(!table) {
        pdebug(DEBUG_WARN,"Null table pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(!key || key_len <= 0) {
        pdebug(DEBUG_WARN,"Key is null or zero length!");
        return PLCTAG_ERR_BAD_DATA;
    }

    if(*data) {
        /* this is a refcount struct. */
        refcount_p rc = *data;

        pdebug(DEBUG_WARN,"Entry still exists for data %p created in function %s at line %d",rc->data, rc->function_name, rc->line_num);

        refcount_cleanup(rc);
    }

    return PLCTAG_STATUS_OK;
}



void refcount_service_teardown()
{
    pdebug(DEBUG_INFO, "Starting");

    /* get rid of the refcount structs. */
    hashtable_on_each(references, refcount_cleanup_entry_destroy);

    hashtable_destroy(references);

    pdebug(DEBUG_INFO,"Done.");
}
