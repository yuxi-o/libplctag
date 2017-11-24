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
#include <stdlib.h>
#include <platform.h>
#include <lib/libplctag.h>
#include <ab/packet.h>
#include <ab/error_codes.h>
#include <util/bytebuf.h>
#include <util/debug.h>
#include <util/rc_thread.h>





#define EIP_ENCAP_SIZE (24)





static int cip_encode_path(bytebuf_p buf, const char *ioi_path);
static int cip_encode_tag_name(bytebuf_p buf, const char *name);


#define MARSHAL_FIELD(BUF, BITS, FIELD)                                \
    do {                                                               \
        rc = bytebuf_set_int ## BITS (BUF, (int ## BITS ## _t)FIELD);  \
        if(rc != PLCTAG_STATUS_OK) {                                   \
            pdebug(DEBUG_WARN,"Error inserting '" #FIELD "' field!");  \
            return rc;                                                 \
        }                                                              \
    } while(0)


#define UNMARSHAL_FIELD(BUF, BITS, FIELD)                              \
    do {                                                               \
        rc = bytebuf_get_int ## BITS (BUF, (int ## BITS ## _t *)FIELD);\
        if(rc != PLCTAG_STATUS_OK) {                                   \
            pdebug(DEBUG_WARN,"Error unmarshalling '" #FIELD "' field!");\
            return rc;                                                 \
        }                                                              \
    } while(0)




int marshal_eip_header(int prev_rc, bytebuf_p buf,
                        uint16_t command,         /* command, like Register Session*/
                        uint32_t session_handle,  /* from session set up */
                        uint64_t sender_context)  /* identifies the unconnected session packet */
{
    int rc = PLCTAG_STATUS_OK;
    uint16_t payload_size;

    pdebug(DEBUG_DETAIL,"Starting.");

    if(prev_rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Aborting due to previous bad return code.");
        return prev_rc;
    }

    payload_size = bytebuf_get_size(buf);

    /* two calls, first gets size, second writes data. */
    for(int i=0; rc == PLCTAG_STATUS_OK && i < 2; i++) {
        rc = bytebuf_marshal(i == 0 ? NULL : buf ,
                             BB_U16, command,
                             BB_U16, payload_size,
                             BB_U32, session_handle,
                             BB_U32, (uint32_t)0,   /* status, always sent as zero */
                             BB_U64, sender_context,
                             BB_U32, (uint32_t)0    /* options, always zero. */
                            );
        if(i == 0 && rc > 0) {
            /* make space at the beginning of the buffer. */
            pdebug(DEBUG_DETAIL,"Making %d bytes of space at the beginning for the header.", rc);
            rc = bytebuf_set_cursor(buf, - rc); /* rc contains the header size. */
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_WARN,"Error setting buffer cursor!");
                return rc;
            }

            rc = PLCTAG_STATUS_OK;
        }
    }

    if(rc > 0) {
        rc = PLCTAG_STATUS_OK;
    } else {
        pdebug(DEBUG_WARN,"Unable to marshal data!");
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}


int unmarshal_eip_header(bytebuf_p buf, uint16_t *command, uint16_t *payload_size, uint32_t *session_handle, uint32_t *status, uint64_t *sender_context)
{
    int rc = PLCTAG_STATUS_OK;
    uint32_t options;

    pdebug(DEBUG_DETAIL,"Starting.");

    /* set this in case the unmarshalling fails. */
    *status = AB_EIP_OK;

    rc = bytebuf_unmarshal(buf ,
                            BB_U16, command,
                            BB_U16, payload_size,
                            BB_U32, session_handle,
                            BB_U32, status,
                            BB_U64, sender_context,
                            BB_U32, &options    /* throw away */
                            );

    if(rc > 0) {
        rc = PLCTAG_STATUS_OK;
    }

    if(*status != AB_EIP_OK) {
        pdebug(DEBUG_WARN,"Remote EIP error %d", status);
        return PLCTAG_ERR_REMOTE_ERR;
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}


int marshal_register_session(bytebuf_p buf, uint16_t eip_version, uint16_t option_flags)
{
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL,"Starting.");

    rc = bytebuf_marshal(buf,
                        BB_U16, eip_version,
                        BB_U16, option_flags
                        );

    if(rc > 0) {
        rc = PLCTAG_STATUS_OK;
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}





int marshal_cip_get_tag_info(bytebuf_p buf, uint32_t start_instance)
{
    /* packet format is as follows:
    uint8_t request_service    0x55
    uint8_t request_path_size  3 - 6 bytes
    uint8_t   0x20    get class
    uint8_t   0x6B    tag info/symbol class
    uint8_t   0x25    get instance (16-bit)
    uint8_t   0x00    padding
    uint8_t   0x00    instance byte 0
    uint8_t   0x00    instance byte 1
    uint16_t  0x02    number of attributes to get
    uint16_t  0x01    attribute #1 - symbol name
    uint16_t  0x02    attribute #2 - symbol type
    */

    int rc = PLCTAG_STATUS_OK;
    uint8_t req_path[] = {0x20, 0x6B, 0x25, 0x00};
    int first_pos = bytebuf_get_cursor(buf);

    rc = bytebuf_marshal(buf,
                        BB_U8, (int8_t)AB_CIP_CMD_GET_INSTANCE_ATTRIB_LIST, /* request service */
                        BB_U8, (uint8_t)(((sizeof(req_path)/sizeof(req_path[0])) + 4)/2),  /* should be 4 16-bit words */
                        BB_BYTES, req_path, (sizeof(req_path)/sizeof(req_path[0])),
                        BB_U16, (uint16_t)start_instance,
                        BB_U16, (uint16_t)3, /* get two attributes. */
                        BB_U16, 0x1, /* MAGIC - attribute #1 is the symbol name. */
                        BB_U16, 0x2,  /* MAGIC - attribute #2 is the symbol type. */
                        BB_U16, 0x3 /* test this. */
                       );

    if(rc > 0) {
        int last_pos = bytebuf_get_cursor(buf);

        rc = bytebuf_set_cursor(buf, first_pos);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to seek to start of new data!");
            return rc;
        }

        pdebug(DEBUG_DETAIL,"Created new packet data:");
        pdebug_dump_bytes(DEBUG_DETAIL, bytebuf_get_buffer(buf), last_pos - first_pos);

        rc = bytebuf_set_cursor(buf, last_pos);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to seek to end of new data!");
            return rc;
        }

        rc = PLCTAG_STATUS_OK;
    }

    return rc;
}






int marshal_cip_read(bytebuf_p buf, const char *name, int elem_count, int offset)
{
    int rc = PLCTAG_STATUS_OK;

    (void)offset;

    /* inject the read command byte */
    rc = bytebuf_marshal(buf, BB_U8, AB_CIP_READ);
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set CIP command to read in buffer!");
        return rc;
    }

    /* process the tag name */
    rc = cip_encode_tag_name(buf, name);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to encode tag name!  Check tag name format!");
        return rc;
    }

    /* set the element count to read. */
    rc = bytebuf_marshal(buf, BB_U16, (uint16_t)elem_count);
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set number of elements to read!");
        return rc;
    } else {
        int last_pos = bytebuf_get_cursor(buf);

        rc = bytebuf_set_cursor(buf, 0);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to seek to start of new data!");
            return rc;
        }

        pdebug(DEBUG_DETAIL,"Created new packet data:");
        pdebug_dump_bytes(DEBUG_DETAIL, bytebuf_get_buffer(buf), last_pos);

        rc = bytebuf_set_cursor(buf, last_pos);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to seek to end of new data!");
            return rc;
        }

        rc = PLCTAG_STATUS_OK;
    }

    return rc;
}


int unmarshal_cip_read(int prev_rc, bytebuf_p buf, bytebuf_p tag_buf, int offset)
{
    int rc = PLCTAG_STATUS_OK;
    uint8_t type_byte;
    uint8_t type_length_byte;

    if(prev_rc != PLCTAG_STATUS_OK) {
        return prev_rc;
    }

    /* eat the type info. */
    rc = bytebuf_unmarshal(buf, BB_U8, &type_byte);
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to get type byte!");
        return rc;
    }

    /* check for a simple/base type */
    if (type_byte >= AB_CIP_DATA_BIT && type_byte <= AB_CIP_DATA_STRINGI) {
        /* skip the type byte and length byte */
        rc = bytebuf_set_cursor(buf, bytebuf_get_cursor(buf) + 1);
    } else if (type_byte == AB_CIP_DATA_ABREV_STRUCT || type_byte == AB_CIP_DATA_ABREV_ARRAY ||
               type_byte == AB_CIP_DATA_FULL_STRUCT || type_byte == AB_CIP_DATA_FULL_ARRAY) {
        /* this is an aggregate type of some sort, the type info is variable length */

        /* get the type length byte */
        rc = bytebuf_unmarshal(buf, BB_U8, &type_length_byte);
        if(rc < PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to get type length byte!");
            return rc;
        }

        /* skip past the type data, does the type length include the type byte and length byte? */
        rc = bytebuf_set_cursor(buf, bytebuf_get_cursor(buf) + (int)type_length_byte);
    } else {
        pdebug(DEBUG_WARN, "Unsupported data type returned, type byte=%d", (int)type_byte);
        return PLCTAG_ERR_UNSUPPORTED;
    }

    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN, "Error seeking past type information!");
        return rc;
    }

    /* copy data from response into tag. */

    /* make sure there is space. */
    rc = bytebuf_set_cursor(tag_buf, offset + bytebuf_get_size(buf) - bytebuf_get_cursor(buf));
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN, "Error adding capacity/size to tag!");
        return rc;
    }

    /* set the cursor back to where we want to copy. */
    rc = bytebuf_set_cursor(tag_buf, offset);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN, "Error seeking to offset in tag!");
        return rc;
    }

    mem_copy(bytebuf_get_buffer(tag_buf), bytebuf_get_buffer(buf), bytebuf_get_size(buf) - bytebuf_get_cursor(buf));

    return rc;
}

int marshal_cip_write(bytebuf_p buf, const char *name, bytebuf_p tag_data)
{
    (void)buf;
    (void)name;
    (void)tag_data;

    return PLCTAG_ERR_NOT_IMPLEMENTED;
}


int unmarshal_cip_write(bytebuf_p buf, const char *name, bytebuf_p tag_data)
{
    (void)buf;
    (void)name;
    (void)tag_data;

    return PLCTAG_ERR_NOT_IMPLEMENTED;
}



int marshal_cip_cm_unconnected(int prev_rc, bytebuf_p buf, const char *ioi_path)
{
    int rc = PLCTAG_STATUS_OK;
    uint8_t mr_path[] = { 0x20, 0x06, 0x24, 0x01 }; /* path to CM */
    int cip_command_length = 0;

    /*

     The CM Unconnected Request packet looks like this:

              CM Service Request - Connection Manager
        uint8_t cm_service_code;         0x52 Unconnected Send
        uint8_t cm_req_path_size;        2, size in words of path, next field
        uint8_t cm_req_path[4];          0x20,0x06,0x24,0x01 for CM, instance 1

              Unconnected send
        uint8_t secs_per_tick;          seconds per tick
        uint8_t timeout_ticks;          timeout = src_secs_per_tick * src_timeout_ticks

        uint16_t uc_cmd_length;         length of embedded packet, in bytes

        ...CIP read/write request, embedded packet...

        ...IOI path to target device, connection IOI...
    */

    pdebug(DEBUG_INFO,"Starting.");

    /* abort if the wrapped call was not good. */
    if(prev_rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Aborting due to previous bad return code.");
        return prev_rc;
    }

    /* save this for later. */
    cip_command_length = bytebuf_get_size(buf);

    /* two calls, first gets size, second writes data. */
    for(int i=0; rc == PLCTAG_STATUS_OK && i < 2; i++) {
        rc = bytebuf_marshal(i == 0 ? NULL : buf ,
                             BB_U8, AB_CIP_CMD_UNCONNECTED_SEND,
                             BB_U8, (uint8_t)((sizeof(mr_path)/sizeof(mr_path[0])) / 2), /*length of path in 16-bit words*/
                             BB_BYTES, mr_path, (sizeof(mr_path)/sizeof(mr_path[0])),
                             BB_U8, (uint8_t)AB_CIP_SECS_PER_TICK,
                             BB_U8, (uint8_t)AB_CIP_TIMEOUT_TICKS,
                             BB_U16, (uint16_t)cip_command_length
                             );
        if(i == 0 && rc > 0) {
            /* make space at the beginning of the buffer. */
            pdebug(DEBUG_DETAIL,"Making %d bytes of space at the beginning for the header.", rc);
            rc = bytebuf_set_cursor(buf, - rc); /* rc contains the header size. */
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_WARN,"Error setting buffer cursor!");
                return rc;
            }

            rc = PLCTAG_STATUS_OK;
        }
    }

    if(rc > 0) {
        int last_pos = bytebuf_get_cursor(buf);

        rc = bytebuf_set_cursor(buf, 0);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to seek to start of data!");
            return rc;
        }

        pdebug(DEBUG_DETAIL,"Created new packet data:");
        pdebug_dump_bytes(DEBUG_DETAIL, bytebuf_get_buffer(buf), bytebuf_get_size(buf));

        rc = bytebuf_set_cursor(buf, last_pos);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to seek to end of new data!");
            return rc;
        }

        rc = PLCTAG_STATUS_OK;
    }

    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to marshal header!");
        return rc;
    }

    /* end of the header */

    /* seek to the end of the packet and add the path. */

    /* push the path to the PLC CPU at the end. */
    rc = bytebuf_set_cursor(buf, bytebuf_get_size(buf));
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to seek to end of packet!");
        return rc;
    }

    /* encode the path at the end. */
    rc = cip_encode_path(buf, ioi_path);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to add PLC IOI path to packet!");
        return rc;
    }

    if(rc == PLCTAG_STATUS_OK) {
        int last_pos = bytebuf_get_cursor(buf);

        rc = bytebuf_set_cursor(buf, 0);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to seek to start of data!");
            return rc;
        }

        pdebug(DEBUG_DETAIL,"Created new packet data:");
        pdebug_dump_bytes(DEBUG_DETAIL, bytebuf_get_buffer(buf), bytebuf_get_size(buf));

        rc = bytebuf_set_cursor(buf, last_pos);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to seek to end of new data!");
            return rc;
        }
    }

    pdebug(DEBUG_INFO,"Done.");

    return rc;
}



int unmarshal_cip_cm_unconnected(int prev_rc, bytebuf_p buf, uint8_t *reply_service, uint8_t *status, uint16_t *extended_status)
{
    int rc = PLCTAG_STATUS_OK;
    uint8_t dummy_byte = 0;
    uint8_t status_words = 0;

    pdebug(DEBUG_DETAIL,"Starting.");

    /*
    The CM Unconnected reply packet looks like this:

              CM unconnected Service Reply
    uint8_t reply_service;      Shows CMD | OK for OK.
    uint8_t reserved;           0x00 in reply
    uint8_t status;             0x00 for success, if not 0, then error status words hold
    uint8_t num_status_words;   number of 16-bit words in status, if status != 0.
    [uint16_t ...]              extra status words, usually the first one is important.

        ...CIP data, if any ...
    */

    /* abort if the wrapped call was not good. */
    if(prev_rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Aborting because previous unmarshalling failed.");
        return rc;
    }

    /* get the main part of the header. */
    rc = bytebuf_unmarshal(buf,
                     BB_U8, reply_service,
                     BB_U8, &dummy_byte,
                     BB_U8, status,
                     BB_U8, &status_words
                     );

    if(rc > 0) {
        rc = PLCTAG_STATUS_OK;
    }

    if(rc !=  PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to unmarshal reply header!");
        return rc;
    }

    /* check the status. */
    if(*status != AB_CIP_STATUS_OK && *status != AB_CIP_STATUS_FRAG) {
        /* get the extended status */
        rc = bytebuf_unmarshal(buf, BB_U16,extended_status);
        if(rc > 0) {
            rc = PLCTAG_STATUS_OK;
        }

        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to get reply extended status!");
            return rc;
        }

        pdebug(DEBUG_WARN,"Response has error! %s (%s)", decode_cip_error(*status, *extended_status, 1), decode_cip_error(*status, *extended_status, 0));

       /* abort processing. */
        return PLCTAG_ERR_REMOTE_ERR;
    } else {
        *extended_status = 0;
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}



int marshal_cip_cfp_unconnected(int prev_rc, bytebuf_p buf)
{
    int rc = PLCTAG_STATUS_OK;
    int payload_length = 0;

    /*
        CPF header looks like this:

              Interface Handle etc.
        uint32_t interface_handle;      ALWAYS 0
        uint16_t router_timeout;        Usually set to a few seconds.

              Common Packet Format - CPF Unconnected
        uint16_t cpf_item_count;        ALWAYS 2
        uint16_t cpf_nai_item_type;     ALWAYS 0
        uint16_t cpf_nai_item_length;   ALWAYS 0
        uint16_t cpf_udi_item_type;     ALWAYS 0x00B2 - Unconnected Data Item
        uint16_t cpf_udi_item_length;   REQ: fill in with length of remaining data.

        ... payload ...
    */

    pdebug(DEBUG_INFO,"Starting.");

    /* punt if wrapped call failed. */
    if(prev_rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Aborting due to previous bad return code.");
        return prev_rc;
    }

    /* get the payload size before we change the buffer. */
    payload_length = bytebuf_get_size(buf);

   /* two calls, first gets size, second writes data. */
    for(int i=0; rc == PLCTAG_STATUS_OK && i < 2; i++) {
        rc = bytebuf_marshal(i == 0 ? NULL : buf ,
                             BB_U32, 0,     /* interface handle, apparently zero */
                             BB_U16, (uint16_t)AB_DEFAULT_ROUTER_TIMEOUT_SECS,
                             BB_U16, 2,     /* item count */
                             BB_U16, (uint16_t)AB_CIP_ITEM_NAI,
                             BB_U16, (uint16_t)0,   /* NULL address length is zero */
                             BB_U16, (uint16_t)AB_CIP_ITEM_UDI,
                             BB_U16, (uint16_t)payload_length
                             );
        if(i == 0 && rc > 0) {
            /* make space at the beginning of the buffer. */
            pdebug(DEBUG_DETAIL,"Making %d bytes of space at the beginning for the header.", rc);
            rc = bytebuf_set_cursor(buf, - rc); /* rc contains the header size. */
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_WARN,"Error setting buffer cursor!");
                return rc;
            }

            rc = PLCTAG_STATUS_OK;
        }
    }

    if(rc > 0) {
        int last_pos = bytebuf_get_cursor(buf);

        rc = bytebuf_set_cursor(buf, 0);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to seek to start of data!");
            return rc;
        }

        pdebug(DEBUG_DETAIL,"Created new packet data:");
        pdebug_dump_bytes(DEBUG_DETAIL, bytebuf_get_buffer(buf), bytebuf_get_size(buf));

        rc = bytebuf_set_cursor(buf, last_pos);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to seek to end of new data!");
            return rc;
        }

        rc = PLCTAG_STATUS_OK;
    }

    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to marshal header!");
        return rc;
    }

    pdebug(DEBUG_INFO,"Done.");

    return rc;
}


int unmarshal_cip_cfp_unconnected(int prev_rc, bytebuf_p buf)
{
    int rc = PLCTAG_STATUS_OK;
    uint32_t interface_handle;
    uint16_t router_timeout;
    uint16_t item_count;
    uint16_t item_nai;
    uint16_t item_nai_length;
    uint16_t item_udi;
    uint16_t payload_length;

    /* The CPF header on a reply looks like this:
        Interface Handle etc.
    uint32_t interface_handle;
    uint16_t router_timeout;

        Common Packet Format - CPF Unconnected
    uint16_t cpf_item_count;         ALWAYS 2
    uint16_t cpf_nai_item_type;      ALWAYS 0
    uint16_t cpf_nai_item_length;    ALWAYS 0
    uint16_t cpf_udi_item_type;      ALWAYS 0x00B2 - Unconnected Data Item
    uint16_t cpf_udi_item_length;    data length

    ... payload ...
    */

    pdebug(DEBUG_DETAIL, "Starting.");

    /* abort if the wrapped function failed. */
    if(prev_rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Aborting because previous call failed.");
        return prev_rc;
    }

    rc = bytebuf_unmarshal(buf,
                     BB_U32, &interface_handle,
                     BB_U16, &router_timeout,
                     BB_U16, &item_count,     /* item count, FIXME - should check this! */
                     BB_U16, &item_nai,
                     BB_U16, &item_nai_length,
                     BB_U16, &item_udi,
                     BB_U16, &payload_length
                     );

    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to marshal header!");
        return rc;
    }

    pdebug(DEBUG_INFO,"Done.");

    return PLCTAG_STATUS_OK;
}








int send_eip_packet(sock_p sock, bytebuf_p payload)
{
    int rc = PLCTAG_STATUS_OK;
    int offset = bytebuf_get_cursor(payload);

    pdebug(DEBUG_DETAIL,"Starting.");

    pdebug(DEBUG_DETAIL,"Current cursor position: %d", bytebuf_get_cursor(payload));

    pdebug(DEBUG_DETAIL,"Sending packet:");
    pdebug_dump_bytes(DEBUG_DETAIL,bytebuf_get_buffer(payload), bytebuf_get_size(payload));

    /* send the data. */
    rc = socket_write(sock, bytebuf_get_buffer(payload), bytebuf_get_size(payload) - offset);
    if(rc < 0) {
        pdebug(DEBUG_WARN,"Error writing data to socket!");
        return rc;
    }

    rc = bytebuf_set_cursor(payload, offset + rc);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error setting buffer cursor!");
        return rc;
    }

    if(bytebuf_get_cursor(payload) == bytebuf_get_size(payload)) {
        rc = PLCTAG_STATUS_OK;
    } else {
        rc = PLCTAG_STATUS_PENDING;
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}

/*
 * FIXME - refactor this turkey so that it does not take multiple tries to
 * get a packet.
 */

int receive_eip_packet(sock_p sock, bytebuf_p buf)
{
    int rc = PLCTAG_STATUS_PENDING;
    int total_read = 0;
    int data_needed = EIP_ENCAP_SIZE;
    int got_header = 0;

    pdebug(DEBUG_SPEW,"Starting.");

    /* how much have we read so far? */
    total_read = bytebuf_get_size(buf);

    /* if we got the header, then we can determine how much more we need to get.*/
    if(total_read >= EIP_ENCAP_SIZE) {
        uint16_t command;
        uint16_t length;
        uint32_t session_handle;
        uint32_t status;
        uint64_t sender_context;

        got_header = 1;

        /* get the header info. */

        /* set the cursor back to the start. */
        rc = bytebuf_set_cursor(buf, 0);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to set cursor!");
            return rc;
        }

        pdebug(DEBUG_DETAIL,"Received partial packet with header:");
        pdebug_dump_bytes(DEBUG_DETAIL, bytebuf_get_buffer(buf), bytebuf_get_size(buf));

        /* parse the header. */
        rc = unmarshal_eip_header(buf, &command, &length, &session_handle, &status, &sender_context);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Error parsing EIP encapsulation header!");
            return rc;
        }

        data_needed = EIP_ENCAP_SIZE + (int)length;

        pdebug(DEBUG_DETAIL,"Got header, size of payload = %d", data_needed);

        if(length == 0) {
            rc = PLCTAG_STATUS_OK;
        }
    } else {
        data_needed = EIP_ENCAP_SIZE;
    }

    /* ensure that the byte buffer has sufficient capacity. */
    rc = bytebuf_set_capacity(buf, data_needed);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set cursor!");
        return rc;
    }

    /* set the cursor to the end of the data. */
    rc = bytebuf_set_cursor(buf, total_read);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set cursor!");
        return rc;
    }

    rc = socket_read(sock, bytebuf_get_buffer(buf),
                     data_needed - total_read);

    /* was there an error? */
    if (rc < 0) {
        if (rc != PLCTAG_ERR_NO_DATA) {
            /* error! */
            pdebug(DEBUG_WARN,"Error reading socket! rc=%d",rc);
            return rc;
        }

        return PLCTAG_STATUS_PENDING;
    }

    /* we got data or at least no error. */
    total_read += rc;

    pdebug(DEBUG_DETAIL,"Got %d more bytes of data.", rc);

    /* set the cursor to the end of the data to set the size. */
    rc = bytebuf_set_cursor(buf, total_read);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set cursor!");
        return rc;
    }

    /* set the cursor back to zero to allow for any decoding. */
    rc = bytebuf_set_cursor(buf, 0);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set cursor!");
        return rc;
    }

    /* still getting data. */
    if(got_header && total_read == data_needed) {
        pdebug(DEBUG_DETAIL,"Received complete packet:");
        pdebug_dump_bytes(DEBUG_DETAIL, bytebuf_get_buffer(buf), bytebuf_get_size(buf));

        rc = PLCTAG_STATUS_OK;
    } else {
        rc = PLCTAG_STATUS_PENDING;
    }

    return rc;
}




/***********************************************************************
 ************************ Helper Functions *****************************
 **********************************************************************/



int cip_encode_path(bytebuf_p buf, const char *ioi_path)
{
    int ioi_size=0;
    int ioi_length_index = 0;
    int link_index=0;
    char **links=NULL;
    char *link=NULL;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL,"Starting.");

    if(!ioi_path || str_length(ioi_path) == 0) {
        pdebug(DEBUG_DETAIL,"Path is null or zero length.");
        return PLCTAG_STATUS_OK;
    }

    /* split the path */
    links = str_split(ioi_path,",");

    if(!links) {
        pdebug(DEBUG_WARN,"Unable to create split string!");
        return PLCTAG_ERR_NO_MEM;
    }

    /* get the index of the IOI length. */
    ioi_length_index = bytebuf_get_cursor(buf);
    rc = bytebuf_marshal(buf, BB_U16, 0); /* place holder */
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to inject place hodler for IOI length!");
        mem_free(links);
        return rc;
    }

    /* start moving along the link entries */
    link = links[link_index];
    while(link) {
        int tmp = 0;

        rc = str_to_int(link, &tmp);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Bad IOI path format to PLC!  Expected number, got %s", link);
            mem_free(links);
            return PLCTAG_ERR_BAD_PARAM;
        }

        rc = bytebuf_marshal(buf, BB_U8, (uint8_t)tmp);
        if(rc < PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to inject new path element!");
            mem_free(links);
            return rc;
        }

        ioi_size++;
        link_index++;
        link = links[link_index];
    }

    /* done with the split string */
    mem_free(links);

    /* pad out the length */
    if(ioi_size & 0x01) {
        rc = bytebuf_marshal(buf, BB_U8, 0); /* place holder */
        if(rc < PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to marshal padding byte!");
            return rc;
        }

        ioi_size++;
    }

    /* set the length */
    rc = bytebuf_set_cursor(buf, ioi_length_index);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set buffer cursor to ioi_length location!");
        return rc;
    }

    /* set the length.   NOTE: in 16-bit words! */
    rc = bytebuf_marshal(buf, BB_U16, (uint8_t)(ioi_size/2));
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Bad IOI path format to PLC!");
        return rc;
    }

    /* seek to the end of the buffer. */
    rc = bytebuf_set_cursor(buf, bytebuf_get_size(buf));
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set buffer cursor to end of buffer!");
        return rc;
    }

    return rc;
}




#ifdef START
#undef START
#endif
#define START 1

#ifdef ARRAY
#undef ARRAY
#endif
#define ARRAY 2

#ifdef DOT
#undef DOT
#endif
#define DOT 3

#ifdef NAME
#undef NAME
#endif
#define NAME 4

/*
 * cip_encode_tag_name()
 *
 * This takes a LGX-style tag name like foo[14].blah and
 * turns it into an IOI path/string.
 */

int cip_encode_tag_name(bytebuf_p buf, const char *name)
{
    const char *p = name;
    int word_count_index = 0;
    int word_count = 0;
    int symbol_len_index = 0;
    int symbol_len;
    int tmp_index = 0;
    int state;
    int rc = PLCTAG_STATUS_OK;

    /* point to location of word count. */
    word_count_index = bytebuf_get_cursor(buf);

    /* inject a place holder for the IOI string word count */
    rc = bytebuf_marshal(buf, BB_U8, 0);
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to inject placeholder for IOI size count!");
        return rc;
    }

    state = START;

    while(*p) {
        switch(state) {
            case START:

                /* must start with an alpha character or _ or :. */
                if(isalpha(*p) || *p == '_' || *p == ':') {
                    state = NAME;
                } else if(*p == '.') {
                    state = DOT;
                } else if(*p == '[') {
                    state = ARRAY;
                } else {
                    pdebug(DEBUG_WARN,"Bad path!  Check Path format.");
                    return PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case NAME:
                rc = bytebuf_marshal(buf, BB_U8, (uint8_t)0x91); /* MAGIC - symbolic segment. */
                if(rc < PLCTAG_STATUS_OK) {
                    pdebug(DEBUG_WARN,"Unable to inject symbol start!");
                    return rc;
                }

                /* inject place holder for the name length */
                symbol_len_index = bytebuf_get_cursor(buf);
                rc = bytebuf_marshal(buf, BB_U8, 0);
                if(rc < PLCTAG_STATUS_OK) {
                    pdebug(DEBUG_WARN,"Unable to inject symbol segment length byte!");
                    return rc;
                }

                while(isalnum(*p) || *p == '_' || *p == ':') {
                    rc = bytebuf_marshal(buf, BB_U8, (uint8_t)*p);
                    if(rc < PLCTAG_STATUS_OK) {
                        pdebug(DEBUG_WARN,"Unable to inject symbol character!");
                        return rc;
                    }

                    p++;
                    symbol_len++;
                }

                /* must pad the name to a multiple of two bytes */
                if(symbol_len & 0x01) {
                    rc = bytebuf_marshal(buf, BB_U8, 0);
                    if(rc < PLCTAG_STATUS_OK) {
                        pdebug(DEBUG_WARN,"Unable to inject symbol padding!");
                        return rc;
                    }
                }

                /* write the length back in. */
                tmp_index = bytebuf_get_cursor(buf);

                rc = bytebuf_set_cursor(buf, symbol_len_index);
                if(rc != PLCTAG_STATUS_OK) {
                    pdebug(DEBUG_WARN,"Unable to set buffer cursor to write symbolic segment length byte!");
                    return rc;
                }

                rc = bytebuf_marshal(buf, BB_U8, (uint8_t)symbol_len);
                if(rc < PLCTAG_STATUS_OK) {
                    pdebug(DEBUG_WARN,"Unable to set buffer cursor to write symbolic segment length byte!");
                    return rc;
                }

                /* return the cursor to the end of the buffer. */
                rc = bytebuf_set_cursor(buf, tmp_index);
                if(rc != PLCTAG_STATUS_OK) {
                    pdebug(DEBUG_WARN,"Unable to set buffer cursor to write symbolic segment length byte!");
                    return rc;
                }

                state = START;

                break;

            case ARRAY:
                /* move the pointer past the [ character */
                p++;

                do {
                    uint32_t val;
                    char *np = NULL;
                    val = (uint32_t)strtol(p,&np,0);

                    if(np == p) {
                        /* we must have a number */
                        pdebug(DEBUG_WARN,"Bad path format.  Must have number at position %d", p - name);
                        return PLCTAG_ERR_BAD_PARAM;
                    }

                    p = np;

                    if(val > 0xFFFF) {
                        rc = bytebuf_marshal(buf, BB_U8, 0x2A); /* MAGIC - numeric segment of 4 bytes */
                        if(rc < PLCTAG_STATUS_OK) {
                            pdebug(DEBUG_WARN,"Unable to inject numeric segment type byte!");
                            return rc;
                        }

                        /* inject a padding byte. */
                        rc = bytebuf_marshal(buf, BB_U8, 0); /* pad to even number of bytes. */
                        if(rc < PLCTAG_STATUS_OK) {
                            pdebug(DEBUG_WARN,"Unable to inject numeric segment type padding byte!");
                            return rc;
                        }

                        rc = bytebuf_marshal(buf, BB_U32, val);
                        if(rc < PLCTAG_STATUS_OK) {
                            pdebug(DEBUG_WARN,"Unable to inject numeric segment value!");
                            return rc;
                        } else {
                            rc = PLCTAG_STATUS_OK;
                        }
                    } else if(val > 0xFF) {
                        rc = bytebuf_marshal(buf, BB_U8, 0x29); /* MAGIC - numeric segment of 2 bytes */
                        if(rc < PLCTAG_STATUS_OK) {
                            pdebug(DEBUG_WARN,"Unable to inject numeric segment type byte!");
                            return rc;
                        }

                        /* inject a padding byte. */
                        rc = bytebuf_marshal(buf, BB_U8, 0); /* pad to even number of bytes. */
                        if(rc < PLCTAG_STATUS_OK) {
                            pdebug(DEBUG_WARN,"Unable to inject numeric segment type padding byte!");
                            return rc;
                        }

                        rc = bytebuf_marshal(buf, BB_U16, val);
                        if(rc < PLCTAG_STATUS_OK) {
                            pdebug(DEBUG_WARN,"Unable to inject numeric segment value!");
                            return rc;
                        } else {
                            rc = PLCTAG_STATUS_OK;
                        }
                    } else {
                        rc = bytebuf_marshal(buf, BB_U8, 0x28); /* MAGIC - numeric segment of 1 byte */
                        if(rc < PLCTAG_STATUS_OK) {
                            pdebug(DEBUG_WARN,"Unable to inject numeric segment type byte!");
                            return rc;
                        }

                        rc = bytebuf_marshal(buf, BB_U8, val);
                        if(rc < PLCTAG_STATUS_OK) {
                            pdebug(DEBUG_WARN,"Unable to inject numeric segment value!");
                            return rc;
                        } else {
                            rc = PLCTAG_STATUS_OK;
                        }
                    }

                    /* eat up whitespace */
                    while(isspace(*p)) p++;
                } while(*p == ',');

                if(*p != ']')
                    pdebug(DEBUG_WARN,"Incorrect array format, must have closing ] character.");
                    return PLCTAG_ERR_BAD_PARAM;

                p++;

                state = START;

                break;

            case DOT:
                p++;
                state = START;
                break;

            default:
                /* this should never happen */
                pdebug(DEBUG_WARN,"Bad state machine state %d!", state);
                return PLCTAG_ERR_BAD_PARAM;

                break;
        }
    }

    /*
     * word_count is in units of 16-bit integers, do not
     * count the word_count value itself.
     */
    //*word_count = (uint8_t)((dp - data)-1)/2;
    word_count = (bytebuf_get_cursor(buf) - word_count_index - 1) / 2;

    tmp_index = bytebuf_get_cursor(buf);

    rc = bytebuf_set_cursor(buf, word_count_index);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set cursor back to word count byte!");
        return rc;
    }

    rc = bytebuf_marshal(buf, BB_U8, (uint8_t)word_count);
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set word count byte!");
        return rc;
    }

    rc = bytebuf_set_cursor(buf, tmp_index);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set cursor end of encoded name!");
        return rc;
    }

    return PLCTAG_STATUS_OK;
}
