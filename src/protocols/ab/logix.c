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
#include <stdio.h>
#include <platform.h>
#include <lib/libplctag.h>
#include <ab/packet.h>
#include <ab/error_codes.h>
#include <ab/logix.h>
#include <util/debug.h>
#include <util/rc_thread.h>
#include <util/resource.h>
#include <util/vector.h>
#include <util/hashtable.h>


struct logix_tag_info_t {
    uint32_t instance_id;
    uint16_t type_info;
};

typedef struct logix_tag_info_t *logix_tag_info_p;


#define SESSION_RESOURCE_PREFIX "AB CIP Session "

typedef enum {PLC_STARTING, PLC_OPEN_SESSION, PLC_RUNNING, PLC_ERROR} plc_state;


struct logix_plc_t {
    char *full_path;
    char *host;
    int port;
    char *path;

    mutex_p mutex;
    vector_p requests;
    hashtable_p tag_info;

    sock_p sock;

    int status;

    uint32_t session_handle;
    uint64_t session_context;

    rc_thread_p monitor;

    bytebuf_p data;
};
typedef struct logix_plc_t *logix_plc_p;


typedef enum {REQUEST_NONE, REQUEST_ABORT, REQUEST_READ, REQUEST_WRITE, REQUEST_GET_TAGS} request_type;

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
static int cleanup_logix_info(hashtable_p table, void *key, int key_len, void *tag_info_entry);

static int process_request(logix_plc_p plc);
static int process_read_request(logix_plc_p plc, logix_request_p request);
static int process_write_request(logix_plc_p plc, logix_request_p request);
static int process_get_tags_request(logix_plc_p plc, logix_request_p request);

static int parse_path(const char *full_path, char **host, int *port, char **local_path);
static char *match_host(char *p);
static char *match_ip(char *p);
static char *match_hostname(char *p);
static char *match_local_path(char *p);
static char *match_number(char *p);



static logix_request_p request_create(tag_p tag, request_type operation);
static void request_destroy(void *request_arg, int extra_arg_count, void **extra_args);
static request_type request_get_type(logix_request_p request);
static tag_p request_get_tag(logix_request_p request);
static int request_abort(logix_request_p request);
static int request_get_abort(logix_request_p request);


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

    path = attr_get_str(attribs,"path",NULL);
    if(!path || !str_length(path)) {
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

    if(plc_name) {
        mem_free(plc_name);
    }

    if(!plc) {
        pdebug(DEBUG_WARN,"Unable to create or get PLC!");
        rc = PLCTAG_ERR_CREATE;
    }

    rc = tag_set_impl_data(tag, plc);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set implementation data on tag!");
        rc_dec(plc);
    }

    pdebug(DEBUG_INFO,"Done.");

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

    if(plc->tag_info) {
        /* clean out the tag info */
        hashtable_on_each(plc->tag_info, cleanup_logix_info);

        hashtable_destroy(plc->tag_info);
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



int cleanup_logix_info(hashtable_p table, void *key, int key_len, void *tag_info_entry)
{
    (void) table;
    (void) key;
    (void) key_len;

    mem_free(tag_info_entry);

    return PLCTAG_STATUS_OK;
}


void plc_monitor(int arg_count, void **args)
{
    logix_plc_p plc;
    int rc = PLCTAG_STATUS_OK;
    int state = PLC_STARTING;

    if(!args) {
        pdebug(DEBUG_WARN,"No args passed!");
        return;
    }

    if(arg_count != 1) {
        pdebug(DEBUG_WARN,"Arg count should be 1 and is %d", arg_count);
        //return;
    }

    plc = args[0];
    if(!plc) {
        pdebug(DEBUG_WARN,"Session argument is NULL!");
        return;
    }

    /* loop until we are aborted. */
    while(!rc_thread_check_abort()) {
        switch(state) {
            case PLC_STARTING:
                /* set up the socket */
                pdebug(DEBUG_DETAIL,"Setting up socket.");

                rc = setup_socket(plc);
                if(rc != PLCTAG_STATUS_OK) {
                    pdebug(DEBUG_WARN,"Unable to connect socket to PLC!");
                    plc->status = rc;

                    state = PLC_ERROR;
                } else {
                    /* set up a session to the PLC next. */
                    state = PLC_OPEN_SESSION;
                }

                break;

            case PLC_OPEN_SESSION:
                /* register plc */
                pdebug(DEBUG_DETAIL,"Registering plc.");

                rc = register_plc(plc);
                if(rc != PLCTAG_STATUS_OK) {
                    pdebug(DEBUG_WARN,"Unable to register plc!");
                    plc->status = rc;

                    state = PLC_ERROR;
                } else {
                    state = PLC_RUNNING;
                }

                break;

            case PLC_RUNNING:
                /* this is synchronous for now. */
                rc = process_request(plc);
                if(rc != PLCTAG_STATUS_OK) {
                    pdebug(DEBUG_WARN,"Unable to process incoming data!");
                    plc->status = rc;

                    state = PLC_ERROR;
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

    pdebug(DEBUG_INFO,"Starting.");

    /* get a request from the queue. */
    critical_block(plc->mutex) {
        request = vector_remove(plc->requests, 0);
    }

    if(request) {
        tag_operation op = request_get_type(request);

        switch(op) {
            case REQUEST_READ:
                rc = process_read_request(plc, request);
                break;

            case REQUEST_WRITE:
                rc = process_write_request(plc, request);
                break;

            case REQUEST_GET_TAGS:
                rc = process_get_tags_request(plc, request);
                break;

            default:
                pdebug(DEBUG_WARN,"Unsupported operation (%d)!", op);
                rc = PLCTAG_ERR_UNSUPPORTED;
                break;
        }

        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to process request, error %s!", plc_tag_decode_error(rc));
        }

        /* done with the response. */
        rc_dec(request);
    }

    pdebug(DEBUG_INFO,"Done.");

    return rc;
}



int process_read_request(logix_plc_p plc, logix_request_p request)
{
    (void) plc;
    (void) request;

    return PLCTAG_ERR_NOT_IMPLEMENTED;
}



int process_write_request(logix_plc_p plc, logix_request_p request)
{
    (void) plc;
    (void) request;

    return PLCTAG_ERR_NOT_IMPLEMENTED;
}


int process_get_tags_reply_entries(bytebuf_p buf, hashtable_p tag_info_table, uint32_t *last_instance_id)
{
    int rc = PLCTAG_STATUS_OK;
    int32_t instance_id;
    uint16_t string_len;
    char symbol_name[128] = {0,};
    uint16_t symbol_type;
    //~ int symbol_name_index;
    //~ int end_of_entry_index;
    logix_tag_info_p tag_info = NULL;

    if(bytebuf_get_cursor(buf) >= bytebuf_get_size(buf)) {
        pdebug(DEBUG_INFO,"No data left in buffer.");
        return PLCTAG_ERR_NO_DATA;
    }

    /* each entry looks like this:
    uint32_t instance_id    monotonically increasing but not contiguous
    uint16_t string_len     string length count
    uint8_t[] string_data   string bytes (string_len of them)
    uint16_t symbol_type    type of the symbol.
    */

    rc = bytebuf_unmarshal(buf, BB_U32, &instance_id, BB_U16, &string_len);
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to get instance ID or string length!");
        return rc;
    }

    /* save this for later */
    *last_instance_id = instance_id;

    /* copy the name for printing */
    rc = bytebuf_unmarshal(buf, BB_BYTES, symbol_name, ((sizeof(symbol_name)-1) < ((size_t)string_len) ? sizeof(symbol_name)-1 : (size_t)string_len));
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN, "Unable to get tag name!");
        return rc;
    }

    /* get the symbol type */
    rc = bytebuf_unmarshal(buf, BB_U16, &symbol_type);
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to get symbol type!");
        return rc;
    }

    pdebug(DEBUG_INFO,"Get symbol entry for \"%s\" with instance ID %x and type %x",symbol_name, (int)instance_id, (int)symbol_type);

    /* store the symbol data into the hashtable. */
    tag_info = hashtable_get(tag_info_table, symbol_name, (int)string_len);
    if(!tag_info) {
        /* make an entry */
        tag_info = mem_alloc(sizeof(struct logix_tag_info_t));
        if(!tag_info) {
            pdebug(DEBUG_ERROR,"Unable to allocate new tag_info struct!");
            return PLCTAG_ERR_NO_MEM;
        }

        /* add the instance to the hashtable */
        rc = hashtable_put(tag_info_table, bytebuf_get_buffer(buf), (int)string_len, tag_info);

        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to insert new info struct into tag info table!");
            return rc;
        }
    }

    /* perhaps the instance ID could be different? */
    tag_info->instance_id = instance_id;
    tag_info->type_info = symbol_type;

    return rc;
}




int process_get_tags_request(logix_plc_p plc, logix_request_p request)
{
    int rc = PLCTAG_STATUS_OK;
    bytebuf_p buf = plc->data;
    uint32_t last_instance_id = 0;
    uint16_t eip_command = 0;
    uint16_t eip_payload_length = 0;
    uint32_t session_handle = 0;
    uint32_t eip_status = 0;
    uint64_t session_context = 0;
    uint8_t cip_reply_service = 0;
    uint8_t cip_status = 0;
    uint16_t cip_extended_status = 0;

    do {
        /* set the cursor back to zero so that we can start writing the data to the socket. */
        rc = bytebuf_reset(plc->data);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Error inserting resetting byte buffer!");
            return rc;
        }

        rc = marshal_eip_header(marshal_cip_cfp_unconnected(marshal_cip_cm_unconnected(marshal_cip_get_tag_info(buf, last_instance_id),
                                                                                       buf, plc->path),
                                                            buf),
                                buf,  AB_EIP_UNCONNECTED_SEND, plc->session_handle, plc->session_context);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to marshal Tag info service request!");
            return rc;
        }

        do {
            rc = send_eip_packet(plc->sock, buf);
            if(rc != PLCTAG_STATUS_OK && rc != PLCTAG_STATUS_PENDING) {
                /* FIXME - should we kill the plc at this point? */
                pdebug(DEBUG_WARN,"Unable to send packet!");
                return rc;
            }
        } while(rc == PLCTAG_STATUS_PENDING && !request_get_abort(request) && !rc_thread_check_abort());

        if(rc_thread_check_abort() || request_get_abort(request)) {
            rc = PLCTAG_ERR_ABORT;
        }

        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Error sending session registration request!");
            return rc;
        }

        /* now get the reply */

        /* reset the buffer so that we can read in data. */
        bytebuf_reset(plc->data);

        do {
            rc = receive_eip_packet(plc->sock, plc->data);
            if(rc != PLCTAG_STATUS_OK && rc != PLCTAG_STATUS_PENDING) {
                pdebug(DEBUG_WARN,"Unable to read in response packet!");
                return rc;
            }
        } while(rc == PLCTAG_STATUS_PENDING && !request_get_abort(request) && !rc_thread_check_abort());

        if(rc_thread_check_abort() || request_get_abort(request)) {
            rc = PLCTAG_ERR_ABORT;
        }

        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Error receiving session registration response!");
            return rc;
        }

        /* process the response */
        rc = unmarshal_cip_cm_unconnected(unmarshal_cip_cfp_unconnected(unmarshal_eip_header(buf, &eip_command, &eip_payload_length, &session_handle, &eip_status, &session_context),
                                                                        buf),
                                          buf, &cip_reply_service, &cip_status, &cip_extended_status);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to receive EIP response packet, error %s!", plc_tag_decode_error(rc));
            return rc;
        }

        if(cip_status != AB_CIP_OK) {
            pdebug(DEBUG_WARN,"Response has error! %s (%s)", decode_cip_error(cip_status, cip_extended_status, 0), decode_cip_error(cip_status, cip_extended_status, 1));
            return PLCTAG_ERR_REMOTE_ERR;
        }

        do {
            rc = process_get_tags_reply_entries(buf, plc->tag_info, &last_instance_id);
        } while(rc == PLCTAG_STATUS_OK);

        if(rc == PLCTAG_ERR_NO_DATA) {
            /* not really an error. */
            rc = PLCTAG_STATUS_OK;
        }
    } while(!request_get_abort(request) && !rc_thread_check_abort() && rc == PLCTAG_STATUS_OK && cip_status == AB_CIP_STATUS_FRAG);

    return rc;
}





int register_plc(logix_plc_p plc)
{
    int rc = PLCTAG_STATUS_OK;

    /* plc response data */
    uint16_t command = 0;
    uint16_t length = 0;
    uint32_t session_handle= 0;
    uint32_t status = 0;
    uint64_t sender_context = 0;

    pdebug(DEBUG_INFO, "Starting.");

    /*
     * clear the plc data.
     *
     * We use the receiving buffer because we do not have a request and nothing can
     * be coming in on the socket yet.
     */
    bytebuf_reset(plc->data);

    /* create a register session packet */
    rc = marshal_eip_header(marshal_register_session(plc->data,(uint16_t)AB_EIP_VERSION, (uint16_t)0),
                            plc->data, (uint16_t)AB_EIP_REGISTER_SESSION, (uint32_t)plc->session_handle,(uint64_t)0);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error marshalling register session packet!");
        return rc;
    }

    /* set the cursor back to zero so that we can start writing the data to the socket. */
    rc = bytebuf_set_cursor(plc->data, 0);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error inserting setting bytebuffer cursor!");
        return rc;
    }

    do {
        rc = send_eip_packet(plc->sock, plc->data);
        if(rc != PLCTAG_STATUS_OK && rc != PLCTAG_STATUS_PENDING) {
            /* FIXME - should we kill the plc at this point? */
            pdebug(DEBUG_WARN,"Unable to send packet!");
            return rc;
        }
    } while(rc == PLCTAG_STATUS_PENDING && !rc_thread_check_abort());

    if(rc_thread_check_abort()) {
        rc = PLCTAG_ERR_ABORT;
    }

    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error sending session registration request!");
        return rc;
    }

    /* reset the buffer so that we can read in data. */
    bytebuf_reset(plc->data);

    do {
        rc = receive_eip_packet(plc->sock, plc->data);
        if(rc != PLCTAG_STATUS_OK && rc != PLCTAG_STATUS_PENDING) {
            pdebug(DEBUG_WARN,"Unable to read in response packet!");
            return rc;
        }
    } while(rc == PLCTAG_STATUS_PENDING && !rc_thread_check_abort());

    if(rc_thread_check_abort()) {
        rc = PLCTAG_ERR_ABORT;
    }

    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error receiving session registration response!");
        return rc;
    }

    /* set the cursor back to the start. */
    bytebuf_set_cursor(plc->data, 0);

    /* parse the header. */
    rc = unmarshal_eip_header(plc->data, &command, &length, &session_handle, &status, &sender_context);
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
    plc->session_handle = session_handle;
    plc->status = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL,"Using plc handle %x.", session_handle);

    pdebug(DEBUG_INFO, "Done.");

    return PLCTAG_STATUS_OK;
}




/*
 * path looks like "192.168.1.10<:port>,path,to,cpu"
 */

logix_plc_p create_plc(const char *path)
{
    logix_plc_p plc = NULL;
    logix_request_p req = NULL;
    int rc = PLCTAG_STATUS_OK;

    plc = rc_alloc(sizeof(struct logix_plc_t), plc_destroy);
    if(!plc) {
        pdebug(DEBUG_ERROR,"Unable to allocate PLC struct!");
        return NULL;
    }

    /* set up the vtable for this kind of plc. */
    //~ plc->base.tag_abort = tag_abort;
    //~ plc->base.tag_read = tag_read;
    //~ plc->base.tag_status = tag_status;
    //~ plc->base.tag_write = tag_write;

    /* set the remaining PLC struct members. */
    plc->status = PLCTAG_STATUS_PENDING;
    plc->sock = NULL;
    plc->port = AB_EIP_DEFAULT_PORT;

    rc = parse_path(path, &plc->host, &plc->port, &plc->path);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR,"Unable to parse path!");
        rc_dec(plc);
        return NULL;
    }

    plc->data = bytebuf_create(550, AB_BYTE_ORDER_INT16, AB_BYTE_ORDER_INT32, AB_BYTE_ORDER_INT64, AB_BYTE_ORDER_FLOAT32, AB_BYTE_ORDER_FLOAT64);  /* MAGIC */
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

    plc->requests = vector_create(20, 10); /* MAGIC */
    if(!plc->requests) {
        pdebug(DEBUG_ERROR,"Unable to create plc request list!");
        rc_dec(plc);
        return NULL;
    }

    plc->tag_info = hashtable_create(100); /* MAGIC */
    if(!plc->tag_info) {
        pdebug(DEBUG_ERROR,"Unable to create plc tag info table!");
        rc_dec(plc);
        return NULL;
    }

    /* queue a request to get the tag data. */
    req = request_create(NULL, REQUEST_GET_TAGS);
    if(!req) {
        pdebug(DEBUG_WARN, "Unable to create new request!");
        rc_dec(plc);
        return NULL;
    }

    rc = vector_put(plc->requests, vector_length(plc->requests), req);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR,"Unable to queue tag info request!");
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
    request_type operation;

    tag_p tag;
};




 logix_request_p request_create(tag_p tag, request_type operation)
{
    logix_request_p req;

    req = rc_alloc(sizeof(struct logix_request_t), request_destroy);
    if(!req) {
        pdebug(DEBUG_ERROR,"Unable to allocate new request!");
        return NULL;
    }

    req->tag = rc_inc(tag);
    req->operation = operation;

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

    rc_dec(request->tag);

    return;
}


request_type request_get_type(logix_request_p request)
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

    request->operation = REQUEST_ABORT;

    return PLCTAG_STATUS_OK;
}


int request_get_abort(logix_request_p request)
{
    if(!request) {
        pdebug(DEBUG_WARN,"NULL request passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    return (request->operation == REQUEST_ABORT);
}

