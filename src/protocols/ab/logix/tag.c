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
#include <util/async_tcp_socket.h>
#include <util/attr.h>
#include <util/bytebuf.h>
#include <util/debug.h>
#include <util/hashtable.h>
#include <util/job.h>
#include <util/refcount.h>
#include <util/resource.h>
#include <ab/ab.h>
#include <ab/logix/packet.h>
#include <ab/logix/session.h>





//~ typedef enum { REQ_START, RECV_START } request_states;

typedef enum { STARTING, GET_SESSION, WAIT_SESSION, START_RETRY_SESSION, WAIT_RETRY_SESSION, RUNNING, BUSY, TERMINATING } tag_states;


typedef struct {
    struct plc_t base_tag;

    mutex_p mutex;

    int status;

    tag_states state;

    int read_requested;
    int write_requested;
    int abort_requested;

    const char *path;
    const char *name;
    int element_count;

    logix_session_p session;
    int session_retry_count;
    int session_current_retry;
    int64_t session_next_retry_time_ms;
    int *session_retries;

    bytebuf_p data;

    job_p monitor;

} logix_tag_t;

typedef logix_tag_t *logix_tag_p;



typedef struct {
    int req_id;         /* which request is this for the tag? */

    bytebuf_p data;
    int status;

    /* flags for communicating with background thread */
    lock_t lock;
    int send_request;
    int resp_received;
    int abort_request;
    int abort_after_send; /* for one shot packets */

    /* used when processing a response */
    int processed;
} logix_request_t;



/*
We need to implement the following functions:

struct impl_vtable {
    int (*abort)(plc_p tag);
    int (*get_size)(plc_p tag);
    int (*get_status)(plc_p tag);
    int (*start_read)(plc_p tag);
    int (*start_write)(plc_p tag);

    int (*get_int)(plc_p tag, int offset, int size, int64_t *val);
    int (*set_int)(plc_p tag, int offset, int size, int64_t val);
    int (*get_double)(plc_p tag, int offset, int size, double *val);
    int (*set_double)(plc_p tag, int offset, int size, double val);
};

*/





/* jobs */
//~ static job_exit_type setup_tag(int arg_count, void **args);
static job_exit_type tag_monitor(int arg_count, void **args);


//~ static void setup_tag_cleanup(void *tag_arg, int arg_count, void **args);
static void logix_tag_destroy(void *tag_arg, int arg_count, void **args);


/* tag vtable routines */
static int abort_operation(plc_p impl_plc);
static int get_size(plc_p impl_plc);
static int get_status(plc_p impl_plc);
static int start_read(plc_p impl_plc);
static int start_write(plc_p impl_plc);
static int get_int(plc_p impl_plc, int offset, int size, int64_t *val);
static int set_int(plc_p impl_plc, int offset, int size, int64_t val);
static int get_double(plc_p impl_plc, int offset, int size, double *val);
static int set_double(plc_p impl_plc, int offset, int size, double val);


/* set up the vtable for this kind of tag. */
static struct impl_vtable logix_vtable = { abort_operation, get_size, get_status, start_read, start_write,
                                           get_int, set_int, get_double, set_double };




plc_p logix_tag_create(attr attribs)
{
    logix_tag_p tag = NULL;
    int rc = PLCTAG_STATUS_OK;

    /* FIXME */
    (void)attribs;

    pdebug(DEBUG_INFO,"Starting.");

    /* build the new tag. */
    tag = rc_alloc(sizeof(logix_tag_t), logix_tag_destroy);
    if(!tag) {
        pdebug(DEBUG_ERROR,"Unable to allocate memory for new tag!");
        return NULL;
    }

    /* set up the vtable for this kind of tag. */
    tag->base_tag.vtable = &logix_vtable;
    tag->status = PLCTAG_STATUS_PENDING;

    rc = mutex_create(&tag->mutex);
    if(!rc == PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to create mutex!");
        logix_tag_destroy(tag, 0, NULL);
        return NULL;
    }

    tag->state = STARTING;

    tag->path = str_dup(attr_get_str(attribs,"path",NULL));
    tag->name = str_dup(attr_get_str(attribs,"name",NULL));
    tag->element_count = attr_get_int(attribs,"elem_count",1);

    /*
     * the rest of this requires synchronization.  We are creating
     * independent threads of control from this thread which could
     * modify tag state.
     */
    critical_block(tag->mutex) {
        /* create a monitor job to monitor the tag during operation. */
        tag->monitor = job_create(tag_monitor, 0, 1, tag);
        if(!tag->monitor) {
            pdebug(DEBUG_ERROR,"Unable to start tag set up job!");
            logix_tag_destroy(tag, 0, NULL);
            break;
        }
    }


    pdebug(DEBUG_INFO,"Done.");

    return (plc_p)tag;
}





/***********************************************************************
 ******************** Tag Implementation Functions *********************
 **********************************************************************/

int abort_operation(plc_p impl_plc)
{
    logix_tag_p tag = (logix_tag_p)impl_plc;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    tag->abort_requested = 1;

    return PLCTAG_STATUS_OK;
}



int get_size(plc_p impl_plc)
{
    logix_tag_p tag = (logix_tag_p)impl_plc;
    int size = 0;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(tag->data) {
        size = bytebuf_get_size(tag->data);
    }

    return size;
}


int get_status(plc_p impl_plc)
{
    logix_tag_p tag = (logix_tag_p)impl_plc;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    return tag->status;
}



int start_read(plc_p impl_plc)
{
    logix_tag_p tag = (logix_tag_p)impl_plc;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    critical_block(tag->mutex) {
        tag->read_requested = 1;
        tag->status = PLCTAG_STATUS_PENDING;
    }

    return PLCTAG_STATUS_OK;
}



int start_write(plc_p impl_plc)
{
    logix_tag_p tag = (logix_tag_p)impl_plc;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    critical_block(tag->mutex) {
        tag->write_requested = 1;
        tag->status = PLCTAG_STATUS_PENDING;
    }

    return PLCTAG_STATUS_OK;
}




int get_int(plc_p impl_plc, int offset, int size, int64_t *val)
{
    logix_tag_p tag = (logix_tag_p)impl_plc;
    int rc = PLCTAG_STATUS_OK;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    critical_block(tag->mutex) {
        if(offset < 0 || ((offset + size) > get_size(impl_plc))) {
            pdebug(DEBUG_WARN,"Offset out of bounds!");
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
            break;
        }

        rc = bytebuf_set_cursor(tag->data, offset);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Error setting cursor in byte buffer!");
            break;
        }

        /* handle supported sizes */
        switch(size) {
            case 1:
                rc = bytebuf_get_int(tag->data, size, byte_order_8, val);
                break;

            case 2:
                rc = bytebuf_get_int(tag->data, size, byte_order_16, val);
                break;

            case 4:
                rc = bytebuf_get_int(tag->data, size, byte_order_32, val);
                break;

            case 8:
                rc = bytebuf_get_int(tag->data, size, byte_order_64, val);
                break;

            default:
                rc = PLCTAG_ERR_UNSUPPORTED;
                break;
        }
    }

    return rc;
}



int set_int(plc_p impl_plc, int offset, int size, int64_t val)
{
    logix_tag_p tag = (logix_tag_p)impl_plc;
    int rc = PLCTAG_STATUS_OK;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    critical_block(tag->mutex) {
        if(offset < 0 || ((offset + size) > get_size(impl_plc))) {
            pdebug(DEBUG_WARN,"Offset out of bounds!");
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
            break;
        }

        rc = bytebuf_set_cursor(tag->data, offset);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Error setting cursor in byte buffer!");
            break;
        }

        /* handle supported sizes */
        switch(size) {
            case 1:
                rc = bytebuf_set_int(tag->data, size, byte_order_8, val);
                break;

            case 2:
                rc = bytebuf_set_int(tag->data, size, byte_order_16, val);
                break;

            case 4:
                rc = bytebuf_set_int(tag->data, size, byte_order_32, val);
                break;

            case 8:
                rc = bytebuf_set_int(tag->data, size, byte_order_64, val);
                break;

            default:
                rc = PLCTAG_ERR_UNSUPPORTED;
                break;
        }
    }

    return rc;
}



int get_double(plc_p impl_plc, int offset, int size, double *val)
{
    logix_tag_p tag = (logix_tag_p)impl_plc;
    int rc = PLCTAG_STATUS_OK;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    critical_block(tag->mutex) {
        if(offset < 0 || ((offset + size) > get_size(impl_plc))) {
            pdebug(DEBUG_WARN,"Offset out of bounds!");
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
            break;
        }

        rc = bytebuf_set_cursor(tag->data, offset);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Error setting cursor in byte buffer!");
            break;
        }

        /* handle supported sizes */
        switch(size) {
            case 4: {
                    int64_t tmp_long;
                    int32_t tmp_int;

                    rc = bytebuf_get_int(tag->data, size, byte_order_32, &tmp_long);

                    if(rc == PLCTAG_STATUS_OK) {
                        float tmp_float;

                        tmp_int = (int32_t)tmp_long;

                        mem_copy(&tmp_float, &tmp_int, sizeof(tmp_float) < sizeof(tmp_int) ? sizeof(tmp_float) : sizeof(tmp_int));

                        *val = (double)tmp_float;
                    }
                }
                break;

            case 8: {
                    int64_t tmp_int;

                    rc = bytebuf_get_int(tag->data, size, byte_order_64, &tmp_int);

                    if(rc == PLCTAG_STATUS_OK) {
                        double tmp_float;

                        mem_copy(&tmp_float, &tmp_int, sizeof(tmp_float) < sizeof(tmp_int) ? sizeof(tmp_float) : sizeof(tmp_int));

                        *val = (double)tmp_float;
                    }
                }
                break;

            default:
                rc = PLCTAG_ERR_UNSUPPORTED;
                break;
        }
    }

    return rc;
}

int set_double(plc_p impl_plc, int offset, int size, double val)
{
    logix_tag_p tag = (logix_tag_p)impl_plc;
    int rc = PLCTAG_STATUS_OK;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    critical_block(tag->mutex) {
        if(offset < 0 || ((offset + size) > get_size(impl_plc))) {
            pdebug(DEBUG_WARN,"Offset out of bounds!");
            rc = PLCTAG_ERR_OUT_OF_BOUNDS;
            break;
        }

        rc = bytebuf_set_cursor(tag->data, offset);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Error setting cursor in byte buffer!");
            break;
        }

        /* handle supported sizes */
        switch(size) {
            case 4: {
                    int32_t tmp_int = 0;
                    float tmp_float = (float)val; /* FIXME - this should cause a warning. */
                    int64_t tmp_long = 0;

                    mem_copy(&tmp_int, &tmp_float, sizeof(tmp_float) < sizeof(tmp_int) ? sizeof(tmp_float) : sizeof(tmp_int));

                    tmp_long = (int64_t)tmp_int;

                    rc = bytebuf_set_int(tag->data, size, byte_order_32, tmp_long);
                }
                break;

            case 8: {
                    int64_t tmp_long = 0;

                    mem_copy(&tmp_long, &val, sizeof(val) < sizeof(tmp_long) ? sizeof(val) : sizeof(tmp_long));

                    rc = bytebuf_set_int(tag->data, size, byte_order_64, tmp_long);
                }
                break;

            default:
                rc = PLCTAG_ERR_UNSUPPORTED;
                break;
        }
    }

    return rc;
}






/***********************************************************************
 ************************* Helper Functions ****************************
 **********************************************************************/


job_exit_type tag_monitor(int arg_count, void **args)
{
    logix_tag_p tag;
    int dead = 0;

    pdebug(DEBUG_SPEW,"Starting.");

    if(arg_count != 1 || !args) {
        pdebug(DEBUG_ERROR,"No arguments or null pointer passed to job!");
        return JOB_DONE;
    }

    tag = args[0];

    /*
     * get a strong reference to the tag to prevent it from
     * disappearing out from underneath us.
     */
    tag = rc_inc(tag);

    if(!tag) {
        pdebug(DEBUG_INFO,"Tag has gone away, exiting.");
        return JOB_DONE;
    }

    critical_block(tag->mutex) {
        switch(tag->state) {
            case STARTING:
                tag->state = GET_SESSION;
                break;

            case GET_SESSION:
                tag->session = logix_get_session(tag->path);

                if(!tag->session) {
                    /* this is fatal */
                    pdebug(DEBUG_WARN,"Unable to get session!");
                    tag->status = PLCTAG_ERR_OPEN;
                    tag->state = TERMINATING;
                } else {
                    /* got a session, now wait for it to be ready.*/
                    pdebug(DEBUG_DETAIL,"Got session, waiting for it to be ready.");
                    tag->status = PLCTAG_STATUS_PENDING;
                    tag->state = WAIT_SESSION;
                }
                break;

            case WAIT_SESSION:
                {
                    int session_status = logix_get_session_status(tag->session);

                    if(session_status == PLCTAG_STATUS_OK) {
                        pdebug(DEBUG_DETAIL,"Session is ready.  Going to RUNNING state.");
                        tag->status = PLCTAG_STATUS_OK;
                        tag->state = RUNNING;
                    } else if(session_status != PLCTAG_STATUS_PENDING) {
                        /* something went wrong! */
                        pdebug(DEBUG_WARN,"Session is not ready and is in a bad state %s", plc_tag_decode_error(session_status));
                        tag->session = rc_dec(tag->session);
                        tag->status = PLCTAG_STATUS_PENDING;
                        tag->state = START_RETRY_SESSION;
                    }
                }
                break;

            case START_RETRY_SESSION:
                /* retries left? */
                if(tag->session_current_retry < tag->session_retry_count) {
                    pdebug(DEBUG_INFO,"Retrying to connect to session.");
                    tag->session_next_retry_time_ms = tag->session_retries[tag->session_current_retry] + time_ms();
                    tag->session_current_retry++;
                    tag->state = WAIT_RETRY_SESSION;
                } else {
                    /* no retries left */
                    pdebug(DEBUG_WARN,"No session connection retries left!");
                    tag->status = PLCTAG_ERR_OPEN;
                    tag->state = TERMINATING;
                }
                break;

            case WAIT_RETRY_SESSION:
                if(time_ms() < tag->session_next_retry_time_ms) {
                    pdebug(DEBUG_DETAIL,"Wait time for session connection retry is over.");
                    tag->state = GET_SESSION;
                }
                break;

            case RUNNING:
                {
                    int session_status = logix_get_session_status(tag->session);

                    if(session_status != PLCTAG_STATUS_OK) {
                        pdebug(DEBUG_INFO,"PLC in bad state, retrying.");
                        tag->session = rc_dec(tag->session);
                        tag->status = PLCTAG_STATUS_PENDING;
                        tag->state = START_RETRY_SESSION;
                    } else {
                        /* session OK */
                        tag->status = PLCTAG_STATUS_OK;

                        /* check for requests. */
                        if(tag->read_requested) {
                            pdebug(DEBUG_DETAIL,"Starting read request.");

                            tag->read_request_in_flight = 1;

                            rc = tag_start_read(tag);
                            if(rc != PLCTAG_STATUS_OK) {
                                pdebug(DEBUG_WARN,"Unable to start read!");
                                tag->state = ERROR;
                                tag->status = rc;
                            } else {
                                tag->state = BUSY;
                                tag->status = PLCTAG_STATUS_PENDING;
                            }
                        } else if(tag->write_requested) {
                            pdebug(DEBUG_DETAIL,"Starting write request.");

                            tag->write_request_in_flight = 1;

                            rc = tag_start_write(tag);
                            if(rc != PLCTAG_STATUS_OK) {
                                pdebug(DEBUG_WARN,"Unable to start write!");
                                tag->state = ERROR;
                                tag->status = rc;
                            } else {
                                tag->state = BUSY;
                                tag->status = PLCTAG_STATUS_PENDING;
                            }
                        } else if(tag->abort_requested) {
                            pdebug(DEBUG_INFO,"Aborting any request in flight.");
                            if(tag->request) {
                                tag->request->abort_requested = 1;
                                tag->request = rc_dec(tag->request);

                                /* FIXME - should this be more complicated and check the current status? */
                                if(tag->status == PLCTAG_STATUS_PENDING)
                                tag->status = PLCTAG_STATUS_OK;
                            }
                        }
                    }
                }
                break;

            case BUSY:
                /* if anything is in flight, check the results. */
                if(tag->request) {
                    if(tag->abort_requested) {
                        tag->request->abort_requested = 1;

                        tag->read_requested = 0;
                        tag->write_requested = 0;

                        if(tag->state == PLCTAG_STATUS_PENDING) {
                            tag->state = PLCTAG_STATUS_OK;
                        }

                        tag->request = rc_dec(tag->request);
                    } else if(tag->request->status == PLCTAG_STATUS_OK) {
                        if(tag->read_requested && tag->request->read_requested) {
                            bytebuf_p old_buf = tag->data;

                            pdebug(DEBUG_DETAIL,"Read succeeded.");

                            /* mutex is already locked, so swap the byte buffers. */
                            tag->data = tag->request->data;
                            tag->request->data = old_buf;

                            tag->read_requested = 0;
                            tag->request = rc_dec(tag->request);

                            tag->status = PLCTAG_STATUS_OK;
                        } else if(tag->write_requested && tag->request->write_requested) {
                            pdebug(DEBUG_DETAIL, "Write succeeded.");

                            tag->write_requested = 0;
                            tag->request = rc_dec(tag->request);

                            tag->status = PLCTAG_STATUS_OK;
                        }
                    } else if(tag->request->status != PLCTAG_STATUS_PENDING) {
                        /* catch errors from downstream. */
                        pdebug(DEBUG_WARN,"Request failed!");

                        tag->status = tag->request->status;
                        tag->request->abort_requested = 1;

                        tag->request = rc_dec(tag->request);
                    }
                }

                if(!tag->request) {
                    pdebug(DEBUG_DETAIL,"Work is done, tag no longer busy.");

                    /* check status */
                    if(tag->status != PLCTAG_STATUS_OK && tag->status != PLCTAG_STATUS_PENDING) {
                        /* error!
                         *
                         * FIXME - do something smarter here. There are situations where we could
                         * try another connection later.
                         */
                         tag->state = TERMINATING;
                    } else {
                        pdebug(DEBUG_DETAIL,"Operation complete.");
                        tag->state = RUNNING;
                    }
                }
                break;

            case TERMINATING:
                dead = 1;
                break;

            default:
                pdebug(DEBUG_ERROR,"Unknown state (%d)!". tag->state);
                dead = 1;
                break;
       }
    }

    /* release our reference to the tag. */
    tag = rc_dec(tag);

    if(dead) {
        return JOB_DONE;
    }

    return JOB_RERUN;
}



int tag_start_read(logix_tag_p tag)
{
    pdebug(DEBUG_INFO,"Starting");

    tag->request = logix_request_create();
    if(!tag->request) {
        pdebug(DEBUG_WARN,"Unable to create new request!");
        return PLCTAG_ERR_NO_MEM;
    }

    request->read_requested = 1;
    request->tag = rc_inc(tag);

    pdebug(DEBUG_INFO,"Done, now queuing request.");

    return session_queue_request(tag->session, request);
}




int tag_start_write(logix_tag_p tag)
{
    pdebug(DEBUG_INFO,"Starting");

    tag->request = logix_request_create();
    if(!tag->request) {
        pdebug(DEBUG_WARN,"Unable to create new request!");
        return PLCTAG_ERR_NO_MEM;
    }

    request->write_requested = 1;
    request->tag = rc_inc(tag);

    pdebug(DEBUG_INFO,"Done, now queuing request.");

    return session_queue_request(tag->session, request);
}



void logix_tag_destroy(void *tag_arg, int arg_count, void **args)
{
    logix_tag_p tag = NULL;

    pdebug(DEBUG_INFO,"Starting.");

    (void)args;

    if(arg_count != 0) {
        pdebug(DEBUG_WARN,"Destructor called with no arguments or null arg pointer!");
        return;
    }

    tag = tag_arg;

    if(!tag) {
        pdebug(DEBUG_WARN,"Destructor called with null pointer!");
        return;
    }

    pdebug(DEBUG_INFO,"Cleaning up tag %s",tag->name);

    if(tag->monitor) {
        pdebug(DEBUG_DETAIL,"Removing monitor job.");
        tag->monitor = rc_dec(tag->monitor);
    }

    if(tag->mutex) {
        pdebug(DEBUG_DETAIL,"Destroying internal tag mutex.");
        mutex_destroy(&tag->mutex);
    }

    if(tag->session) {
        pdebug(DEBUG_DETAIL,"Releasing reference to session");
        tag->session = rc_dec(tag->session);
    }

    if(tag->path) {
        pdebug(DEBUG_DETAIL,"Freeing path memory.");
        mem_free(tag->path);
        tag->path = NULL;
    }

    if(tag->name) {
        pdebug(DEBUG_DETAIL,"Freeing name memory.");
        mem_free(tag->name);
        tag->name = NULL;
    }

    if(tag->session_retries) {
        pdebug(DEBUG_DETAIL,"Freeing retry array.");
        mem_free(tag->session_retries);
        tag->session_retries = NULL;
    }

    if(tag->data) {
        pdebug(DEBUG_DETAIL,"Freeing data bytebuf.");
        bytebuf_destroy(tag->data);
    }

    pdebug(DEBUG_WARN,"Done.");
}



