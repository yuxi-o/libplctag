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
#include <util/bytebuf.h>
#include <util/debug.h>
#include <util/hashtable.h>
#include <util/pt.h>
#include <util/refcount.h>
#include <util/resource.h>
#include <ab/ab.h>
#include <ab/logix.h>



#define PLC_RESOURCE_PREFIX "AB Logix PLC"

typedef enum { STARTING, SEND_OPEN_SESSION, WAIT_SESSION_ACK, DONE } plc_states;

typedef struct {
    char *path;
    lock_t lock;
    int resource_count;
    sock_p sock;
    int status;
    plc_states state;
    thread_p worker_thread;
} logix_plc_t;

typedef logix_plc_t *logix_plc_p;


typedef enum { STARTING, GET_PLC, WAIT_PLC, START_RETRY_PLC, WAIT_RETRY_PLC, RUNNING, BUSY, TERMINATING } tag_states;

typedef struct {
    struct impl_tag_t base_tag;

    mutex_p mutex;

    int status;

    tag_states state;

    int read_requested;
    int write_requested;
    int abort_requested;

    const char *path;
    const char *name;
    int element_count;

    logix_plc_p plc;
    int plc_retry_count;
    int plc_current_retry;
    int64_t plc_next_retry_time_ms;
    int *plc_retries;

    bytebuf_p data;

    protothread_p worker;
    protothread_p monitor;

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



static PT_FUNC(setup_tag);
static void setup_tag_cleanup(void *tag_arg, int arg_count, void **args);
static void logix_tag_destroy(void *tag_arg, int arg_count, void **args);

//~ static rc_plc find_plc(attr attribs);
//~ static rc_plc create_plc(attr attribs);


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

impl_tag_p logix_tag_create(attr attribs)
{
    logix_tag_p tag = NULL;
    int rc = PLCTAG_STATUS_OK;
    const char *path;
    const char *name;
    const char *elems;

    /* FIXME */
    (void)attribs;

    pdebug(DEBUG_INFO,"Starting.");

    /* build the new tag. */
    tag = rc_alloc(sizeof(logix_tag_t), logix_tag_destroy, 0);
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
    tag->elem_count = attr_get_int(attribs,"elem_count",1);

    /*
     * the rest of this requires synchronization.  We are creating
     * independent threads of control from this thread which could
     * modify tag state.
     */
    critical_block(tag->mutex) {
        /* create a monitor protothread to monitor the tag during operation. */
        tag->monitor = pt_create(tag_monitor, 1, tag);
        if(!tag->monitor) {
            pdebug(DEBUG_ERROR,"Unable to start tag set up protothread!");
            logix_tag_destroy(tag, 0, NULL);
            break;
        }
    }


    pdebug(DEBUG_INFO,"Done.");

    return (impl_tag_p)tag;
}





/***********************************************************************
 *********************** Implementation Functions **********************
 **********************************************************************/


PT_FUNC(tag_monitor) {
    if(arg_count != 1 || !args) {
        pdebug(DEBUG_ERROR,"No arguments or null pointer passed to PT!");
        PT_EXIT;
    }

PT_BODY
    while(1) {
        logix_tag_p tag = args[0]
        int dead = 0;


        pdebug(DEBUG_DETAIL,"Running");

        /*
         * get a strong reference to the tag to prevent it from
         * disappearing out from underneath us.
         */
        tag = rc_inc(tag);

        if(!tag) {
            pdebug(DEBUG_INFO,"Tag has gone away, exiting.");
            PT_EXIT;
        }

        critical_block(tag->mutex) {
            switch(tag->state) {
                case STARTING:
                    tag->state = GET_PLC;
                    break;

                case GET_PLC:
                    tag->plc = get_plc(tag);

                    if(!tag->plc) {
                        /* this is fatal */
                        pdebug(DEBUG_WARN,"Unable to get PLC!");
                        tag->state = TERMINATING;
                        tag->status = PLCTAG_ERR_OPEN;
                    } else {
                        /* got a PLC, now wait for it to be ready.*/
                        tag->status = PLCTAG_STATUS_PENDING;
                        tag->state = WAIT_PLC;
                    }
                    break;

                case WAIT_PLC:
                    {
                        int plc_status = plc_get_status(tag->plc);

                        if(plc_status == PLCTAG_STATUS_OK) {
                            tag->status = PLCTAG_STATUS_OK;
                            tag->state = RUNNING;
                        } else if(plc_status != PLCTAG_STATUS_PENDING) {
                            /* something went wrong! */
                            tag->plc = rc_dec(tag->plc);
                            tag->status = PLCTAG_STATUS_PENDING;
                            tag->state = START_RETRY_PLC;
                        }
                    }
                    break;

                case START_RETRY_PLC:
                    /* retries left? */
                    if(tag->plc_current_retry < tag->plc_retry_count) {
                        pdebug(DEBUG_INFO,"Retrying to connect to PLC.");
                        tag->plc_next_retry_time_ms = tag->plc_retries[tag->plc_current_retry] + time_ms();
                        tag->plc_current_retry++;
                        tag->state = WAIT_RETRY_PLC;
                    } else {
                        /* no retries left */
                        pdebug(DEBUG_WARN,"No PLC retries left!");
                        tag->status = PLCTAG_ERR_OPEN;
                        tag->state = TERMINATING;
                    }
                    break;

                case WAIT_RETRY_PLC:
                    if(time_ms() < tag->plc_next_retry_time_ms) {
                        pdebug(DEBUG_DETAIL,"Wait time for PLC retry is over.");
                        tag->state = GET_PLC;
                    }
                    break;

                case RUNNING:
                    {
                        int plc_status = plc_get_status(tag->plc);

                        if(plc_status != PLCTAG_STATUS_OK) {
                            pdebug(DEBUG_INFO,"PLC in bad state, retrying.");
                            tag->plc = rc_dec(tag->plc);
                            tag->status = PLCTAG_STATUS_PENDING;
                            tag->state = START_RETRY_PLC;
                        } else {
                            /* PLC OK */
                            tag->status = PLCTAG_STATUS_OK;

                            /* check for requests. */
                            if(tag->read_requested) {
                                pdebug(DEBUG_DETAIL,"Starting read worker.");
                                tag->worker = pt_create(read_worker,1, tag);
                                if(!tag->worker) {
                                    pdebug(DEBUG_WARN,"Unable to create read worker PT!");
                                    tag->status = PLCTAG_ERR_NO_MEM;
                                    tag->state = TERMINATING;
                                } else {
                                    /* all OK */
                                    tag->status = PLCTAG_STATUS_PENDING;
                                    tag->state = BUSY;
                                }
                            } else if(tag->write_requested) {
                                pdebug(DEBUG_DETAIL,"Starting write worker.");
                                tag->worker = pt_create(write_worker,1, tag);
                                if(!tag->worker) {
                                    pdebug(DEBUG_WARN,"Unable to create write worker PT!");
                                    tag->status = PLCTAG_ERR_NO_MEM;
                                    tag->status = TERMINATING
                                } else {
                                    /* all OK */
                                    tag->status = PLCTAG_STATUS_PENDING;
                                    tag->state = BUSY;
                                }
                            }
                        }
                    }
                    break;

                case BUSY:
                    if(!tag->worker) {
                        pdebug(DEBUG_DETAIL,"Work is done, tag no longer busy.");
                        tag->state = RUNNING;
                    }
                    break;

                case TERMINATING:
                    dead = 1;
                    break;
           }
        }

        /* release our reference to the tag. */
        tag = rc_dec(tag);

        if(dead) {
            PT_EXIT;
        }

        PT_YIELD;
    }

PT_END
}




/***********************************************************************
 ************************ Helper Functions *****************************
 **********************************************************************/


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

    if(tag->mutex) {
        mutex_destroy(&tag->mutex);
    }

    if(tag->path) {
        mem_free(tag->path);
        tag->path = NULL;
    }

    if(tag->name) {
        mem_free(tag->name);
        tag->name = NULL;
    }

    if(tag->plc_retries) {
        mem_free(tag->plc_retries);
        tag->plc_retries = NULL;
    }

    if(tag->data) {
        bytebuf_destroy(tag->data);
    }

    if(tag->worker) {
        rc_dec(tag->worker);
    }

    if(tag->monitor) {
        rc_dec(tag->monitor);
    }

    pdebug(DEBUG_WARN,"Done.");
}




/***********************************************************************
 ******************** Tag Implementation Functions *********************
 **********************************************************************/

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

    if(tag->data) {
        size = bytebuf_size(tag->data);
    }

    return size;
}


int get_status(impl_tag_p impl_tag)
{
    logix_tag_p tag = (logix_tag_p)impl_tag;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    return tag->status;
}



int start_read(impl_tag_p impl_tag)
{
    logix_tag_p tag = (logix_tag_p)impl_tag;
    int rc = PLCTAG_STATUS_OK;

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



int start_write(impl_tag_p impl_tag)
{
    logix_tag_p tag = (logix_tag_p)impl_tag;
    int rc = PLCTAG_STATUS_OK;

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




int get_int(impl_tag_p impl_tag, int offset, int size, int64_t *val)
{
    logix_tag_p tag = (logix_tag_p)impl_tag;
    int rc = PLCTAG_STATUS_OK;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(offset < 0 || ((offset + size) > get_size(tag))) {
        pdebug(DEBUG_WARN,"Offset out of bounds!");
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    rc = bytebuf_set_cursor(tag->data, offset);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error setting cursor in byte buffer!");
        return rc;
    }

    /* handle supported sizes */
    switch(size) {
        case 1: {
                int byte_order[1] = {0};
                rc = bytebuf_get_int(tag->data, size, byte_order, val);
            }
            break;

        case 2: {
                int byte_order[2] = {0,1};
                rc = bytebuf_get_int(tag->data, size, byte_order, val);
            }
            break;

        case 4: {
                int byte_order[4] = {0,1,2,3};
                rc = bytebuf_get_int(tag->data, size, byte_order, val);
            }
            break;

        case 8: {
                int byte_order[8] = {0,1,2,3,4,5,6,7};
                rc = bytebuf_get_int(tag->data, size, byte_order, val);
            }
            break;

        default:
            rc = PLCTAG_ERR_UNSUPPORTED;
            break;
    }

    return rc;
}



int set_int(impl_tag_p impl_tag, int offset, int size, int64_t val)
{
    logix_tag_p tag = (logix_tag_p)impl_tag;
    int rc = PLCTAG_STATUS_OK;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(offset < 0 || ((offset + size) > get_size(tag))) {
        pdebug(DEBUG_WARN,"Offset out of bounds!");
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    rc = bytebuf_set_cursor(tag->data, offset);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error setting cursor in byte buffer!");
        return rc;
    }

    /* handle supported sizes */
    switch(size) {
        case 1: {
                int byte_order[1] = {0};
                rc = bytebuf_set_int(tag->data, size, byte_order, val);
            }
            break;

        case 2: {
                int byte_order[2] = {0,1};
                rc = bytebuf_set_int(tag->data, size, byte_order, val);
            }
            break;

        case 4: {
                int byte_order[4] = {0,1,2,3};
                rc = bytebuf_set_int(tag->data, size, byte_order, val);
            }
            break;

        case 8: {
                int byte_order[8] = {0,1,2,3,4,5,6,7};
                rc = bytebuf_set_int(tag->data, size, byte_order, val);
            }
            break;

        default:
            rc = PLCTAG_ERR_UNSUPPORTED;
            break;
    }

    return rc;
}



int get_double(impl_tag_p impl_tag, int offset, int size, double *val)
{
    logix_tag_p tag = (logix_tag_p)impl_tag;
    int rc = PLCTAG_STATUS_OK;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(offset < 0 || ((offset + size) > get_size(tag))) {
        pdebug(DEBUG_WARN,"Offset out of bounds!");
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    rc = bytebuf_set_cursor(tag->data, offset);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error setting cursor in byte buffer!");
        return rc;
    }

    /* handle supported sizes */
    switch(size) {
        case 4: {
                int byte_order[4] = {0,1,2,3};
                int32_t tmp_int;

                rc = bytebuf_get_int(tag->data, size, byte_order, &tmp_int);

                if(rc == PLCTAG_STATUS_OK) {
                    float tmp_float;

                    mem_copy(&tmp_float, &tmp_int, sizeof(tmp_float) < sizeof(tmp_int) ? sizeof(tmp_float) : sizeof(tmp_int));

                    *val = (double)tmp_float;
                }
            }
            break;

        case 8: {
                int byte_order[8] = {0,1,2,3,4,5,6,7};
                int64_t tmp_int;

                rc = bytebuf_get_int(tag->data, size, byte_order, &tmp_int);

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

    return rc;
}

int set_double(impl_tag_p impl_tag, int offset, int size, double val)
{
    logix_tag_p tag = (logix_tag_p)impl_tag;
    int rc = PLCTAG_STATUS_OK;

    if(!tag) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(offset < 0 || ((offset + size) > get_size(tag))) {
        pdebug(DEBUG_WARN,"Offset out of bounds!");
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    rc = bytebuf_set_cursor(tag->data, offset);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error setting cursor in byte buffer!");
        return rc;
    }

    /* handle supported sizes */
    switch(size) {
        case 4: {
                int byte_order[4] = {0,1,2,3};
                int32_t tmp_int = 0;
                float tmp_float = (float)val; /* FIXME - this should cause a warning. */
                int64_t tmp_long = 0;

                mem_copy(&tmp_int, &tmp_float, sizeof(tmp_float) < sizeof(tmp_int) ? sizeof(tmp_float) : sizeof(tmp_int));

                tmp_long = (int64_t)tmp_int;

                rc = bytebuf_set_int(tag->data, size, byte_order, &tmp_long);
            }
            break;

        case 8: {
                int byte_order[8] = {0,1,2,3,4,5,6,7};
                int64_t tmp_long = 0;

                mem_copy(&tmp_long, &val, sizeof(val) < sizeof(tmp_long) ? sizeof(val) : sizeof(tmp_long));

                rc = bytebuf_set_int(tag->data, size, byte_order, &tmp_long);
            }
            break;

        default:
            rc = PLCTAG_ERR_UNSUPPORTED;
            break;
    }

    return rc;
}


/***********************************************************************
 *********************** Support Functions *****************************
 **********************************************************************/



logix_plc_p get_plc(const char *path)
{
    char *plc_name = resource_make_name(PLC_RESOURCE_PREFIX, path);
    logix_plc_p plc = NULL;

    pdebug(DEBUG_INFO,"Starting with path %s", path);

    if(!plc_name) {
        pdebug(DEBUG_WARN,"Unable to make PLC name!");
        return NULL;
    }

    if(str_length(plc_name) == 0) {
        pdebug(DEBUG_WARN,"PLC name is null or zero length!");
        mem_free(plc_name);
        return NULL;
    }

    plc = resource_get(plc_name);

    if(!plc) {
        pdebug(DEBUG_DETAIL,"Might need to create new PLC.");

        /* better try to make a PLC. */
        plc = create_plc(tag->path);
        if(plc) {
            if(resource_put(plc_name, plc) == PLCTAG_ERR_DUPLICATE) {
                /* oops hit race condition, need to dump this, there is one already. */
                pdebug(DEBUG_DETAIL,"Oops! Someone else created PLC already!");

                plc = rc_dec(plc);

                plc = resource_get(PLC_RESOURCE_PREFIX, path);
            } else {
                plc->sock = async_tcp_socket_create(plc->host, plc->port);
                if(!plc->sock) {
                    pdebug(DEBUG_ERROR,"Unable to create new socket!");
                    plc = rc_dec(plc);
                }
            }
        } else {
            /* create failed! */
            pdebug(DEBUG_ERROR,"Unable to create new PLC!");
        }
    }

    if(plc_name) {
        mem_free(plc_name);
    }

    return plc;
}



/*
 * path looks like "192.168.1.10<:port>,path,to,cpu"
 */

logix_plc_p create_plc(const char *path)
{
    logix_plc_p plc = NULL;
    int rc = PLCTAG_STATUS_OK;

    plc = rc_alloc(sizeof(logix_plc_t), logix_plc_destroy, 0);
    if(!plc) {
        pdebug(DEBUG_ERROR,"Unable to allocate PLC struct!");
        return NULL;
    }
    char *path;
    lock_t lock;
    int resource_count;
    sock_p sock;
    int status;

    rc = parse_path(path, &plc->host, &plc->port, &plc->path);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR,"Unable to parse path!");
        rc_dec(plc);
        return NULL;
    }

    plc->lock = LOCK_INIT;
    plc->resource_count = 1;
    plc->status = PLCTAG_STATUS_PENDING;
    plc->sock = NULL;
    plc->state = STARTING;

    return plc;
}

char *copy_string(const char *src, int len)
{
    char *result = mem_alloc(len+1);

    if(!result) {
        pdebug(DEBUG_ERROR,"Unable to allocate memory for new string!");
        return NULL;
    }

    str_copy(result, len, src);
    result[len-1] = 0;

    return result;
}



/*
path ::= host (: port) (local_path)
host ::= IPaddr | hostname
port ::= number
local_path ::= (, number)*
IPaddr ::= number . number . number . number
hostname ::= [a-zA-Z]+ ([^,:])*
number ::= [0-9]+
*/

int parse_path(const char *full_path, char **host, int *port, char **local_path)
{
    int len;
    char *p, *q;
    char tmp_char;
    char *tmp_path = str_dup(full_path);

    pdebug(DEBUG_INFO,"Starting");

    if(!tmp_path) {
        pdebug(DEBUG_ERROR,"Unable to copy path string!");
        return PLCTAG_ERR_NO_MEM;
    }

    /* scan for the end of the host/IP address. */
    p = tmp_path;

    q = match_host(p);
    if(!q) {
        pdebug(DEBUG_WARN,"Bad hostname/ip format in path!");
        mem_free(tmp_path);
        return PLCTAG_ERR_BAD_PARAM;
    }

    tmp_char = *q;
    *q = 0;
    *host = str_dup(p);
    if(!*host) {
        pdebug(DEBUG_ERROR,"Unable to duplicate hostname string!");
        mem_free(tmp_path);
        return PLCTAG_ERR_BAD_PARAM;
    }
    *q = tmp_char;
    p = q;

    q = match_port(p);
    if(q) {
        tmp_char = *q;
        *q = 0;
        rc = str_to_int(p, port);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Bad port number format!");
            mem_free(tmp_path);
            return PLCTAG_ERR_BAD_PARAM;
        }
        *q = tmp_char;
        p = q;
    }

    q = match_local_path(p);
    if(!q) {
        pdebug(DEBUG_WARN,"Bad local path format!");
        mem_free(tmp_path);
        return PLCTAG_ERR_BAD_PARAM;
    }

    *local_path = str_dup(p);
    if(!*local_path) {
        pdebug(DEBUG_ERROR,"Unable to duplicate local path string.");
        mem_free(tmp_path);
        return PLCTAG_ERR_BAD_PARAM;
    }

    pdebug(DEBUG_INFO,"Done.");

    return PLCTAG_STATUS_OK;
}

char *match_host(char *p) {
    /* try IP address. */
    pdebug(DEBUG_DETAIL,"Starting.");
    char *q = match_ip(p, hostname);

    if(!q) {
        q = match_hostname(p);
    }

    pdebug(DEBUG_DETAIL,"Done.");
    return q;
}

char *match_ip(char *p) {
    int octet = 0;
    char *q = match_number(p, &octet);

    pdebug(DEBUG_DETAIL,"Starting.");

    if(!q) {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    if(*q != '.') {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    q = match_number(p, &octet);
    if(!q) {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    if(*q != '.') {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    q = match_number(p, &octet);
    if(!q) {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    if(*q != '.') {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    q = match_number(p, &octet);
    if(!q) {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return q;
}


char *match_hostname(char *p)
{
    char *q = p;

    pdebug(DEBUG_DETAIL,"Starting.");

    while(*q && *q != ':' && *q != ',') {
        q++;
    }

    if(q == p) {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL
    }

    return q;
}
