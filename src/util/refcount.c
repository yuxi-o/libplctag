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
 * This is a rc struct that we use to make sure that we are able to align
 * the remaining part of the allocated block.
 */

struct rc {
    int strong_count;
    int weak_count;
    lock_t lock;
    void (*cleanup_func)(void *data);

	/* FIXME - this is to force alignment. */
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
 * The clean up function must free the passed data using rc_free.   This will be a pointer into
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

    rc->strong_count = 1;
    rc->weak_count = 0;
    rc->lock = LOCK_INIT;
    rc->cleanup_func = cleanup_func;

    /* return the address _past_ the rc struct. */
    return (rc+1);
}


/*
 * return the original pointer or NULL.   Increments the strong count.
 * This is for usage like:
 * my_struct->some_field = rc_inc(rc_obj);
 */

void *rc_inc(const void *data)
{
    struct rc *rc = NULL;
    int strong_count = 0;

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

    if(rc->strong_count > 0) {
		rc->strong_count++;
		strong_count = rc->strong_count;		
	} else {
		// the reference was bad!
		strong_count = 0;
	}

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    pdebug(DEBUG_DETAIL,"Ref strong_count is now %d",strong_count);

	/* return NULL if the strong count is zero. */
    return (strong_count > 0 ? (void *)data : (void *)NULL);
}


/*
 * return NULL.  Decrement the strong count.   This succeeds in returning NULL even if the weak count is set.
 * 
 * This is for usage like:
 * my_struct->some_field = rc_dec(rc_obj);
 *
 * Note that the clean up function _MUST_ free the data pointer
 * passed to it.   It must clean up anything referenced by that data,
 * and the block itself using rc_free();
 */

void *rc_dec(const void *data)
{
    struct rc *rc = NULL;
    int strong_count;
    int weak_count;

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

    rc->strong_count--;

    if(rc->strong_count < 0) {
        rc->strong_count = 0;
    }

	strong_count = rc->strong_count;
	weak_count = rc->weak_count;

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    pdebug(DEBUG_DETAIL,"Ref strong count is now %d", strong_count);
    pdebug(DEBUG_DETAIL,"Ref weak count is now %d", weak_count);

	/* clean up only if both strong and weak count are zero. */
    if(strong_count <= 0 && weak_count <= 0) {
        pdebug(DEBUG_DETAIL,"Calling cleanup function.");

        rc->cleanup_func((void *)data);

        /* finally done. */
        mem_free(rc);
    }

    return NULL;
}







/*
 * return the original pointer or NULL.   Increments the weak count.
 * 
 * This is for usage like:
 * my_struct->some_field = rc_weak_inc(rc_obj);
 */

void *rc_weak_inc(const void *data)
{
    struct rc *rc = NULL;
    int strong_count = 0;
    int weak_count = 0;

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

    if(rc->strong_count > 0) {
		rc->weak_count++;
		weak_count = rc->weak_count;		
	} else {
		// the reference was bad!
		pdebug(DEBUG_ERROR,"Incrementing ref count with zero strong ref count!");
		weak_count = 0;
	}

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    pdebug(DEBUG_DETAIL,"Ref strong_count is now %d",strong_count);
    pdebug(DEBUG_DETAIL,"Ref weak_count is now %d",weak_count);

	/* return NULL if the strong count is zero. */
    return (strong_count > 0 ? (void *)data : (void *)NULL);
}



/*
 * return NULL.  Decrement the weak count.
 * 
 * This is for usage like:
 * my_struct->some_field = rc_weak_dec(rc_obj);
 *
 * Note that the clean up function _MUST_ free the data pointer
 * passed to it.   It must clean up anything referenced by that data,
 * and the block itself using rc_free();
 */

void *rc_weak_dec(const void *data)
{
    struct rc *rc = NULL;
    int strong_count;
    int weak_count;

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

    rc->weak_count--;

    if(rc->weak_count < 0) {
        rc->weak_count = 0;
    }

	strong_count = rc->strong_count;
	weak_count = rc->weak_count;

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    pdebug(DEBUG_DETAIL,"Ref strong count is now %d", strong_count);
    pdebug(DEBUG_DETAIL,"Ref weak count is now %d", weak_count);

	/* clean up only if all references are gone. */
    if(strong_count <= 0 && weak_count <= 0) {
        pdebug(DEBUG_DETAIL,"Calling cleanup function.");

        rc->cleanup_func((void *)data);

        /* finally done. */
        mem_free(rc);
    }

    return NULL;
}



