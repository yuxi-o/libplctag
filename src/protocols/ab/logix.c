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
#include <ab/logix/plc.h>
#include <ab/logix/packet.h>
#include <util/debug.h>
#include <util/job.h>
#include <util/resource.h>




#define SESSION_RESOURCE_PREFIX "AB CIP Session "

typedef enum {SESSION_STARTING, SESSION_OPEN_SESSION, SESSION_RUNNING} plc_state;


struct logix_plc_t {
    struct plc_t base;

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

    job_p monitor;

    bytebuf_p data;
};
typedef struct logix_plc_t *logix_plc_p;


struct request_t {
    int abort_requested;
    int read_requested;
    int write_requested;
    int operation_in_flight;

    int status;

    tag_p tag;
    bytebuf_p data;
};

typedef struct request_t *request_p;

/* declarations for our v-table functions */
static int tag_status(plc_p plc, tag_p tag);
static int tag_abort(plc_p plc, tag_p tag);
static int tag_read(plc_p plc, tag_p tag);
static int tag_write(plc_p plc, tag_p tag);



static int register_plc(logix_plc_p plc);
static job_exit_type plc_monitor(int arg_count, void **args);
static logix_plc_p create_plc(const char *path);
static void plc_destroy(void *plc_arg, int arg_count, void **args);

static int parse_path(const char *full_path, char **host, int *port, char **local_path);
static char *match_host(char *p);
static char *match_ip(char *p);
static char *match_hostname(char *p);
static char *match_local_path(char *p);
static char *match_number(char *p);


/***********************************************************************
 *********************** Public Functions ******************************
 **********************************************************************/


plc_p logix_plc_create(attr attribs)
{
    char *plc_name = NULL;
    logix_plc_p plc = NULL;
    const char *path = attr_get_str(attribs,"path",NULL);

    pdebug(DEBUG_INFO,"Starting");

    if(!path || str_length(path)) {
        pdebug(DEBUG_WARN,"PLC path is missing or empty!");
        return NULL;
    }

    plc_name = resource_make_name(SESSION_RESOURCE_PREFIX, path);
    if(!plc_name) {
        pdebug(DEBUG_WARN,"Unable to make PLC name!");
        return NULL;
    }

    if(str_length(plc_name) == 0) {
        pdebug(DEBUG_WARN,"PLC name is null or zero length!");
        mem_free(plc_name);
        return NULL;
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
                plc->monitor = job_create(plc_monitor, 0, 1, plc);
                if(!plc->monitor) {
                    pdebug(DEBUG_ERROR,"Unable to create plc monitor job!");
                    plc = rc_dec(plc);
                }
            }
        } else {
            /* create failed! */
            pdebug(DEBUG_ERROR,"Unable to create new plc!");
        }
    }

    if(plc_name) {
        mem_free(plc_name);
    }

    if(plc) {
        /* register the tag in the PLC tag store */

    }

    return (plc_p)plc;
}


//~ plc_p logix_tag_create(attr attribs)
//~ {
    //~ logix_plc_p plc = NULL;
    //~ int rc = PLCTAG_STATUS_OK;

    //~ /* FIXME */
    //~ (void)attribs;

    //~ pdebug(DEBUG_INFO,"Starting.");

    //~ /* build the new plc. */
    //~ plc = rc_alloc(sizeof(logix_plc_t), logix_plc_destroy);
    //~ if(!plc) {
        //~ pdebug(DEBUG_ERROR,"Unable to allocate memory for new plc!");
        //~ return NULL;
    //~ }


    //~ rc = mutex_create(&plc->mutex);
    //~ if(!rc == PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Unable to create mutex!");
        //~ logix_plc_destroy(plc, 0, NULL);
        //~ return NULL;
    //~ }

    //~ plc->status = PLCTAG_STATUS_PENDING;
    //~ plc->state = STARTING;

    //~ plc->path = str_dup(attr_get_str(attribs,"path",NULL));
    //~ plc->name = str_dup(attr_get_str(attribs,"name",NULL));
    //~ plc->element_count = attr_get_int(attribs,"elem_count",1);

    //~ /*
     //~ * the rest of this requires synchronization.  We are creating
     //~ * independent threads of control from this thread which could
     //~ * modify plc state.
     //~ */
    //~ critical_block(plc->mutex) {
        //~ /* create a monitor job to monitor the plc during operation. */
        //~ plc->monitor = job_create(plc_monitor, 0, 1, plc);
    //~ }

    //~ if(!plc->monitor) {
        //~ pdebug(DEBUG_ERROR,"Unable to start plc set up job!");
        //~ logix_plc_destroy(plc, 0, NULL);
        //~ break;
    //~ }

    //~ pdebug(DEBUG_INFO,"Done.");

    //~ return (plc_p)plc;
//~ }



int tag_status(plc_p plc_arg, tag_p tag)
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
            request_p *request = vector_get(plc->requests, i);

            if(request->tag == tag) {
                status = request->status;
                break;
            }
        }
    }

    return status;
}


int tag_abort(plc_p plc_arg, tag_p tag)
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
            request_p *request = vector_get(plc->requests, i);

            if(request->tag == tag) {
                request->abort_requested = 1;
                break;
            }
        }
    }

    return status;
}


int tag_read(plc_p plc, tag_p tag)
{

}


int tag_write(plc_p plc, tag_p tag)
{

}




/***********************************************************************
 ************************ Support Routines *****************************
 **********************************************************************/








int logix_get_plc_status(logix_plc_p plc)
{
    pdebug(DEBUG_DETAIL,"Starting.");

    if(!plc) {
        pdebug(DEBUG_WARN,"Called with null pointer.");
        return PLCTAG_ERR_NULL_PTR;
    }

    /*
     * FIXME - this is not thread safe, it will probably work, but
     * not on all platforms!
     */

     pdebug(DEBUG_DETAIL,"Done.");

     return plc->status;
}



int logix_plc_queue_request(logix_plc_p plc, logix_request_p request)
{
    int rc = PLCTAG_STATUS_OK;

    if(!plc) {
        pdebug(DEBUG_WARN,"Session pointer is NULL!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(!request) {
        pdebug(DEBUG_WARN,"Request pointer is NULL!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(!request->tag) {
        pdebug(DEBUG_WARN,"Request has NULL tag pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    critical_block(plc->mutex) {
        /* put the new request at the end */
        rc = vector_put(plc->requests, vector_length(plc->requests), request);
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



job_exit_type plc_monitor(int arg_count, void **args)
{
    logix_plc_p plc;
    int rc = PLCTAG_STATUS_OK;

    if(!args) {
        pdebug(DEBUG_WARN,"No args passed!");
        return JOB_DONE;
    }

    if(arg_count != 1) {
        pdebug(DEBUG_WARN,"Arg count should be 1 and is %d", arg_count);
        return JOB_DONE;
    }

    plc = args[0];
    if(!plc) {
        pdebug(DEBUG_WARN,"Session argument is NULL!");
        return JOB_DONE;
    }

    switch(plc->state) {
        case SESSION_STARTING:
            /* set up the socket */
            pdebug(DEBUG_DETAIL,"Setting up socket.");

            rc = socket_create(&plc->sock);
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_WARN,"Unable to create socket!");
                return JOB_DONE;
            }

            /* FIXME - this blocks! */
            rc = socket_connect_tcp(plc->sock, plc->host, plc->port);
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_WARN,"Unable to connect to PLC!");
                return JOB_DONE;
            }

            /* set up plc next. */
            plc->state = SESSION_OPEN_SESSION;

            break;

        case SESSION_OPEN_SESSION:
            /* register plc */
            pdebug(DEBUG_DETAIL,"Registering plc.");

            rc = register_plc(plc);
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_WARN,"Unable to register plc!");
                plc->status = rc;
                return JOB_DONE;
            }

            plc->state = SESSION_RUNNING;

            break;

        case SESSION_RUNNING:
            /* this is synchronous for now. */
            rc = process_request(plc);
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_WARN,"Unable to process request!");
                plc->status = rc;
                return JOB_DONE;
            }

            break;
    }

    return JOB_RERUN;
}



/*
 * Check to see if there is a pending request.  If there is, then
 * try to process it.
 */

int process_request(logix_plc_p plc)
{
    int rc = PLCTAG_STATUS_OK;
    logix_request_p request;

    critical_block(plc->mutex) {
        /* pop one off the beginning treating the requests list as a queue. */
        request = vector_remove(plc->requests, 0);
    }

    if(request) {
        /* there is something to do. */
        if(request->abort_requested) {
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
    rc = marshal_register_plc(plc->data,(uint16_t)AB_EIP_VERSION, (uint16_t)0);
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
    plc->state = SESSION_STARTING;
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
