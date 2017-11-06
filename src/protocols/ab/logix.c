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


#include <ctype.h>
#include <platform.h>
#include <lib/libplctag.h>
#include <ab/packet.h>
#include <ab/logix.h>
#include <util/debug.h>
#include <util/rc_thread.h>
#include <util/resource.h>
#include <util/vector.h>




#define SESSION_RESOURCE_PREFIX "AB CIP Session "

typedef enum {PLC_STARTING, PLC_OPEN_SESSION, PLC_RUNNING, PLC_ERROR} plc_state;


struct logix_plc_t {
    char *full_path;
    char *host;
    int port;
    char *path;

    mutex_p mutex;
    vector_p requests;

    sock_p sock;

    int status;
    plc_state state;

    uint32_t plc_handle;
    uint64_t plc_context;

    rc_thread_p monitor;

    bytebuf_p data;
};
typedef struct logix_plc_t *logix_plc_p;


typedef enum {REQUEST_NONE, REQUEST_READ, REQUEST_WRITE} request_type;

typedef struct logix_request_t *logix_request_p;


/* declarations for our v-table functions */
static int dispatch_function(tag_p tag, void *impl_data, tag_operation op);
static int tag_status(void* plc, tag_p tag);
static int tag_abort(void* plc, tag_p tag);
static int tag_read(void* plc, tag_p tag);
static int tag_write(void* plc, tag_p tag);


static int setup_socket(logix_plc_p plc);
static int register_plc(logix_plc_p plc);
static void plc_monitor(int arg_count, void **args);
static logix_plc_p create_plc(const char *path);
static void plc_destroy(void *plc_arg, int arg_count, void **args);

static int process_incoming_data(logix_plc_p plc);
static int process_outgoing_requests(logix_plc_p plc);

static int parse_path(const char *full_path, char **host, int *port, char **local_path);
static char *match_host(char *p);
static char *match_ip(char *p);
static char *match_hostname(char *p);
static char *match_local_path(char *p);
static char *match_number(char *p);



static logix_request_p request_create(tag_p tag, request_type operation);
static void request_destroy(void *request_arg, int extra_arg_count, void **extra_args);
static bytebuf_p request_get_data(logix_request_p request);
static request_type request_get_type(logix_request_p request);
static tag_p request_get_tag(logix_request_p request);
static int request_abort(logix_request_p request);


/***********************************************************************
 *********************** Public Functions ******************************
 **********************************************************************/


int logix_tag_create(tag_p tag)
{
    char *plc_name = NULL;
    logix_plc_p plc = NULL;
    attr attribs = tag_get_attribs(tag);
    const char *path = NULL;
    bytebuf_p data = NULL;
    int rc = PLCTAG_STATUS_OK;


    pdebug(DEBUG_INFO,"Starting");

    if(!attribs) {
        pdebug(DEBUG_WARN,"Tag has no attributes!");
        return PLCTAG_ERR_NO_DATA;
    }

    path =  attr_get_str(attribs,"path",NULL);
    if(!path || str_length(path)) {
        pdebug(DEBUG_WARN,"PLC path is missing or empty!");
        return PLCTAG_ERR_BAD_PARAM;
    }

    /* set up the byte buffer. */
    data = bytebuf_create(1, AB_BYTE_ORDER_INT16, AB_BYTE_ORDER_INT32, AB_BYTE_ORDER_INT64, AB_BYTE_ORDER_FLOAT32, AB_BYTE_ORDER_FLOAT64);
    if(!data) {
        pdebug(DEBUG_ERROR,"Unable to create byte buffer for tag data!");
        return PLCTAG_ERR_NO_MEM;
    }

    rc = tag_set_bytebuf(tag, data);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set tag byte buffer!");
        return rc;
    }

    rc = tag_set_impl_op_func(tag, dispatch_function);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set tag byte buffer!");
        return rc;
    }

    /* get a reference to the PLC */
    plc_name = str_concat(SESSION_RESOURCE_PREFIX, path);
    if(!plc_name) {
        pdebug(DEBUG_WARN,"Unable to make PLC name!");
        return PLCTAG_ERR_NO_MEM;
    }

    if(str_length(plc_name) == 0) {
        pdebug(DEBUG_WARN,"PLC name is null or zero length!");
        mem_free(plc_name);
        return PLCTAG_ERR_BAD_PARAM;
    }

    pdebug(DEBUG_INFO,"Starting with path %s", path);

    plc = resource_get(plc_name);

    if(!plc) {
        pdebug(DEBUG_DETAIL,"Might need to create new plc.");

        /* better try to make a plc. */
        plc = create_plc(path);
        if(plc) {
            if(resource_put(plc_name, plc) == PLCTAG_ERR_DUPLICATE) {
                /* oops hit race condition, need to dump this, there is one already. */
                pdebug(DEBUG_DETAIL,"Oops! Someone else created plc already!");

                plc = rc_dec(plc);

                plc = resource_get(plc_name);
            } else {
                plc->monitor = rc_thread_create(plc_monitor, plc);
                if(!plc->monitor) {
                    pdebug(DEBUG_ERROR,"Unable to create plc monitor thread!");
                    plc = rc_dec(plc);
                    rc = PLCTAG_ERR_CREATE;
                }
            }
        }
    }

    if(!plc) {
        pdebug(DEBUG_WARN,"Unable to create or get PLC!");
        rc = PLCTAG_ERR_CREATE;
    }

    if(plc_name) {
        mem_free(plc_name);
    }

    return rc;
}


/***********************************************************************
 ************************* PLC Functions *******************************
 **********************************************************************/

int dispatch_function(tag_p tag, void *plc_arg, tag_operation op)
{
    int rc = PLCTAG_STATUS_OK;

    switch(op) {
        case TAG_OP_ABORT:
            rc = tag_abort(plc_arg, tag);
            break;

        case TAG_OP_READ:
            rc = tag_read(plc_arg, tag);
            break;

        case TAG_OP_STATUS:
            rc = tag_status(plc_arg, tag);
            break;

        case TAG_OP_WRITE:
            rc = tag_write(plc_arg, tag);
            break;

        default:
            pdebug(DEBUG_WARN,"Operation %d not implemented!",op);
            rc = PLCTAG_ERR_NOT_IMPLEMENTED;
            break;
    }

    return rc;
}


int tag_abort(void *plc_arg, tag_p tag)
{
    logix_plc_p plc = (logix_plc_p)plc_arg;
    int status = PLCTAG_STATUS_OK;

    if(!plc) {
        pdebug(DEBUG_WARN,"PLC pointer is NULL.");
    }

    critical_block(plc->mutex) {
        status = plc->status;
    }

    if(status != PLCTAG_STATUS_OK) {
        return status;
    }

    /* look up tag to see if it is in some sort of operation. */
    status = PLCTAG_STATUS_OK;

    critical_block(plc->mutex) {
        for(int i=0; i < vector_length(plc->requests); i++) {
            logix_request_p request = vector_get(plc->requests, i);

            if(request_get_tag(request) == tag) {
                request_abort(request);
                break;
            }
        }
    }

    return status;
}


int tag_read(void *plc_arg, tag_p tag)
{
    logix_plc_p plc = (logix_plc_p)plc_arg;
    int rc = PLCTAG_STATUS_OK;
    logix_request_p req = NULL;
    int busy = 0;

    if(!plc) {
        pdebug(DEBUG_WARN,"PLC pointer is NULL.");
    }

    critical_block(plc->mutex) {
        rc = plc->status;
    }

    if(rc != PLCTAG_STATUS_OK && rc != PLCTAG_STATUS_PENDING) {
        return rc;
    }

    /* look up tag to see if it is in some sort of operation. */
    rc = PLCTAG_STATUS_OK;

    critical_block(plc->mutex) {
        for(int i=0; i < vector_length(plc->requests); i++) {
            logix_request_p request = vector_get(plc->requests, i);

            if(request_get_tag(request) == tag) {
                busy = 1;
                rc = PLCTAG_ERR_BUSY;
                break;
            }
        }

        if(!busy) {
            req = request_create(rc_inc(tag), REQUEST_READ);
            if(!req) {
                pdebug(DEBUG_WARN, "Unable to create new request!");
                rc = PLCTAG_ERR_NO_MEM;
                break;
            }

            rc = vector_put(plc->requests, vector_length(plc->requests), req);
        }
    }

    /* OK means we queued the request, so we are now waiting. */
    if(rc == PLCTAG_STATUS_OK) {
        rc = PLCTAG_STATUS_PENDING;
    }

    return rc;

}




int tag_status(void *plc_arg, tag_p tag)
{
    logix_plc_p plc = (logix_plc_p)plc_arg;
    int status = PLCTAG_STATUS_OK;

    if(!plc) {
        pdebug(DEBUG_WARN,"PLC pointer is NULL.");
    }

    critical_block(plc->mutex) {
        status = plc->status;
    }

    if(status != PLCTAG_STATUS_OK) {
        return status;
    }

    /* look up tag to see if it is in some sort of operation. */
    status = PLCTAG_STATUS_OK;

    critical_block(plc->mutex) {
        for(int i=0; i < vector_length(plc->requests); i++) {
            logix_request_p request = vector_get(plc->requests, i);

            if(request_get_tag(request) == tag) {
                status = PLCTAG_STATUS_PENDING;
                break;
            }
        }
    }

    return status;
}


int tag_write(void *plc_arg, tag_p tag)
{
    logix_plc_p plc = (logix_plc_p)plc_arg;
    int rc = PLCTAG_STATUS_OK;
    logix_request_p req = NULL;
    int busy = 0;

    if(!plc) {
        pdebug(DEBUG_WARN,"PLC pointer is NULL.");
    }

    critical_block(plc->mutex) {
        rc = plc->status;
    }

    if(rc != PLCTAG_STATUS_OK && rc != PLCTAG_STATUS_PENDING) {
        return rc;
    }

    /* look up tag to see if it is in some sort of operation. */
    rc = PLCTAG_STATUS_OK;

    critical_block(plc->mutex) {
        for(int i=0; i < vector_length(plc->requests); i++) {
            logix_request_p request = vector_get(plc->requests, i);

            if(request_get_tag(request) == tag) {
                busy = 1;
                rc = PLCTAG_ERR_BUSY;
                break;
            }
        }

        if(!busy) {
            req = request_create(rc_inc(tag), REQUEST_WRITE);
            if(!req) {
                pdebug(DEBUG_WARN, "Unable to create new request!");
                rc = PLCTAG_ERR_NO_MEM;
                break;
            }

            rc = vector_put(plc->requests, vector_length(plc->requests), req);
        }
    }

    /* OK means we queued the request, so we are now waiting. */
    if(rc == PLCTAG_STATUS_OK) {
        rc = PLCTAG_STATUS_PENDING;
    }

    return rc;
}






/***********************************************************************
 ************************ Helper Functions *****************************
 **********************************************************************/


void plc_destroy(void *plc_arg, int arg_count, void **args)
{
    logix_plc_p plc;

    (void)arg_count;
    (void)args;

    pdebug(DEBUG_INFO,"Starting");

    if(!plc_arg) {
        pdebug(DEBUG_WARN,"Session passed is NULL!");
        return;
    }

    plc = plc_arg;

    if(plc->monitor) {
        pdebug(DEBUG_DETAIL,"Releasing monitor job.");
        plc->monitor = rc_dec(plc->monitor);
    }

    if(plc->mutex) {
        pdebug(DEBUG_DETAIL,"Freeing plc mutex.");
        mutex_destroy(&plc->mutex);
    }

    if(plc->requests) {
        for(int i = 0; i < vector_length(plc->requests); i++) {
            logix_request_p request = vector_get(plc->requests, i);

            if(request) {
                rc_dec(request);
            }
        }

        vector_destroy(plc->requests);

        plc->requests = NULL;
    }

    if(plc->full_path) {
        mem_free(plc->full_path);
    }

    if(plc->host) {
        mem_free(plc->host);
    }

    if(plc->path) {
        mem_free(plc->path);
    }

    if(plc->sock) {
        socket_close(plc->sock);
        socket_destroy(&plc->sock);
    }

    if(plc->data) {
        bytebuf_destroy(plc->data);
    }

    pdebug(DEBUG_INFO,"Done");
}



void plc_monitor(int arg_count, void **args)
{
    logix_plc_p plc;
    int rc = PLCTAG_STATUS_OK;

    if(!args) {
        pdebug(DEBUG_WARN,"No args passed!");
        return;
    }

    if(arg_count != 1) {
        pdebug(DEBUG_WARN,"Arg count should be 1 and is %d", arg_count);
        return;
    }

    plc = args[0];
    if(!plc) {
        pdebug(DEBUG_WARN,"Session argument is NULL!");
        return;
    }

    /* loop until we are aborted. */
    while(!rc_thread_check_abort()) {
        switch(plc->state) {
            case PLC_STARTING:
                /* set up the socket */
                pdebug(DEBUG_DETAIL,"Setting up socket.");

                rc = setup_socket(plc);
                if(rc != PLCTAG_STATUS_OK) {
                    pdebug(DEBUG_WARN,"Unable to connect socket to PLC!");
                    plc->status = rc;

                    plc->state = PLC_ERROR;
                } else {
                    /* set up a session to the PLC next. */
                    plc->state = PLC_OPEN_SESSION;
                }

                break;

            case PLC_OPEN_SESSION:
                /* register plc */
                pdebug(DEBUG_DETAIL,"Registering plc.");

                rc = register_plc(plc);
                if(rc != PLCTAG_STATUS_OK) {
                    pdebug(DEBUG_WARN,"Unable to register plc!");
                    plc->status = rc;

                    plc->state = PLC_ERROR;
                } else {
                    plc->state = PLC_RUNNING;
                }

                break;

            case PLC_RUNNING:
                /* this is synchronous for now. */
                rc = process_request(plc);
                if(rc != PLCTAG_STATUS_OK) {
                    pdebug(DEBUG_WARN,"Unable to process incoming data!");
                    plc->status = rc;

                    plc->state = PLC_ERROR;
                    break;
                }
                break;

            case PLC_ERROR: {
                    logix_request_p request = NULL;
                    /* just eat the requests as they come in and do not do anything. */
                    critical_block(plc->mutex) {
                        request = vector_remove(plc->requests,0);
                    }

                    if(request) {
                        rc_dec(request);
                    }
                }

                break;
        }

        sleep_ms(1);
    }


    return;
}


int setup_socket(logix_plc_p plc)
{
    int rc = PLCTAG_STATUS_OK;

    rc = socket_create(&plc->sock);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to create socket!");
        return rc;
    }

    /* FIXME - this blocks! */
    rc = socket_connect_tcp(plc->sock, plc->host, plc->port);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to connect to PLC!");
        return rc;
    }

    return rc;
}


int process_request(logix_plc_p plc)
{
    logix_request_p request = NULL;
    int rc = PLCTAG_STATUS_OK;

    /* get a request from the queue. */
    critical_block(plc->mutex) {
        request = vector_remove(plc->requests, 0);
    }

    if(request) {
        tag_p tag = request_get_tag(request);
        attr attribs = tag_get_attribs(tag);
        const char *name = attr_get_str(attribs, "name", NULL);
        bytebuf_p buf = request_get_data(request);
        tag_operation op = request_get_type(request);
        uint16_t command;
        uint16_t length;
        uint32_t session_handle;
        uint32_t status,
        uint64_t sender_context;
        uint32_t options;

        /* determine how to encode the packet. */
        switch(op) {
            case REQUEST_READ:
                rc = marshal_cip_read(buf, name);
                break;

            case REQUEST_WRITE:
                rc = marshal_cip_write(buf, name, tag_get_bytebuf(tag));
                break;

            default:
                pdebug(DEBUG_WARN,"Unsupported operation (%d)!", op);
                rc = PLCTAG_ERR_UNSUPPORTED;
                break;
        }

        if(rc != PLCTAG_STATUS_OK) {
            return rc;
        }

        rc = marshal_cip_cfp_unconnected(buf, plc->local_path);
        if(rc != PLCTAG_STATUS_OK) {
            return rc;
        }

        rc = send_eip_packet(plc->sock, AB_EIP_UNCONNECTED_SEND, plc->session_handle, plc->sender_context, buf);
        if(rc != PLCTAG_STATUS_OK) {
            return rc;
        }

        /* get the response. */
        rc = bytebuf_reset(buf);
        if(rc != PLCTAG_STATUS_OK) {
            return rc;
        }

        rc = receive_eip_packet(plc->sock, buf);
        if(rc != PLCTAG_STATUS_OK) {
            return rc;
        }

        rc = unmarshal_eip_header(buf, &command, &length, &session_handle, &status, &sender_context, &options);
        if(rc != PLCTAG_STATUS_OK) {
            return rc;
        }


    }

    return PLCTAG_STATUS_OK;
}

/*
 * Check to see if there is a pending request.  If there is, then
 * try to process it.
 */

int process_outgoing_requests(logix_plc_p plc)
{
    int rc = PLCTAG_STATUS_OK;
    logix_request_p request;

    critical_block(plc->mutex) {
        /* pop one off the beginning treating the requests list as a queue. */
        request = vector_remove(plc->requests, 0);
    }

    if(request) {
        /* there is something to do. */
        if(request_get_abort(request)) {
            /* clean up the request. */
            pdebug(DEBUG_DETAIL,"Request not started and abort requested.");

            rc_dec(request);

            return PLCTAG_STATUS_OK;
        }

        if(request->read_requested) {
            return process_read_request(plc, request);
        } else if(request->write_requested) {
            return process_write_request(plc, request);
        }
    } else {
        pdebug(DEBUG_SPEW,"No request to process.");
    }

    return PLCTAG_STATUS_OK;
}


int process_read_request(logix_plc_p plc, logix_request_p request)
{

}



int register_plc(logix_plc_p plc)
{
    int rc = PLCTAG_STATUS_OK;

    /* plc response data */
    uint16_t command = 0;
    uint16_t length = 0;
    uint32_t plc_handle= 0;
    uint32_t status = 0;
    uint64_t sender_context = 0;
    uint32_t options = 0;

    pdebug(DEBUG_INFO, "Starting.");

    /*
     * clear the plc data.
     *
     * We use the receiving buffer because we do not have a request and nothing can
     * be coming in on the socket yet.
     */
    bytebuf_reset(plc->data);

    /* request the specific EIP/CIP version */
    rc = marshal_register_session(plc->data,(uint16_t)AB_EIP_VERSION, (uint16_t)0);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to marshall register plc packet!");
        return rc;
    }

    rc = send_eip_packet(plc->sock, (uint16_t)AB_EIP_REGISTER_SESSION, (uint32_t)plc->plc_handle,(uint64_t)0, plc->data);
    if(rc != PLCTAG_STATUS_OK) {
        /* FIXME - should we kill the plc at this point? */
        pdebug(DEBUG_WARN,"Unable to send packet!");
        return rc;
    }

    /* reset the buffer so that we can read in data. */
    bytebuf_reset(plc->data);

    rc = receive_eip_packet(plc->sock, plc->data);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to read in response packet!");
        return rc;
    }

    /* set the cursor back to the start. */
    bytebuf_set_cursor(plc->data, 0);

    /* parse the header. */
    rc = unmarshal_eip_header(plc->data, &command, &length, &plc_handle, &status, &sender_context, &options);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error parsing EIP encapsulation header!");
        return rc;
    }

    /* check the response type */
    if (command != AB_EIP_REGISTER_SESSION) {
        pdebug(DEBUG_WARN, "EIP unexpected response packet type: %d!", (int)command);
        return PLCTAG_ERR_BAD_DATA;
    }

    /* check response status */
    if (status != (uint32_t)AB_EIP_OK) {
        pdebug(DEBUG_WARN, "EIP command failed, response code: %d", (int)status);
        return PLCTAG_ERR_REMOTE_ERR;
    }

    /*
     * after all that, save the plc handle, we will
     * use it in future packets.
     */
    plc->plc_handle = plc_handle;
    plc->status = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL,"Using plc handle %x.", plc_handle);

    pdebug(DEBUG_INFO, "Done.");

    return PLCTAG_STATUS_OK;
}




/*
 * path looks like "192.168.1.10<:port>,path,to,cpu"
 */

logix_plc_p create_plc(const char *path)
{
    logix_plc_p plc = NULL;
    int rc = PLCTAG_STATUS_OK;

    plc = rc_alloc(sizeof(struct logix_plc_t), plc_destroy);
    if(!plc) {
        pdebug(DEBUG_ERROR,"Unable to allocate PLC struct!");
        return NULL;
    }

    /* set up the vtable for this kind of plc. */
    plc->base.tag_abort = tag_abort;
    plc->base.tag_read = tag_read;
    plc->base.tag_status = tag_status;
    plc->base.tag_write = tag_write;

    /* set the remaining PLC struct members. */
    plc->resource_count = 1; /* MAGIC */
    plc->status = PLCTAG_STATUS_PENDING;
    plc->sock = NULL;
    plc->state = PLC_STARTING;
    plc->port = AB_EIP_DEFAULT_PORT;

    rc = parse_path(path, &plc->host, &plc->port, &plc->path);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR,"Unable to parse path!");
        rc_dec(plc);
        return NULL;
    }

    plc->data = bytebuf_create(550);  /* MAGIC */
    if(!plc->data) {
        pdebug(DEBUG_ERROR,"Unable to create plc buffer!");
        rc_dec(plc);
        return NULL;
    }

    rc = mutex_create(&plc->mutex);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR,"Unable to create plc mutex!");
        rc_dec(plc);
        return NULL;
    }

    plc->requests = vector_create(20, 10);
    if(!plc->requests) {
        pdebug(DEBUG_ERROR,"Unable to create plc request list!");
        rc_dec(plc);
        return NULL;
    }

    return plc;
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


char *str_dup_from_to(char *from, char *to)
{
    char tmp;
    char *result;

    tmp = *to;
    *to = 0;
    result = str_dup(from);
    *to = tmp;

    return result;
}

int parse_path(const char *full_path, char **host, int *port, char **local_path)
{
    char *p, *q;
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

    *host = str_dup_from_to(p, q);
    if(!*host) {
        pdebug(DEBUG_ERROR,"Unable to duplicate hostname string!");
        mem_free(tmp_path);
        return PLCTAG_ERR_BAD_PARAM;
    }
    p = q;

    pdebug(DEBUG_DETAIL,"Got host string %s", *host);

    /* port is optional */
    if(*p && *p == ':') {
        char tmp_char;
        int rc;

        p++;

        q = match_number(p);

        if(!q) {
            pdebug(DEBUG_WARN,"Bad path string, port comes after a colon and must be a number.");
            mem_free(tmp_path);
            return PLCTAG_ERR_BAD_PARAM;
        }

        tmp_char = *q;
        *q = 0;

        pdebug(DEBUG_DETAIL,"Matched port %s",p);

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

    *local_path = str_dup_from_to(p,q);
    if(!*local_path) {
        pdebug(DEBUG_ERROR,"Unable to duplicate local path string.");
        mem_free(tmp_path);
        return PLCTAG_ERR_BAD_PARAM;
    }

    pdebug(DEBUG_DETAIL,"Matched local path %s", *local_path);

    mem_free(tmp_path);

    pdebug(DEBUG_INFO,"Done.");

    return PLCTAG_STATUS_OK;
}

char *match_host(char *p)
{
    /* try IP address. */
    pdebug(DEBUG_DETAIL,"Starting.");

    char *q = match_ip(p);

    if(!q) {
        q = match_hostname(p);
    }

    pdebug(DEBUG_DETAIL,"Done.");
    return q;
}


char *match_ip(char *p)
{
    char *q = match_number(p);

    pdebug(DEBUG_DETAIL,"Starting.");

    if(!q) {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }

    if(*q != '.') {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    p = ++q;

    q = match_number(p);
    if(!q) {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }

    if(*q != '.') {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    p = ++q;

    q = match_number(p);
    if(!q) {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    if(*q != '.') {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    p = ++q;

    q = match_number(p);
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
        return NULL;
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return q;
}


/* FIXME - this should match IP addresses too! */
char *match_local_path(char *p)
{
    char *q = p;

    pdebug(DEBUG_DETAIL,"Starting.");

    while(*q && (isdigit(*q) || *q == ',')) {
        q++;
    }

    if(q == p) {
        pdebug(DEBUG_WARN,"A local path is required!");
        return NULL;
    }

    if(*q && !isdigit(*q) && *q != ',') {
        pdebug(DEBUG_WARN,"A local path must be a comma delimited list of numbers!");
        return NULL;
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return q;
}


char *match_number(char *p)
{
    char *q = p;

    pdebug(DEBUG_DETAIL,"Starting.");

    while(*q && isdigit(*q)) {
        q++;
    }

    if(q == p) {
        pdebug(DEBUG_WARN,"Bad number format!");
        return NULL;
    }

    return q;
}


/***********************************************************************
 ************************** Request Functions **************************
 **********************************************************************/

struct logix_request_t {
    int abort_requested;
    request_type operation;
    int operation_in_flight;

    int status;

    tag_p tag;
    bytebuf_p data;
};




 logix_request_p request_create(tag_p tag, request_type operation)
{
    logix_request_p req;

    req = rc_alloc(sizeof(struct logix_request_t), request_destroy);
    if(!req) {
        pdebug(DEBUG_ERROR,"Unable to allocate new request!");
        return NULL;
    }

    req->data = bytebuf_create(1, AB_BYTE_ORDER_INT16, AB_BYTE_ORDER_INT32, AB_BYTE_ORDER_INT64, AB_BYTE_ORDER_FLOAT32, AB_BYTE_ORDER_FLOAT64);
    if(!req->data) {
        pdebug(DEBUG_ERROR,"Unable to create new request data buffer!");
        rc_dec(req);
        return NULL;
    }

    return req;
}


void request_destroy(void *request_arg, int extra_arg_count, void **extra_args)
{
    logix_request_p request = request_arg;

    (void)extra_arg_count;
    (void)extra_args;

    if(!request) {
        pdebug(DEBUG_WARN,"Request pointer is NULL!");
        return;
    }

    bytebuf_destroy(request->data);

    rc_dec(request->tag);

    return;
}


bytebuf_p request_get_data(logix_request_p request)
{
    if(!request) {
        pdebug(DEBUG_WARN,"NULL request passed!");
        return NULL;
    }

    return request->data;
}


tag_operation request_get_type(logix_request_p request)
{
    if(!request) {
        pdebug(DEBUG_WARN,"NULL request passed!");
        return REQUEST_NONE;
    }

    return request->operation;
}


tag_p request_get_tag(logix_request_p request)
{
    if(!request) {
        pdebug(DEBUG_WARN,"NULL request passed!");
        return NULL;
    }

    return request->tag;
}


int request_abort(logix_request_p request)
{
    if(!request) {
        pdebug(DEBUG_WARN,"NULL request passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    request->abort_requested = 1;

    return PLCTAG_STATUS_OK;
}


