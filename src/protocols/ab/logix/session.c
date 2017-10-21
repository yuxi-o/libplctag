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
#include <ab/logix/session.h>
#include <ab/logix/packet.h>
#include <util/debug.h>
#include <util/job.h>
#include <util/resource.h>




#define SESSION_RESOURCE_PREFIX "AB CIP Session "

typedef enum {SESSION_STARTING, SESSION_OPEN_SESSION, SESSION_RUNNING} session_state;

struct eip_session_t {
    char *full_path;
    char *host;
    int port;
    char *path;

    mutex_p mutex;

    int resource_count;

    sock_p sock;

    int status;
    session_state state;

    uint32_t session_handle;
    uint64_t session_context;

    job_p monitor;

    bytebuf_p data;
};


static int register_session(eip_session_p session);
static job_exit_type session_monitor(int arg_count, void **args);
static eip_session_p create_session(const char *path);
static void session_destroy(void *session_arg, int arg_count, void **args);

static int parse_path(const char *full_path, char **host, int *port, char **local_path);
static char *match_host(char *p);
static char *match_ip(char *p);
static char *match_hostname(char *p);
static char *match_local_path(char *p);
static char *match_number(char *p);


eip_session_p get_session(const char *path)
{
    char *session_name = resource_make_name(SESSION_RESOURCE_PREFIX, path);
    eip_session_p session = NULL;

    pdebug(DEBUG_INFO,"Starting with path %s", path);

    if(!session_name) {
        pdebug(DEBUG_WARN,"Unable to make session name!");
        return NULL;
    }

    if(str_length(session_name) == 0) {
        pdebug(DEBUG_WARN,"Session name is null or zero length!");
        mem_free(session_name);
        return NULL;
    }

    session = resource_get(session_name);

    if(!session) {
        pdebug(DEBUG_DETAIL,"Might need to create new session.");

        /* better try to make a session. */
        session = create_session(path);
        if(session) {
            if(resource_put(session_name, session) == PLCTAG_ERR_DUPLICATE) {
                /* oops hit race condition, need to dump this, there is one already. */
                pdebug(DEBUG_DETAIL,"Oops! Someone else created session already!");

                session = rc_dec(session);

                session = resource_get(session_name);
            } else {
                session->monitor = job_create(session_monitor, 0, 1, session);
                if(!session->monitor) {
                    pdebug(DEBUG_ERROR,"Unable to create session monitor job!");
                    session = rc_dec(session);
                }
            }
        } else {
            /* create failed! */
            pdebug(DEBUG_ERROR,"Unable to create new session!");
        }
    }

    if(session_name) {
        mem_free(session_name);
    }

    return session;
}


int get_session_status(eip_session_p session)
{
    pdebug(DEBUG_DETAIL,"Starting.");

    if(!session) {
        pdebug(DEBUG_WARN,"Called with null pointer.");
        return PLCTAG_ERR_NULL_PTR;
    }

    /*
     * FIXME - this is not thread safe, it will probably work, but
     * not on all platforms!
     */

     pdebug(DEBUG_DETAIL,"Done.");

     return session->status;
}



/***********************************************************************
 ************************ Helper Functions *****************************
 **********************************************************************/


void session_destroy(void *session_arg, int arg_count, void **args)
{
    eip_session_p session;

    (void)arg_count;
    (void)args;

    pdebug(DEBUG_INFO,"Starting");

    if(!session_arg) {
        pdebug(DEBUG_WARN,"Session passed is NULL!");
        return;
    }

    session = session_arg;

    if(session->monitor) {
        pdebug(DEBUG_DETAIL,"Releasing monitor job.");
        session->monitor = rc_dec(session->monitor);
    }

    if(session->mutex) {
        pdebug(DEBUG_DETAIL,"Freeing session mutex.");
        mutex_destroy(&session->mutex);
    }

    if(session->full_path) {
        mem_free(session->full_path);
    }

    if(session->host) {
        mem_free(session->host);
    }

    if(session->path) {
        mem_free(session->path);
    }

    if(session->sock) {
        socket_close(session->sock);
        socket_destroy(&session->sock);
    }

    if(session->data) {
        bytebuf_destroy(session->data);
    }

    pdebug(DEBUG_INFO,"Done");
}



job_exit_type session_monitor(int arg_count, void **args)
{
    eip_session_p session;
    int rc = PLCTAG_STATUS_OK;

    if(!args) {
        pdebug(DEBUG_WARN,"No args passed!");
        return JOB_DONE;
    }

    if(arg_count != 1) {
        pdebug(DEBUG_WARN,"Arg count should be 1 and is %d", arg_count);
        return JOB_DONE;
    }

    session = args[0];
    if(!session) {
        pdebug(DEBUG_WARN,"Session argument is NULL!");
        return JOB_DONE;
    }

    switch(session->state) {
        case SESSION_STARTING:
            /* set up the socket */
            pdebug(DEBUG_DETAIL,"Setting up socket.");

            rc = socket_create(&session->sock);
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_WARN,"Unable to create socket!");
                return JOB_DONE;
            }

            /* FIXME - this blocks! */
            rc = socket_connect_tcp(session->sock, session->host, session->port);
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_WARN,"Unable to connect to PLC!");
                return JOB_DONE;
            }

            /* set up session next. */
            session->state = SESSION_OPEN_SESSION;

            break;

        case SESSION_OPEN_SESSION:
            /* register session */
            pdebug(DEBUG_DETAIL,"Registering session.");

            rc = register_session(session);
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_WARN,"Unable to register session!");
                session->status = rc;
                return JOB_DONE;
            }

            session->state = SESSION_RUNNING;

            break;

        case SESSION_RUNNING:
            /* check for incoming data. */

            /* check for outgoing data. */

            /* FIXME - implement something! */

            break;
    }

    return JOB_RERUN;
}






int register_session(eip_session_p session)
{
    int rc = PLCTAG_STATUS_OK;

    /* session response data */
    uint16_t command = 0;
    uint16_t length = 0;
    uint32_t session_handle= 0;
    uint32_t status = 0;
    uint64_t sender_context = 0;
    uint32_t options = 0;

    pdebug(DEBUG_INFO, "Starting.");

    /*
     * clear the session data.
     *
     * We use the receiving buffer because we do not have a request and nothing can
     * be coming in on the socket yet.
     */
    bytebuf_reset(session->data);

    /* request the specific EIP/CIP version */
    rc = marshal_register_session(session->data,(uint16_t)AB_EIP_VERSION, (uint16_t)0);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to marshall register session packet!");
        return rc;
    }

    rc = send_eip_packet(session->sock, (uint16_t)AB_EIP_REGISTER_SESSION, (uint32_t)session->session_handle,(uint64_t)0, session->data);
    if(rc != PLCTAG_STATUS_OK) {
        /* FIXME - should we kill the session at this point? */
        pdebug(DEBUG_WARN,"Unable to send packet!");
        return rc;
    }

    /* reset the buffer so that we can read in data. */
    bytebuf_reset(session->data);

    rc = receive_eip_packet(session->sock, session->data);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to read in response packet!");
        return rc;
    }

    /* set the cursor back to the start. */
    bytebuf_set_cursor(session->data, 0);

    /* parse the header. */
    rc = unmarshal_eip_header(session->data, &command, &length, &session_handle, &status, &sender_context, &options);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error parsing EIP encapsulation header!");
        return rc;
    }

    /* check the response status */
    if (command != AB_EIP_REGISTER_SESSION) {
        pdebug(DEBUG_WARN, "EIP unexpected response packet type: %d!", (int)command);
        return PLCTAG_ERR_BAD_DATA;
    }

    if (status != (uint32_t)AB_EIP_OK) {
        pdebug(DEBUG_WARN, "EIP command failed, response code: %d", (int)status);
        return PLCTAG_ERR_REMOTE_ERR;
    }

    /*
     * after all that, save the session handle, we will
     * use it in future packets.
     */
    session->session_handle = session_handle;
    session->status = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO, "Done.");

    return PLCTAG_STATUS_OK;
}




/*
 * path looks like "192.168.1.10<:port>,path,to,cpu"
 */

eip_session_p create_session(const char *path)
{
    eip_session_p session = NULL;
    int rc = PLCTAG_STATUS_OK;

    session = rc_alloc(sizeof(struct eip_session_t), session_destroy, 0);
    if(!session) {
        pdebug(DEBUG_ERROR,"Unable to allocate PLC struct!");
        return NULL;
    }

    rc = parse_path(path, &session->host, &session->port, &session->path);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR,"Unable to parse path!");
        rc_dec(session);
        return NULL;
    }

    session->data = bytebuf_create(550);  /* MAGIC */
    if(!session->data) {
        pdebug(DEBUG_ERROR,"Unable to create session buffer!");
        rc_dec(session);
        return NULL;
    }

    rc = mutex_create(&session->mutex);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR,"Unable to create session mutex!");
        rc_dec(session);
        return NULL;
    }

    session->resource_count = 1;
    session->status = PLCTAG_STATUS_PENDING;
    session->sock = NULL;
    session->state = SESSION_STARTING;

    return session;
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
    q = match_number(p);
    if(!q) {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    if(*q != '.') {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    q = match_number(p);
    if(!q) {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
    if(*q != '.') {
        pdebug(DEBUG_DETAIL,"No match.");
        return NULL;
    }
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

    if(!isdigit(*q) && *q != ',') {
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

    while(*q && isdigit(q)) {
        q++;
    }

    if(q == p) {
        pdebug(DEBUG_WARN,"Bad number format!");
        return NULL;
    }

    return q;
}
