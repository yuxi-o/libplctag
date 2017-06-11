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



/*
 * New API
 */



/*
 * This is a rc struct that we use to make sure that we are able to align
 * the remaining part of the allocated block.
 */

struct rc {
    int count;
    lock_t lock;
    void (*cleanup_func)(void *data);

    union {
        uint8_t dummy_u8;
        uint16_t dummy_u16;
        uint32_t dummy_u32;
        uint64_t dummy_u64;
        double dummy_double;
        void *dummy_ptr;
        void (*dummy_func)(void);
    } dummy_align[];
};


/*
 * rc_alloc
 *
 * Allocate memory, replacing mem_alloc, and prepend reference counting data to the
 * memory block.
 *
 * The clean up function must NOT free the passed data.   This will be a pointer into
 * the middle of a malloc'ed block of memory.
 */


void *rc_alloc(int size, void (*cleanup_func)(void *data))
{
    int total_size = size + sizeof(struct rc);
    struct rc *rc = mem_alloc(total_size);

    pdebug(DEBUG_DETAIL,"Allocating %d byte from a request of %d with result pointer %p",total_size, size, rc);

    if(!rc) {
        pdebug(DEBUG_WARN,"Unable to allocate sufficient memory!");
        return rc;
    }

    rc->count = 1;
    rc->lock = LOCK_INIT;
    rc->cleanup_func = cleanup_func;

    /* return the address _past_ the rc struct. */
    return (rc+1);
}


/*
 * return the original pointer or NULL.
 * This is for usage like:
 * my_struct->some_field = rc_inc(rc_obj);
 */

void *rc_inc(const void *data)
{
    struct rc *rc = NULL;
    int count = 0;

    if(!data) {
        pdebug(DEBUG_WARN,"Null pointer passed!");
        return (void *)data;
    }

    /*
     * The struct rc we want is before the memory pointer we get.
     * Thus we cast and subtract.
     */
    rc = ((struct rc *)data) - 1;

    /* loop until we get the lock */
    while (!lock_acquire(&rc->lock)) {
        ; /* do nothing, just spin */
    }

    rc->count++;
    count = rc->count;

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    pdebug(DEBUG_DETAIL,"Ref count is now %d",count);

    return (void *)data;
}

/*
 * return NULL.
 * This is for usage like:
 * my_struct->some_field = rc_dec(rc_obj);
 *
 * Note that the clean up function must _NOT_ free the data pointer
 * passed to it.   It must clean up anything referenced by that data,
 * but not the block itself.  That will happen here after the clean up
 * function is called.
 */

void *rc_dec(const void *data)
{
    struct rc *rc = NULL;
    int count;

    if(!data) {
        pdebug(DEBUG_WARN,"Null pointer passed!");
        return NULL;
    }

    /*
     * The struct rc we want is before the memory pointer we get.
     * Thus we cast and subtract.
     *
     * This gets rid of the "const" part!
     */
    rc = ((struct rc *)data) - 1;

    /* loop until we get the lock */
    while (!lock_acquire(&rc->lock)) {
        ; /* do nothing, just spin */
    }

    rc->count--;

    if(rc->count < 0) {
        rc->count = 0;
    }

    count = rc->count;

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    pdebug(DEBUG_DETAIL,"Refcount is now %d", count);

    if(count <= 0) {
        pdebug(DEBUG_DETAIL,"Calling destructor function.");

        rc->cleanup_func((void *)data);

        mem_free(rc);
    }

    return NULL;
}






refcount refcount_init(int count, void *data, void (*delete_func)(void *data))
{
    refcount rc;

    pdebug(DEBUG_INFO, "Initializing refcount struct with count=%d", count);

    rc.count = count;
    rc.lock = LOCK_INIT;
    rc.data = data;
    rc.delete_func = delete_func;

    return rc;
}

/* must be called with a mutex held! */
int refcount_acquire(refcount *rc)
{
    int count;

    if(!rc) {
        return PLCTAG_ERR_NULL_PTR;
    }

    /* loop until we get the lock */
    while (!lock_acquire(&rc->lock)) {
        ; /* do nothing, just spin */
    }

    rc->count++;
    count = rc->count;

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    pdebug(DEBUG_INFO,"Ref count is now %d",count);

    return count;
}


int refcount_release(refcount *rc)
{
    int count;

    if(!rc) {
        return PLCTAG_ERR_NULL_PTR;
    }

    /* loop until we get the lock */
    while (!lock_acquire(&rc->lock)) {
        ; /* do nothing, just spin */
    }

    rc->count--;

    if(rc->count < 0) {
        rc->count = 0;
    }

    count = rc->count;

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    pdebug(DEBUG_INFO,"Refcount is now %d", count);

    if(count <= 0) {
        pdebug(DEBUG_INFO,"Calling clean up function.");

        rc->delete_func(rc->data);
    }

    return count;
}

int refcount_get_count(refcount *rc)
{
    int count;

    if(!rc) {
        return PLCTAG_ERR_NULL_PTR;
    }

    /* loop until we get the lock */
    while (!lock_acquire(&rc->lock)) {
        ; /* do nothing, just spin */
    }

    count = rc->count;

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    return count;
}

