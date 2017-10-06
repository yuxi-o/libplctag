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


#ifndef container_of
#define container_of(ptr, type, member) ((type *)((char *)(1 ? (ptr) : &((type *)0)->member) - offsetof(type, member)))
#endif



/*
 * This is a rc struct that we use to make sure that we are able to align
 * the remaining part of the allocated block.
 */


struct refcount_t {
    lock_t lock;
    int strong;
    int weak;
    const char *function_name;
    int line_num;
    void *data;
    void (*cleanup_func)(void *);
};


typedef struct refcount_t *refcount_p;



/*
 * Must be called with lock!
 */
static inline refcount_p get_refcount(rc_ptr ref)
{
    refcount_p result = NULL;

    switch(rc_get_ref_type(ref)) {
        case REF_STRONG:
            /* strong ref. */
            result = container_of(ref, struct refcount_t, strong);
            break;
        case REF_WEAK:
            /* weak ref. */
            result = container_of(ref, struct refcount_t, weak);
            break;
        default:
            /* REF_NULL or unknown. */
            result = NULL;
            break;
    }

    return result;
}





rc_ref_type rc_get_ref_type(rc_ptr ref)
{
    if(!ref) {
        return REF_NULL;
    } else if(*(ref) & 0x01) {
        return REF_WEAK;
    } else {
        return REF_STRONG;
    }
}



/*
 * rc_make_ref
 *
 * Create a reference counted control for the passed data.  Return a strong
 * reference to the data.
 */

rc_ptr rc_make_ref_impl(const char *func, int line_num, void *data, void (*cleanup_func)(void *))
{
    refcount_p rc = NULL;

    pdebug(DEBUG_INFO,"Starting, called from %s:%d",func, line_num);

    pdebug(DEBUG_DETAIL,"Allocating %d-byte refcount struct",(int)sizeof(struct refcount_t));

    rc = mem_alloc(sizeof(struct refcount_t));
    if(!rc) {
        pdebug(DEBUG_WARN,"Unable to allocate refcount struct!");
        return NULL;
    }

    rc->strong = 2;
    rc->weak = 1; /* weak count always has lower bit set. */
    rc->lock = LOCK_INIT;
    rc->cleanup_func = cleanup_func;
    rc->data = data;

    /* store where we were called from for later. */
    rc->function_name = func;
    rc->line_num = line_num;

    pdebug(DEBUG_INFO, "Done");

    /* return the address of the strong count. */
    return &(rc->strong);
}



/*
 * Increments the strong count if the reference is valid.
 *
 * It returns a strong ref if the passed ref was valid.  It returns
 * NULL if the passed ref was invalid.
 *
 * This is for usage like:
 * my_struct->some_field_ref = rc_strong(ref);
 */

rc_ptr rc_strong_impl(const char *func, int line_num, rc_ptr ref)
{
    refcount_p rc = NULL;
    int strong = 0;
    int weak = 0;
    rc_ptr result = NULL;

    pdebug(DEBUG_INFO,"Starting, called from %s:%d",func, line_num);

    if(!ref) {
        pdebug(DEBUG_WARN,"Invalid counter pointer passed!");
        return result;
    }

    rc = get_refcount(ref);

    if(!rc) {
        pdebug(DEBUG_WARN,"Cannot get ref count structure pointer from ref!");
        return result;
    }

    /* loop until we get the lock */
    while (!lock_acquire(&rc->lock)) {
        ; /* do nothing, just spin */
    }

        if(rc->strong > 0) {
            rc->strong += 2;
            result = &(rc->strong);
        } else {
            // the reference was bad!
            pdebug(DEBUG_DETAIL,"Attempt to get strong reference on already invalid reference.");
            rc->strong = 0;
        }

        strong = rc->strong;
        weak = rc->weak;

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    if(!result) {
        pdebug(DEBUG_DETAIL,"Invalid ref!  Unable to take strong reference.");
    } else {
        pdebug(DEBUG_DETAIL,"Ref strong count is %d and weak count is %d",(strong >> 1), (weak >> 1));
    }

    /* return the result struct for copying. */
    return result;
}



/*
 * Increments the weak count if the reference is valid.
 *
 * It returns a weak ref if the passed ref was valid.  It returns
 * an invalid ref if the passed ref was invalid.
 *
 * This is for usage like:
 * my_struct->some_field_ref = rc_weak(ref);
 */

rc_ptr rc_weak_impl(const char *func, int line_num, rc_ptr ref)
{
    refcount_p rc = NULL;
    int strong = 0;
    int weak = 0;
    rc_ptr result = NULL;

    pdebug(DEBUG_INFO,"Starting, called from %s:%d",func, line_num);

    if(!ref) {
        pdebug(DEBUG_WARN,"Invalid counter pointer passed!");
        return result;
    }

    rc = get_refcount(ref);

    if(!rc) {
        pdebug(DEBUG_WARN,"Cannot get ref count structure pointer from ref!");
        return result;
    }

    /* loop until we get the lock */
    while (!lock_acquire(&rc->lock)) {
        ; /* do nothing, just spin */
    }

        if(rc->strong > 0) {
            rc->weak += 2;
            result = &(rc->weak);
        } else {
            /* the reference was bad! */
            pdebug(DEBUG_DETAIL,"Attempt to get weak reference on already invalid reference.");
            rc->strong = 0;
        }

        strong = rc->strong;
        weak = rc->weak;

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    if(!result) {
        pdebug(DEBUG_DETAIL,"Invalid ref!  Unable to take strong reference.");
    } else {
        pdebug(DEBUG_DETAIL,"Ref strong count is %d and weak count is %d",(strong >> 1), (weak >> 1));
    }

    /* return the result struct for copying. */
    return result;
}



/*
 * Decrement the ref count.   Which count is decremented depends on the ref type of the
 * passed pointer.   Either way, a NULL is returned.
 *
 * This is for usage like:
 * my_struct->some_field = rc_release(rc_obj);
 *
 * Note that the clean up function _MUST_ free the data pointer
 * passed to it.   It must clean up anything referenced by that data,
 * and the block itself using mem_free() or the appropriate function;
 */

rc_ptr rc_release_impl(const char *func, int line_num, rc_ptr ref)
{
    refcount_p rc = NULL;
    int strong = 0;
    int weak = 0;
    int invalid = 0;

    pdebug(DEBUG_INFO,"Starting, called from %s:%d",func, line_num);

    if(!ref) {
        pdebug(DEBUG_WARN,"Null reference passed!");
        return NULL;
    }

    rc = get_refcount(ref);

    if(!rc) {
        pdebug(DEBUG_WARN,"Cannot get ref count structure pointer from ref!");
        return NULL;
    }

    /* loop until we get the lock */
    while (!lock_acquire(&rc->lock)) {
        ; /* do nothing, just spin */
    }

        switch(rc_get_ref_type(ref)) {
            case REF_STRONG:
                if(rc->strong > 0) {
                    rc->strong -= 2;
                } else {
                    /* oops, already zeroed out!  This is a bug in the calling logic! */
                    pdebug(DEBUG_WARN,"Strong reference count already zero!");
                    rc->strong = 0;
                }
                break;

            case REF_WEAK:
                if(rc->weak > 1) {
                    rc->weak -= 2;
                } else {
                    /* oops, already zeroed out!  This is a bug in the calling logic! */
                    pdebug(DEBUG_WARN,"Weak reference count already zero!");
                    rc->weak = 1;
                }
                break;

            default:
                pdebug(DEBUG_ERROR,"Malformed reference!  Not NULL, but not valid!");
                invalid = 1;
                break;
        }

        strong = rc->strong;
        weak = rc->weak;

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    pdebug(DEBUG_DETAIL,"Ref strong count is %d and weak count is %d",(strong >> 1), (weak >> 1));

    /* clean up only if both strong and weak count are zero. */
    if(!invalid && strong <= 0 && weak <= 1) {
        pdebug(DEBUG_DETAIL,"Calling cleanup function.");

        rc->cleanup_func(rc->data);

        /* finally done. */
        mem_free(rc);
    }

    return NULL;
}



void *rc_deref_impl(const char *func, int line_num, rc_ptr ref)
{
    refcount_p rc = NULL;
    int strong = 0;
    int weak = 0;
    void *result = NULL;

    pdebug(DEBUG_SPEW,"Starting, called from %s:%d",func, line_num);

    if(!ref) {
        pdebug(DEBUG_WARN,"Invalid counter pointer passed!");
        return result;
    }

    rc = get_refcount(ref);

    if(!rc) {
        pdebug(DEBUG_WARN,"Cannot get ref count structure pointer from ref!");
        return result;
    }

    /* loop until we get the lock */
    while (!lock_acquire(&rc->lock)) {
        ; /* do nothing, just spin */
    }

        strong = rc->strong;
        weak = rc->weak;

        if(strong > 0) {
            result = rc->data;
        }

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    pdebug(DEBUG_SPEW,"Ref strong count is %d and weak count is %d",(strong >> 1), (weak >> 1));

    if(result) {
        pdebug(DEBUG_SPEW,"Valid reference, returning data pointer.");
    } else {
        pdebug(DEBUG_INFO,"Invalid reference, returning NULL pointer.");
    }

    return result;
}

