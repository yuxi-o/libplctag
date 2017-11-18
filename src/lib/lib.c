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


#define LIBPLCTAGDLL_EXPORTS 1

#include <stdlib.h>
#include <limits.h>
#include <float.h>
#include <lib/libplctag.h>
#include <lib/lib.h>
#include <platform.h>
#include <util/attr.h>
#include <util/bytebuf.h>
#include <util/debug.h>
#include <util/hashtable.h>
#include <util/rc_thread.h>
#include <util/resource.h>
#include <system/system.h>
#include <ab/logix.h>



/*
 * The version string.
 */

const char *VERSION="2.0.5";
const int VERSION_ARRAY[3] = {2,0,5};




struct tag_t {
    tag_id id;

    mutex_p api_mut;
    mutex_p external_mut;

    attr attribs;
    int read_in_flight;
    int write_in_flight;
    int create_in_flight;

    int64_t read_cache_expire_ms;
    int64_t read_cache_duration_ms;

    /* implementation specific data and entry function */
    void *impl_data; /* MUST be ref counted! */
    impl_operation_func op_func;

    /* a place to store the data */
    bytebuf_p data;
};





/***********************************************************************
 ************************* Global Variables ****************************
 **********************************************************************/

static lock_t library_initialization_lock = LOCK_INIT;
static volatile int library_initialized = 0;
static mutex_p global_library_mutex = NULL;
static volatile hashtable_p tags = NULL;
static volatile int global_tag_id = 1;

#define MAX_TAG_ID (1000000000)


static void tag_destroy(void *tar_arg, int arg_count, void **args);
static int insert_tag(tag_p tag);
static int wait_for_timeout(tag_p tag, int timeout);
static tag_p get_tag(tag_id id);
static int initialize_modules(void);
static void teardown_modules(void);





/**************************************************************************
 ***************************  API Functions  ******************************
 **************************************************************************/




/*
 * plc_tag_decode_error()
 *
 * This takes an integer error value and turns it into a printable string.
 *
 * FIXME - this should produce better errors than this!
 */

LIB_EXPORT const char* plc_tag_decode_error(int rc)
{
    switch(rc) {
    case PLCTAG_STATUS_PENDING:
        return "PLCTAG_STATUS_PENDING";
        break;
    case PLCTAG_STATUS_OK:
        return "PLCTAG_STATUS_OK";
        break;
    case PLCTAG_ERR_NULL_PTR:
        return "PLCTAG_ERR_NULL_PTR";
        break;
    case PLCTAG_ERR_OUT_OF_BOUNDS:
        return "PLCTAG_ERR_OUT_OF_BOUNDS";
        break;
    case PLCTAG_ERR_NO_MEM:
        return "PLCTAG_ERR_NO_MEM";
        break;
    case PLCTAG_ERR_LL_ADD:
        return "PLCTAG_ERR_LL_ADD";
        break;
    case PLCTAG_ERR_BAD_PARAM:
        return "PLCTAG_ERR_BAD_PARAM";
        break;
    case PLCTAG_ERR_CREATE:
        return "PLCTAG_ERR_CREATE";
        break;
    case PLCTAG_ERR_NOT_EMPTY:
        return "PLCTAG_ERR_NOT_EMPTY";
        break;
    case PLCTAG_ERR_OPEN:
        return "PLCTAG_ERR_OPEN";
        break;
    case PLCTAG_ERR_SET:
        return "PLCTAG_ERR_SET";
        break;
    case PLCTAG_ERR_WRITE:
        return "PLCTAG_ERR_WRITE";
        break;
    case PLCTAG_ERR_TIMEOUT:
        return "PLCTAG_ERR_TIMEOUT";
        break;
    case PLCTAG_ERR_TIMEOUT_ACK:
        return "PLCTAG_ERR_TIMEOUT_ACK";
        break;
    case PLCTAG_ERR_RETRIES:
        return "PLCTAG_ERR_RETRIES";
        break;
    case PLCTAG_ERR_READ:
        return "PLCTAG_ERR_READ";
        break;
    case PLCTAG_ERR_BAD_DATA:
        return "PLCTAG_ERR_BAD_DATA";
        break;
    case PLCTAG_ERR_ENCODE:
        return "PLCTAG_ERR_ENCODE";
        break;
    case PLCTAG_ERR_DECODE:
        return "PLCTAG_ERR_DECODE";
        break;
    case PLCTAG_ERR_UNSUPPORTED:
        return "PLCTAG_ERR_UNSUPPORTED";
        break;
    case PLCTAG_ERR_TOO_LONG:
        return "PLCTAG_ERR_TOO_LONG";
        break;
    case PLCTAG_ERR_CLOSE:
        return "PLCTAG_ERR_CLOSE";
        break;
    case PLCTAG_ERR_NOT_ALLOWED:
        return "PLCTAG_ERR_NOT_ALLOWED";
        break;
    case PLCTAG_ERR_THREAD:
        return "PLCTAG_ERR_THREAD";
        break;
    case PLCTAG_ERR_NO_DATA:
        return "PLCTAG_ERR_NO_DATA";
        break;
    case PLCTAG_ERR_THREAD_JOIN:
        return "PLCTAG_ERR_THREAD_JOIN";
        break;
    case PLCTAG_ERR_THREAD_CREATE:
        return "PLCTAG_ERR_THREAD_CREATE";
        break;
    case PLCTAG_ERR_MUTEX_DESTROY:
        return "PLCTAG_ERR_MUTEX_DESTROY";
        break;
    case PLCTAG_ERR_MUTEX_UNLOCK:
        return "PLCTAG_ERR_MUTEX_UNLOCK";
        break;
    case PLCTAG_ERR_MUTEX_INIT:
        return "PLCTAG_ERR_MUTEX_INIT";
        break;
    case PLCTAG_ERR_MUTEX_LOCK:
        return "PLCTAG_ERR_MUTEX_LOCK";
        break;
    case PLCTAG_ERR_NOT_IMPLEMENTED:
        return "PLCTAG_ERR_NOT_IMPLEMENTED";
        break;
    case PLCTAG_ERR_BAD_DEVICE:
        return "PLCTAG_ERR_BAD_DEVICE";
        break;
    case PLCTAG_ERR_BAD_GATEWAY:
        return "PLCTAG_ERR_BAD_GATEWAY";
        break;
    case PLCTAG_ERR_REMOTE_ERR:
        return "PLCTAG_ERR_REMOTE_ERR";
        break;
    case PLCTAG_ERR_NOT_FOUND:
        return "PLCTAG_ERR_NOT_FOUND";
        break;
    case PLCTAG_ERR_ABORT:
        return "PLCTAG_ERR_ABORT";
        break;
    case PLCTAG_ERR_WINSOCK:
        return "PLCTAG_ERR_WINSOCK";
        break;
    case PLCTAG_ERR_DUPLICATE:
        return "PLCTAG_ERR_DUPLICATE";
        break;

    case PLCTAG_ERR_BUSY:
        return "PLCTAG_ERR_BUSY";
        break;

    default:
        return "Unknown error.";
        break;
    }

    return "Unknown error.";
}



/*
 * plc_tag_create()
 *
 * This is where the dispatch occurs to the protocol specific implementation.
 */

LIB_EXPORT tag_id plc_tag_create(const char *attrib_str, int timeout)
{
    tag_p tag = NULL;
    attr attribs = NULL;
    int rc = PLCTAG_STATUS_OK;
    int read_cache_ms = 0;
    const char *plc_type = NULL;

    pdebug(DEBUG_INFO,"Starting");

    if(initialize_modules() != PLCTAG_STATUS_OK) {
        return PLC_TAG_NULL;
    }

    if(!attrib_str || str_length(attrib_str) == 0) {
        return PLC_TAG_NULL;
    }

    attribs = attr_create_from_str(attrib_str);

    if(!attribs) {
        return PLC_TAG_NULL;
    }

    /* set debug level */
    set_debug_level(attr_get_int(attribs, "debug", DEBUG_NONE));

    /* create the generic part of the tag. */
    tag = rc_alloc(sizeof(*tag), tag_destroy);
    if(!tag) {
        attr_destroy(attribs);
        pdebug(DEBUG_ERROR,"Unable to create tag!");
        return PLCTAG_ERR_NO_MEM;
    }

    tag->create_in_flight = 1;
    tag->attribs = attribs;

    /* set up the mutexes */
    rc = mutex_create(&tag->api_mut);
    if(rc != PLCTAG_STATUS_OK) {
        rc_dec(tag);
        pdebug(DEBUG_ERROR,"Unable to create API mutex!");
        return rc;
    }

    rc = mutex_create(&tag->external_mut);
    if(rc != PLCTAG_STATUS_OK) {
        rc_dec(tag);
        pdebug(DEBUG_ERROR,"Unable to create external mutex!");
        return rc;
    }

    /* set up the read cache config. */
    read_cache_ms = attr_get_int(attribs,"read_cache_ms",0);

    if(read_cache_ms < 0) {
        pdebug(DEBUG_WARN, "read_cache_ms value must be positive, using zero.");
        read_cache_ms = 0;
    }

    tag->read_cache_expire_ms = (uint64_t)0;
    tag->read_cache_duration_ms = (uint64_t)read_cache_ms;

    /*
     * Get the implementation specific to the tag/PLC type.
     * This uses the protocol and the CPU/PLC elements of the attributes.
     * Everything else is specific to the protocol.
     */

    plc_type = attr_get_str(attribs, "plc", "NONE");
    if(str_cmp_i(plc_type,"logix") == 0) {
        /* Allen-Bradley ControlLogix or CompactLogix PLC */
        rc = logix_tag_create(tag);
    } else if(str_cmp_i(plc_type, "system") == 0) {
        rc = system_tag_create(tag);
    } else {
        pdebug(DEBUG_WARN,"Unsupported PLC type %s!", plc_type);
        rc_dec(tag);
        return PLCTAG_ERR_BAD_PARAM;
    }

    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR,"Unable to create or find PLC %s!",plc_type);
        rc_dec(tag);
        return PLCTAG_ERR_CREATE;
    }

    /* insert the tag into the tag hashtable. */
    insert_tag(tag);

    /*
     * if there is a timeout, then loop until we get
     * an error or we timeout.
     */
    if(timeout>0) {
        rc = wait_for_timeout(tag, timeout);
    } else {
        /* not waiting and no errors yet, so carry on. */
        rc = PLCTAG_STATUS_OK;
    }

    if(rc == PLCTAG_STATUS_OK || rc == PLCTAG_STATUS_PENDING) {
        return tag->id;
    } else {
        /* tag is bad */
        return rc;
    }
}



/*
 * plc_tag_lock
 *
 * Lock the tag against use by other threads.  Because operations on a tag are
 * very much asynchronous, actions like getting and extracting the data from
 * a tag take more than one API call.  If more than one thread is using the same tag,
 * then the internal state of the tag will get broken and you will probably experience
 * a crash.
 *
 * This should be used to initially lock a tag when starting operations with it
 * followed by a call to tag_unlock when you have everything you need from the tag.
 */

LIB_EXPORT int plc_tag_lock(tag_id id)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_INFO, "Starting.");

        tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        rc = mutex_lock(tag->external_mut);
    }

    rc_dec(tag);

    pdebug(DEBUG_INFO, "Done.");

    return rc;
}






/*
 * plc_tag_unlock
 *
 * The opposite action of tag_unlock.  This allows other threads to access the
 * tag.
 */

LIB_EXPORT int plc_tag_unlock(tag_id id)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_INFO, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        rc = mutex_unlock(tag->external_mut);
    }

    rc_dec(tag);

    pdebug(DEBUG_INFO, "Done.");

    return rc;
}




/*
 * plc_tag_abort()
 *
 * This function calls through the vtable in the passed tag to call
 * the protocol-specific implementation.
 *
 * The implementation must do whatever is necessary to abort any
 * ongoing IO.
 *
 * The status of the operation is returned.
 */

LIB_EXPORT int plc_tag_abort(tag_id id)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_INFO, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        rc = tag->op_func(tag, tag->impl_data, TAG_OP_ABORT);
    }

    rc_dec(tag);

    pdebug(DEBUG_INFO, "Done.");

    return rc;
}







/*
 * plc_tag_destroy()
 *
 * Remove all implementation specific details about a tag and clear its
 * memory.
 *
 * Note that the tag may not be immediately destroyed.
 */

LIB_EXPORT int plc_tag_destroy(tag_id id)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_INFO, "Starting.");

    /*
     * This is done in two steps to prevent the mutex from being locked
     * while the tag gets destroyed.   The first line is used to get a reference
     * to the tag.  This increments the ref count.
     *
     * The first line removes the tag from the hashtable.   At that point,
     * only existing threads that are using the tag have references.  No
     * new references will be created from the hash table.
     *
     * Then the tag reference count is decremented.  Normally, this will cause
     * the tag destructor to trigger.  However, depending on how implementations
     * are done, it is possible that there will still be in flight references that
     * need to be cleaned up.
     */
    critical_block(global_library_mutex) {
        tag = hashtable_remove(tags, &id, sizeof(id));
    }

    /* abort any operations in flight. We need to call the implementation directly. */
    if(tag) {
        rc = tag->op_func(tag, tag->impl_data, TAG_OP_ABORT);
    }

    /* now we are safe to decrement the ref count. */
    rc_dec(tag);

    pdebug(DEBUG_INFO, "Done.");

    return rc;
}





/*
 * plc_tag_read()
 *
 * This function calls through the vtable in the passed tag to call
 * the protocol-specific implementation.  That starts the read operation.
 * If there is a timeout passed, then this routine waits for either
 * a timeout or an error.
 *
 * The status of the operation is returned.
 */

LIB_EXPORT int plc_tag_read(tag_id id, int timeout)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_INFO, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        if(tag->write_in_flight || tag->create_in_flight) {
            pdebug(DEBUG_WARN,"Conflicting operation already in progress!");
            rc = PLCTAG_ERR_BUSY;
            break;
        }

        /* check read cache, if not expired, return existing data. */
        if(tag->read_cache_expire_ms > time_ms()) {
            pdebug(DEBUG_INFO, "Returning cached data.");
            rc = PLCTAG_STATUS_OK;
            break;
        }

        /* the protocol implementation does not do the timeout. */
        rc = tag->op_func(tag, tag->impl_data, TAG_OP_READ);

        /* if error, return now */
        if(rc != PLCTAG_STATUS_PENDING && rc != PLCTAG_STATUS_OK) {
            break;
        }

        if(rc == PLCTAG_STATUS_PENDING) {
            tag->read_in_flight = 1;
        }

        /*
         * if there is a timeout, then loop until we get
         * an error or we timeout.
         */
        if(timeout && rc == PLCTAG_STATUS_PENDING) {
            int64_t start_time = time_ms();

            rc = wait_for_timeout(tag, timeout);

            pdebug(DEBUG_INFO,"elapsed time %ldms",(time_ms()-start_time));
        }

        /* set up the cache time */
        if((rc == PLCTAG_STATUS_OK || rc == PLCTAG_STATUS_PENDING) && tag->read_cache_duration_ms) {
            tag->read_cache_expire_ms = time_ms() + tag->read_cache_duration_ms;
        }
    } /* end of api block */

    rc_dec(tag);

    pdebug(DEBUG_INFO, "Done");

    return rc;
}






/*
 * plc_tag_status
 *
 * Return the current status of the tag.  This will be PLCTAG_STATUS_PENDING if there is
 * an uncompleted IO operation.  It will be PLCTAG_STATUS_OK if everything is fine.  Other
 * errors will be returned as appropriate.
 *
 * This is a function provided by the underlying protocol implementation.
 */

LIB_EXPORT int plc_tag_status(tag_id id)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_SPEW, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        rc = tag->op_func(tag, tag->impl_data, TAG_OP_STATUS);

        if(rc == PLCTAG_STATUS_OK) {
            tag->create_in_flight = 0;
            tag->read_in_flight = 0;
            tag->write_in_flight = 0;
        }
    }

    rc_dec(tag);

    pdebug(DEBUG_SPEW, "Done.");

    return rc;
}







/*
 * plc_tag_write()
 *
 * This function calls through the vtable in the passed tag to call
 * the protocol-specific implementation.  That starts the write operation.
 * If there is a timeout passed, then this routine waits for either
 * a timeout or an error.
 *
 * The status of the operation is returned.
 */

LIB_EXPORT int plc_tag_write(tag_id id, int timeout)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_INFO, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        if(tag->read_in_flight || tag->create_in_flight) {
            pdebug(DEBUG_WARN,"Conflicting operation already in progress!");
            rc = PLCTAG_ERR_BUSY;
            break;
        }

        /* the protocol implementation does not do the timeout. */
        rc = tag->op_func(tag, tag->impl_data, TAG_OP_WRITE);

        /* if error, return now */
        if(rc != PLCTAG_STATUS_PENDING && rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Response from write command is not OK!");
            break;
        }

        if(rc == PLCTAG_STATUS_PENDING) {
            tag->write_in_flight = 1;
        }

        /*
         * if there is a timeout, then loop until we get
         * an error or we timeout.
         */
        if(timeout && rc == PLCTAG_STATUS_PENDING) {
            int64_t start_time = time_ms();

            rc = wait_for_timeout(tag, timeout);

            pdebug(DEBUG_INFO,"elapsed time %ldms",(time_ms()-start_time));
        }
    } /* end of api block */

    rc_dec(tag);

    pdebug(DEBUG_INFO, "Done");

    return rc;
}






LIB_EXPORT int plc_tag_get_size(tag_id id)
{
    int result = 0;
    tag_p tag = NULL;
    int status = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        /* some operations can change the tag size */
        status = tag->op_func(tag, tag->impl_data, TAG_OP_STATUS);
        if(status != PLCTAG_STATUS_OK) {
            result = status;
            break;
        }

        if(!tag->data) {
            result = PLCTAG_ERR_NO_DATA;
            break;
        }

        result = bytebuf_get_size(tag->data);
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return result;
}






/*
 * Tag data accessors.
 */


LIB_EXPORT uint8_t plc_tag_get_uint8(tag_id id, int offset)
{
    uint8_t res = (uint8_t)UINT8_MAX;
    tag_p tag = NULL;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 1) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_unmarshal(tag->data, BB_U8, &res);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        }
    }

    if(rc != PLCTAG_STATUS_OK) {
        res = UINT8_MAX;
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return res;
}



LIB_EXPORT int plc_tag_set_uint8(tag_id id, int offset, uint8_t val)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            rc = PLCTAG_ERR_NULL_PTR;
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 1) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_marshal(tag->data, BB_U8, val);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        } else {
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}




LIB_EXPORT int8_t plc_tag_get_int8(tag_id id, int offset)
{
    int8_t res = (int8_t)INT8_MIN;
    tag_p tag = NULL;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 1) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_unmarshal(tag->data, BB_I8, &res);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        }
    }

    if(rc != PLCTAG_STATUS_OK) {
        res = INT8_MIN;
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return res;
}


LIB_EXPORT int plc_tag_set_int8(tag_id id, int offset, int8_t val)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            rc = PLCTAG_ERR_NULL_PTR;
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 1) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_marshal(tag->data, BB_I8, val);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
           }
        } else {
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}






LIB_EXPORT uint16_t plc_tag_get_uint16(tag_id id, int offset)
{
    uint16_t res = (uint16_t)UINT16_MAX;
    tag_p tag = NULL;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 2) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_unmarshal(tag->data, BB_U16, &res);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        }
    }

    if(rc != PLCTAG_STATUS_OK) {
        res = UINT16_MAX;
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return res;
}


LIB_EXPORT int plc_tag_set_uint16(tag_id id, int offset, uint16_t val)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            rc = PLCTAG_ERR_NULL_PTR;
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 2) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_marshal(tag->data, BB_U16, val);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        } else {
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}


LIB_EXPORT int16_t plc_tag_get_int16(tag_id id, int offset)
{
    int16_t res = (int16_t)INT16_MIN;
    tag_p tag = NULL;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 2) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_unmarshal(tag->data, BB_I16, &res);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        }
    }

    if(rc != PLCTAG_STATUS_OK) {
        res = INT16_MIN;
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return res;
}


LIB_EXPORT int plc_tag_set_int16(tag_id id, int offset, int16_t val)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_DETAIL, "Starting.");


    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            rc = PLCTAG_ERR_NULL_PTR;
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 2) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_marshal(tag->data, BB_I16, val);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        } else {
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}





LIB_EXPORT uint32_t plc_tag_get_uint32(tag_id id, int offset)
{
    uint32_t res = (uint32_t)UINT32_MAX;
    tag_p tag = NULL;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 4) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_unmarshal(tag->data, BB_U32, &res);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
           }
        }
    }

    if(rc != PLCTAG_STATUS_OK) {
        res = UINT32_MAX;
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return res;
}


LIB_EXPORT int plc_tag_set_uint32(tag_id id, int offset, uint32_t val)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            rc = PLCTAG_ERR_NULL_PTR;
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 4) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_marshal(tag->data, BB_U32, val);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        } else {
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}


LIB_EXPORT int32_t plc_tag_get_int32(tag_id id, int offset)
{
    int32_t res = (int32_t)INT32_MIN;
    tag_p tag = NULL;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 4) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_unmarshal(tag->data, BB_I32, &res);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        }
    }

    if(rc != PLCTAG_STATUS_OK) {
        res = INT32_MIN;
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return res;
}


LIB_EXPORT int plc_tag_set_int32(tag_id id, int offset, int32_t val)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            rc = PLCTAG_ERR_NULL_PTR;
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 4) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_marshal(tag->data, BB_I32, val);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        } else {
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}




LIB_EXPORT uint64_t plc_tag_get_uint64(tag_id id, int offset)
{
    uint64_t res = (uint64_t)UINT64_MAX;
    tag_p tag = NULL;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 8) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_unmarshal(tag->data, BB_U64, &res);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        }
    }

    if(rc != PLCTAG_STATUS_OK) {
        res = UINT64_MAX;
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return res;
}

LIB_EXPORT int plc_tag_set_uint64(tag_id id, int offset, uint64_t val)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            rc = PLCTAG_ERR_NULL_PTR;
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 8) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_marshal(tag->data, BB_U64, val);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        } else {
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}

LIB_EXPORT int64_t plc_tag_get_int64(tag_id id, int offset)
{
    int64_t res = (int64_t)INT64_MIN;
    tag_p tag = NULL;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 8) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_unmarshal(tag->data, BB_I64, &res);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        }
    }

    if(rc != PLCTAG_STATUS_OK) {
        res = INT64_MIN;
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return res;
}


LIB_EXPORT int plc_tag_set_int64(tag_id id, int offset, int64_t val)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            rc = PLCTAG_ERR_NULL_PTR;
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 8) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_marshal(tag->data, BB_I64, val);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        } else {
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}




LIB_EXPORT float plc_tag_get_float32(tag_id id, int offset)
{
    tag_p tag = NULL;
    float res = FLT_MIN;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return res;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 4) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_unmarshal(tag->data, BB_F32, &res);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        }
    }

    if(rc != PLCTAG_STATUS_OK) {
        res = FLT_MIN;
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return res;
}




LIB_EXPORT int plc_tag_set_float32(tag_id id, int offset, float val)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            rc = PLCTAG_ERR_NULL_PTR;
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 4) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_marshal(tag->data, BB_F32, val);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        } else {
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}









LIB_EXPORT double plc_tag_get_float64(tag_id id, int offset)
{
    tag_p tag = NULL;
    double res = DBL_MIN;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL, "Starting.");

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return res;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 8) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_unmarshal(tag->data, BB_F64, &res);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        }
    }

    if(rc != PLCTAG_STATUS_OK) {
        res = DBL_MIN;
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return res;
}




LIB_EXPORT int plc_tag_set_float64(tag_id id, int offset, double val)
{
    int rc = PLCTAG_STATUS_OK;
    tag_p tag = NULL;

    tag = get_tag(id);

    if(!tag) {
        pdebug(DEBUG_WARN,"Tag %d not found.", id);
        return PLCTAG_ERR_NOT_FOUND;
    }

    critical_block(tag->api_mut) {
        int size;

        if(!tag->data) {
            rc = PLCTAG_ERR_NULL_PTR;
            break;
        }

        size = bytebuf_get_size(tag->data);

        if(offset >= 0 && (offset + 8) <= size) {
            rc = bytebuf_set_cursor(tag->data, offset);

            if(rc == PLCTAG_STATUS_OK) {
                rc = bytebuf_marshal(tag->data, BB_F64, val);

                if(rc > 0) {
                    rc = PLCTAG_STATUS_OK;
                }
            }
        } else {
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
        }
    }

    rc_dec(tag);

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}




/***********************************************************************
 ************************ Internal API Functions ***********************
 **********************************************************************/

/*
 * WARNING - These are not threadsafe!  Make sure they are calle with
 * a mutex held.
 *
 * These are only safe as long as only one thread is accessing them.
 */



bytebuf_p tag_get_bytebuf(tag_p tag)
{
    bytebuf_p data = NULL;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with NULL tag pointer!");
        return NULL;
    }

    data = tag->data;

    return data;
}



int tag_set_bytebuf(tag_p tag, bytebuf_p data)
{
    bytebuf_p old_buf = NULL;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with NULL tag pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    old_buf = tag->data;
    tag->data = data;

    if(old_buf) {
        bytebuf_destroy(old_buf);
    }

    return PLCTAG_STATUS_OK;
}


int tag_set_impl_data(tag_p tag, void *impl_data)
{
    if(!tag) {
        pdebug(DEBUG_WARN,"Called with NULL tag pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    tag->impl_data = impl_data;

    return PLCTAG_STATUS_OK;
}


int tag_set_impl_op_func(tag_p tag, impl_operation_func op_func)
{
    if(!tag) {
        pdebug(DEBUG_WARN,"Called with NULL tag pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    tag->op_func = op_func;

    return PLCTAG_STATUS_OK;
}



attr tag_get_attribs(tag_p tag)
{
    if(!tag) {
        pdebug(DEBUG_WARN,"Called with NULL tag pointer!");
        return NULL;
    }

    /* static data once the tag is created. */
    return tag->attribs;
}



/***********************************************************************
 ************************** Helper Functions ***************************
 **********************************************************************/

void tag_destroy(void *tag_arg, int arg_count, void **args)
{
    tag_p tag = NULL;

    (void)args;

    pdebug(DEBUG_INFO,"Starting");

    if(arg_count != 0) {
        pdebug(DEBUG_WARN,"No args or NULL pointer passed! arg count=%d", arg_count);
        return;
    }

    tag = tag_arg;

    if(tag) {
        rc_dec(tag->impl_data);
        mutex_destroy(&tag->api_mut);
        mutex_destroy(&tag->external_mut);
        bytebuf_destroy(tag->data);
        attr_destroy(tag->attribs);
    }

    pdebug(DEBUG_INFO,"Done.");
}



/*
 * helper to insert a tag into the global hash table.
 */

int insert_tag(tag_p tag)
{
    tag_id id;

    critical_block(global_library_mutex) {
        int found = 0;

        id = global_tag_id;

        do {
            tag_p tmp;

            /* get a new ID */
            id++;

            if(id <= 0 || id > MAX_TAG_ID) {
                id = 1;
            }

            tmp = hashtable_get(tags, &id, sizeof(id));
            if(!tmp) {
                found = 1;

                global_tag_id = id;
                tag->id = id;

                /* FIXME - check return code! */
                hashtable_put(tags, &id, sizeof(id), tag);
            }
        } while(!found);
    }

    return PLCTAG_STATUS_OK;
}




int wait_for_timeout(tag_p tag, int timeout)
{
    int64_t timeout_time = timeout + time_ms();
    int rc = tag->op_func(tag, tag->impl_data, TAG_OP_STATUS);

    while(rc == PLCTAG_STATUS_PENDING && timeout_time > time_ms()) {
        rc = tag->op_func(tag, tag->impl_data, TAG_OP_STATUS);

        /*
         * terminate early and do not wait again if the
         * async operations are done.
         */
        if(rc != PLCTAG_STATUS_PENDING) {
            break;
        }

        sleep_ms(5); /* MAGIC */
    }

    /*
     * if we dropped out of the while loop but the status is
     * still pending, then we timed out.
     *
     * The create failed, so now we need to punt.
     */
    if(rc == PLCTAG_STATUS_PENDING) {
        pdebug(DEBUG_WARN, "Operation timed out.");
        rc = PLCTAG_ERR_TIMEOUT;
    }

    return rc;
}




tag_p get_tag(tag_id id)
{
    tag_p result = NULL;

    critical_block(global_library_mutex) {
        result = hashtable_get(tags, &id, sizeof(id));
        result = rc_inc(result);
    }

    return result;
}






/***********************************************************************
 ************************* Library Setup/Teardown **********************
 **********************************************************************/



/*
 * initialize_modules() is called the first time any kind of tag is
 * created.  It will be called before the tag creation routines are
 * run.
 */


int initialize_modules(void)
{
    int rc = PLCTAG_STATUS_OK;

    /* loop until we get the lock flag */
    while (!lock_acquire((lock_t*)&library_initialization_lock)) {
        sleep_ms(1);
    }

    if(!library_initialized) {
        pdebug(DEBUG_INFO,"Initializing modules.");

        pdebug(DEBUG_INFO,"Initializing library global mutex.");

        /* first see if the mutex is there. */
        if (!global_library_mutex) {
            rc = mutex_create((mutex_p*)&global_library_mutex);

            if (rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_ERROR, "Unable to create global tag mutex!");
            }
        }

        pdebug(DEBUG_INFO,"Creating internal tag hashtable.");
        if(rc == PLCTAG_STATUS_OK) {
            tags = hashtable_create(5); /* FIXME - MAGIC */
            if(!tags) {
                pdebug(DEBUG_ERROR,"Unable to create internal tag hashtable!");
                rc = PLCTAG_ERR_CREATE;
            }
        }

        pdebug(DEBUG_INFO,"Setting up resource service.");
        if(rc == PLCTAG_STATUS_OK) {
            rc = resource_service_init();

            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_ERROR,"Resource service failed to initialize correctly!");
            }
        }

        if(rc == PLCTAG_STATUS_OK) {
            rc = rc_thread_service_init();

            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_ERROR,"RCThread utility failed to initialize correctly!");
            }
        }

        //~ if(rc == PLCTAG_STATUS_OK) {
            //~ rc = ab_init();

            //~ if(rc != PLCTAG_STATUS_OK) {
                //~ pdebug(DEBUG_ERROR,"AB protocol failed to initialize correctly!");
            //~ }
        //~ }

        library_initialized = 1;

        /* hook the destructor */
        atexit(teardown_modules);

        pdebug(DEBUG_INFO,"Done initializing library modules.");
    }

    /* we hold the lock, so clear it.*/
    lock_release((lock_t*)&library_initialization_lock);

    return rc;
}




/*
 * teardown_modules() is called when the main process exits.
 *
 * Modify this for any PLC/protocol that needs to have something
 * torn down at the end.
 */

void teardown_modules(void)
{
    //~ ab_teardown();

    rc_thread_service_teardown();

    resource_service_teardown();

    pdebug(DEBUG_INFO,"Tearing down library.");


    pdebug(DEBUG_INFO,"Releasing internal tag hashtable.");
    hashtable_destroy(tags);

    pdebug(DEBUG_INFO,"Destroying global library mutex.");
    if(global_library_mutex) {
        mutex_destroy((mutex_p*)&global_library_mutex);
    }

    pdebug(DEBUG_INFO,"Done.");
}





