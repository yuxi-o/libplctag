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





static int cip_encode_path(bytebuf_p buf, const char *ioi_path, bytebuf_arg_type count_type);
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


int marshal_forward_open_request(bytebuf_p buf, const char *plc_path, uint32_t connection_id, uint16_t connection_serial_num, uint16_t conn_params)
{
    int rc = PLCTAG_STATUS_OK;
    int first_pos = 0;
    int last_pos = 0;
    uint8_t cm_path[] = {0x20, 0x06, 0x24, 0x01}; /* path to CM */
    const char *mr_path = ",32,2,36,1";
    char *ioi_path = NULL;

    pdebug(DEBUG_INFO,"Starting.");

    /*

      We use part of the CM header.  In AB's infinite wisdom they do not use
      exactly the same format for this (no payload size) as for other CIP
      commands.

    uint8_t cm_service_code;        0x54 Forward Open
    uint8_t cm_req_path_size;       ALWAYS 2, size in words of path, next field
    uint8_t cm_req_path[4];         ALWAYS 0x20,0x06,0x24,0x01 for CM, instance 1

      Unconnected send
    uint8_t secs_per_tick;          seconds per tick
    uint8_t timeout_ticks;          timeout = src_secs_per_tick * src_timeout_ticks

     Forward Open Params
    uint32_t orig_to_targ_conn_id;   0, returned by target in reply.
    uint32_t targ_to_orig_conn_id;   what is _our_ ID for this connection, use ab_connection ptr as id ?
    uint16_t conn_serial_number;     our connection serial number ??
    uint16_t orig_vendor_id;         our unique vendor ID
    uint32_t orig_serial_number;     our unique serial number
    uint8_t conn_timeout_multiplier; timeout = mult * RPI
    uint8_t reserved[3];             reserved, set to 0
    uint32_t orig_to_targ_rpi;       us to target RPI - Request Packet Interval in microseconds
    uint16_t orig_to_targ_conn_params;  some sort of identifier of what kind of PLC we are???
    uint32_t targ_to_orig_rpi;       target to us RPI, in microseconds
    uint16_t targ_to_orig_conn_params;  some sort of identifier of what kind of PLC the target is ???
    uint8_t transport_class;         ALWAYS 0xA3, server transport, class 3, application trigger

    uint8_t path_size;               size of connection path in 16-bit words
    uint8_t path[];                  * connection path f
                                     * Example: LGX with 1756-ENBT and CPU in slot 0 would be:
                                     * 0x01 - backplane port of 1756-ENBT
                                     * 0x00 - slot 0 for CPU
                                     * 0x20 - class
                                     * 0x02 - MR Message Router
                                     * 0x24 - instance
                                     * 0x01 - instance #1.
    */

    first_pos = bytebuf_get_cursor(buf);

    /* make the combined IOI path to the MR */
    ioi_path = str_concat(plc_path, mr_path);
    if(!ioi_path) {
        pdebug(DEBUG_WARN,"Unable to contatenate paths for IOI path!");
        return PLCTAG_ERR_NO_MEM;
    }

    rc = bytebuf_marshal(buf,
                        BB_U8, AB_CIP_CMD_FORWARD_OPEN,
                        BB_U8, (uint8_t)((sizeof(cm_path)/sizeof(cm_path[0])) / 2), /*length of path in 16-bit words*/
                        BB_BYTES, cm_path, (sizeof(cm_path)/sizeof(cm_path[0])),
                        BB_U8, (uint8_t)AB_CIP_SECS_PER_TICK,
                        BB_U8, (uint8_t)AB_CIP_TIMEOUT_TICKS,
                        BB_U32, 0,                           /* MAGIC, zero for the target's connection ID.  This will be returned. */
                        BB_U32, connection_id,               /* Our connection ID for this connection. */
                        BB_U16, connection_serial_num,      /* our connection serial number? */
                        BB_U16, AB_CIP_VENDOR_ID,            /* our vendor ID, completely fake. */
                        BB_U32, AB_CIP_VENDOR_SN,            /* our vendor serial number, again, completely fake. */
                        BB_U8, AB_CIP_TIMEOUT_MULTIPLIER,    /* timeout = multiplier * RPI.  Ignored on Type 3 connections? */
                        BB_U8, 0,                            /* reserved */
                        BB_U8, 0,                            /* reserved */
                        BB_U8, 0,                            /* reserved */
                        BB_U32, AB_CIP_RPI,                  /* us to target RPI - requested packet interval in microseconds. */
                        BB_U16, conn_params,                  /* defines some things (what?) about the connection. Lower 9 bits are max payload size? */
                        BB_U32, AB_CIP_RPI,                  /* target to us RPI - not really used on T3 connections? */
                        BB_U16, conn_params,                  /* defines some things about the connection.   Lower 9 bits are max payload size? */
                        BB_U8, AB_CIP_TRANSPORT_CLASS_T3     /* server transport, class 3 (TCP, explicit messaging), application trigger. */
                        );

    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to marshal forward open request!");
        mem_free(ioi_path);
        return rc;
    }

    /* encode the path at the end. */
    rc = cip_encode_path(buf, ioi_path, BB_U8);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to add PLC IOI path to packet!");
        mem_free(ioi_path);
        return rc;
    }

    /* we do not need the ioi_path anymore. */
    mem_free(ioi_path);

    /* save where we are in order to print out the packet. */
    last_pos = bytebuf_get_cursor(buf);

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


    pdebug(DEBUG_INFO,"Done.");

    return PLCTAG_STATUS_OK;
}



int unmarshal_forward_open_response(bytebuf_p buf, uint8_t *cip_status, uint16_t *cip_extended_status, uint32_t *to_connection_id, uint32_t *ot_connection_id)
{
    int rc = PLCTAG_STATUS_OK;
    uint16_t conn_serial_number = 0;
    uint16_t orig_vendor_id = 0;
    uint32_t orig_serial_number = 0;
    uint32_t orig_to_targ_api = 0;
    uint32_t targ_to_orig_api = 0;
    uint8_t app_data_size = 0;
    uint8_t reserved = 0;
    uint8_t reply_service = 0;
    uint8_t dummy_byte = 0;
    uint8_t status_words = 0;

    /*
        CM reply header
    uint8_t reply_service;      Shows CMD | OK for OK.
    uint8_t reserved;           0x00 in reply
    uint8_t status;             0x00 for success, if not 0, then error status words hold
    uint8_t num_status_words;   number of 16-bit words in status, if status != 0.
    [uint16_t ...]              extra status words, usually the first one is important.

        Forward Open response
    uint32_t orig_to_targ_conn_id;   target's connection ID for us, save this.
    uint32_t targ_to_orig_conn_id;   our connection ID back for reference
    uint16_t conn_serial_number;     our connection serial number from request
    uint16_t orig_vendor_id;         our unique vendor ID from request
    uint32_t orig_serial_number;     our unique serial number from request
    uint32_t orig_to_targ_api;       Actual packet interval, microsecs
    uint32_t targ_to_orig_api;       Actual packet interval, microsecs
    uint8_t app_data_size;           size in 16-bit words of send_data at end
    uint8_t reserved;
    */

    rc = bytebuf_unmarshal(buf,
                            BB_U8, &reply_service,
                            BB_U8, &dummy_byte,
                            BB_U8, cip_status,
                            BB_U8, &status_words
                           );
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to unmarshal forward open CM response!");
        return rc;
    }

    /* check the status. */
    if(*cip_status != AB_CIP_STATUS_OK) {
        /* get the extended status */
        rc = bytebuf_unmarshal(buf, BB_U16, cip_extended_status);
        if(rc > 0) {
            rc = PLCTAG_STATUS_OK;
        }

        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to get reply extended status!");
            return rc;
        }

        pdebug(DEBUG_WARN,"Response has error! %s (%s)", decode_cip_error(*cip_status, *cip_extended_status, 1), decode_cip_error(*cip_status, *cip_extended_status, 0));

       /* abort processing. */
        return PLCTAG_ERR_REMOTE_ERR;
    } else {
        *cip_extended_status = 0;
    }

    rc = bytebuf_unmarshal(buf,
                            BB_U32, to_connection_id,
                            BB_U32, ot_connection_id,
                            BB_U16, &conn_serial_number,
                            BB_U16, &orig_vendor_id,
                            BB_U32, &orig_serial_number,
                            BB_U32, &orig_to_targ_api,
                            BB_U32, &targ_to_orig_api,
                            BB_U8,  &app_data_size,
                            BB_U8,  &reserved
                           );

    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to unmarshal forward open response!");
        return rc;
    }

    return PLCTAG_STATUS_OK;
}



int marshal_cip_get_tag_info(bytebuf_p buf, uint32_t start_instance)
{
    /* packet format is as follows:
       CIP Tag Info command
    uint8_t request_service    0x55
    uint8_t request_path_size  3 - 6 bytes
    uint8_t   0x20    get class
    uint8_t   0x6B    tag info/symbol class
    uint8_t   0x25    get instance (16-bit)
    uint8_t   0x00    padding
    uint8_t   0x00    instance byte 0
    uint8_t   0x00    instance byte 1
    uint16_t  0x04    number of attributes to get
    uint16_t  0x02    attribute #2 - symbol type
    uint16_t  0x07    attribute #7 - base type size (array element) in bytes
    uint16_t  0x08    attribute #8 - array dimensions (3xu32)
    uint16_t  0x01    attribute #1 - symbol name
    */

    int rc = PLCTAG_STATUS_OK;
    uint8_t req_path[] = {0x20, 0x6B, 0x25, 0x00};
    int first_pos = bytebuf_get_cursor(buf);
    int last_pos = 0;

    rc = bytebuf_marshal(buf,
                        BB_U8, (int8_t)AB_CIP_CMD_GET_INSTANCE_ATTRIB_LIST, /* request service */
                        BB_U8, (uint8_t)(((sizeof(req_path)/sizeof(req_path[0])) + 2)/2),  /* should be 3 16-bit words */
                        BB_BYTES, req_path, (sizeof(req_path)/sizeof(req_path[0])),
                        BB_U16, (uint16_t)start_instance,
                        BB_U16, (uint16_t)4, /* get four attributes. */
                        BB_U16, 0x2,  /* MAGIC - attribute #2 is the symbol type. */
                        BB_U16, 0x7,  /* MAGIC - byte count, size of one tag element in bytes. */
                        BB_U16, 0x8,   /* array dimensions */
                        BB_U16, 0x1    /* MAGIC - attribute #1 is the symbol name. */
                       );
    if(rc < 0) {
        pdebug(DEBUG_WARN,"Unable to marshal packet!");
        return rc;
    }

    last_pos = bytebuf_get_cursor(buf);

    //~ packet_size = last_pos - packet_size_index - 2; /* remove two for the packet size itself */

    //~ rc = bytebuf_set_cursor(buf, packet_size_index);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Unable to seek cursor to packet size field!");
        //~ return rc;
    //~ }

    //~ rc = bytebuf_marshal(buf, BB_U16, packet_size);
    //~ if(rc < 0) {
        //~ pdebug(DEBUG_WARN,"Unable to marshal packet size field!");
        //~ return rc;
    //~ }

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

    return rc;
}






int marshal_cip_read(bytebuf_p buf, const char *name, int elem_count, int offset)
{
    int rc = PLCTAG_STATUS_OK;
    int first_pos = 0;
    int last_pos = 0;

    /*
      packet has the following format:
    u8 - CIP read command
    u8[] - tag name, encoded
    u16 - element count
    u32 - byte offset
    */

    /* get the current position for later reset of the packet size. */
    first_pos = bytebuf_get_cursor(buf);

    /* inject the size placeholder and read command byte */
    rc = bytebuf_marshal(buf, BB_U8, AB_CIP_READ_FRAG);
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
    rc = bytebuf_marshal(buf, BB_U16, (uint16_t)elem_count, BB_U32, (uint32_t)offset);
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set number of elements to read or offset!");
        return rc;
    }

    last_pos = bytebuf_get_cursor(buf);

    /* print out the packet */
    rc = bytebuf_set_cursor(buf, first_pos);
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

    return rc;
}


int unmarshal_cip_read(int prev_rc, bytebuf_p buf, int *type_info_index, int *type_info_length)
{
    int rc = PLCTAG_STATUS_OK;
    uint8_t type_byte;
    uint8_t type_length_byte;

    if(prev_rc != PLCTAG_STATUS_OK) {
        return prev_rc;
    }

    *type_info_index = bytebuf_get_cursor(buf);
    *type_info_length = 1;

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
        *type_info_length += 1;
    } else if (type_byte == AB_CIP_DATA_ABREV_STRUCT || type_byte == AB_CIP_DATA_ABREV_ARRAY ||
               type_byte == AB_CIP_DATA_FULL_STRUCT || type_byte == AB_CIP_DATA_FULL_ARRAY) {
        /* this is an aggregate type of some sort, the type info is variable length */

        /* get the type length byte */
        rc = bytebuf_unmarshal(buf, BB_U8, &type_length_byte);
        if(rc < PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to get type length byte!");
            return rc;
        }

        *type_info_length += type_length_byte + 1; /* + 1 for the length byte itself */

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

    return rc;
}

int marshal_cip_write(bytebuf_p buf, const char *name, uint8_t *type_info, int type_info_length, int elem_count, int *offset, int max_command_size, bytebuf_p tag_data)
{
    int rc = PLCTAG_STATUS_OK;
    int command_pos = 0;
    int last_pos = 0;
    int remainder = 0;

    /*
      packet has the following format:
    u8 - CIP write command
    u8[] - tag name, encoded
    u8[] - type data
    u16 - element count
    u32 - byte offset (if the tag has enough data)
    u8[] tag data
    */


    /*
     * We need to construct the packet a bit speculatively.
     * We try to write the data as if it is going to fit into
     * the max command size.   If it does, we continue.
     * If it does not, then we patch up the CIP command to
     * use the fragmented write command and add the offset before
     * the data.
     */

    /* get the current position for later reset of the packet size. */
    command_pos = bytebuf_get_cursor(buf);

    /* inject the size placeholder and read command byte */
    rc = bytebuf_marshal(buf, BB_U8, AB_CIP_WRITE);
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set CIP command to write in buffer!");
        return rc;
    }

    /* process the tag name */
    rc = cip_encode_tag_name(buf, name);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to encode tag name!  Check tag name format!");
        return rc;
    }

    /* set the type data and the element count to write. */
    rc = bytebuf_marshal(buf, BB_BYTES, type_info, type_info_length, BB_U16, (uint16_t)elem_count);
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to set type info or number of elements to write!");
        return rc;
    }

    /* now check what we have left. If there isn't enough space, we fragment. */
    if((max_command_size - bytebuf_get_size(buf)) < bytebuf_get_size(tag_data)) {
        last_pos = bytebuf_get_cursor(buf);

        /* seek to the command */
        rc = bytebuf_set_cursor(buf, command_pos);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to seek to start of new data!");
            return rc;
        }

        /* set the correct command */
        rc = bytebuf_marshal(buf, BB_U8, AB_CIP_WRITE_FRAG);
        if(rc < PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to set CIP command to write (fragmented) in buffer!");
            return rc;
        }

        /* seek to the end */
        rc = bytebuf_set_cursor(buf, last_pos);
        if(rc != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to seek to end of new data!");
            return rc;
        }

        /* set the offset. */
        rc = bytebuf_marshal(buf, BB_U32, (uint32_t)*offset);
        if(rc < PLCTAG_STATUS_OK) {
            pdebug(DEBUG_WARN,"Unable to set number of elements to read or offset!");
            return rc;
        }

        last_pos = bytebuf_get_cursor(buf);
    }

    /*
     * now copy the data.
     *
     * First, figure out how much we can copy.   Then make sure that it is
     * a multiple of 4 bytes in length.
     */

    remainder = max_command_size - bytebuf_get_size(buf);
    remainder = remainder & 0xFFFFFC; /* round down to nearest multiple of 4 */

    /*
     * if there is less left than we can fit in the remaining space, then adjust
     * the remainder.
     */
    if(remainder > (bytebuf_get_size(tag_data) -  *offset)) {
        remainder = (bytebuf_get_size(tag_data) -  *offset);
    }

    /* expand the size of the buffer */
    //~ rc = bytebuf_set_capacity(buf, bytebuf_get_size(buf) + remainder);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Unable to set capacity for tag data space!");
        //~ return rc;
    //~ }

    pdebug(DEBUG_DETAIL,"buf size=%d", bytebuf_get_size(buf));
    pdebug(DEBUG_DETAIL,"buf cursor=%d", bytebuf_get_cursor(buf));
    pdebug(DEBUG_DETAIL,"tag buf size=%d", bytebuf_get_size(tag_data));
    pdebug(DEBUG_DETAIL,"tag buf cursor=%d", bytebuf_get_cursor(tag_data));
    pdebug(DEBUG_DETAIL,"remainder=%d", remainder);
    pdebug(DEBUG_DETAIL,"offset=%d", *offset);



    /* copy the data */
    //~ mem_copy(bytebuf_get_buffer(buf), bytebuf_get_buffer(tag_data) + *offset, remainder);

    rc = bytebuf_marshal(buf, BB_BYTES, bytebuf_get_buffer(tag_data) + *offset, remainder);
    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to marshal tag write data!");
        return rc;
    }

    *offset += remainder;

    /* print out the packet */
    rc = bytebuf_set_cursor(buf, command_pos);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to seek to start of new data!");
        return rc;
    }

    pdebug(DEBUG_DETAIL,"Created new packet data:");
    pdebug_dump_bytes(DEBUG_DETAIL, bytebuf_get_buffer(buf), last_pos + remainder);

    rc = bytebuf_set_cursor(buf, last_pos + remainder);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to seek to end of new data!");
        return rc;
    }

    return rc;
}


int unmarshal_cip_write(bytebuf_p buf)
{
    (void)buf;

    return PLCTAG_ERR_NOT_IMPLEMENTED;
}



int marshal_cip_cm_unconnected(int prev_rc, bytebuf_p buf, const char *ioi_path)
{
    int rc = PLCTAG_STATUS_OK;
    uint8_t cm_path[] = { 0x20, 0x06, 0x24, 0x01 }; /* path to CM */
    uint16_t payload_size = 0;

    /*

     The CM Unconnected Request packet looks like this:

              CM Service Request - Connection Manager
        uint8_t cm_service_code;         0x52 Unconnected Send
        uint8_t cm_req_path_size;        2, size in words of path, next field
        uint8_t cm_req_path[4];          0x20, 0x06, 0x24, 0x01 for CM, instance 1

              Unconnected send
        uint8_t secs_per_tick;          seconds per tick
        uint8_t timeout_ticks;          timeout = src_secs_per_tick * src_timeout_ticks
        uint16_t payload_size;          Size of payload in bytes.

        ...CIP read/write request, embedded packet...

        uint16_t path_size;             in 16-bit chunks.
        uint8_t[]                       IOI path to target device, connection IOI... zero padded if odd length.
    */

    pdebug(DEBUG_INFO,"Starting.");

    /* how big is the payload? */
    payload_size = bytebuf_get_size(buf);

    /* abort if the wrapped call was not good. */
    if(prev_rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Aborting due to previous bad return code.");
        return prev_rc;
    }

    /* two calls, first gets size, second writes data. */
    for(int i=0; rc == PLCTAG_STATUS_OK && i < 2; i++) {
        rc = bytebuf_marshal(i == 0 ? NULL : buf ,
                             BB_U8, AB_CIP_CMD_UNCONNECTED_SEND,
                             BB_U8, (uint8_t)((sizeof(cm_path)/sizeof(cm_path[0])) / 2), /*length of path in 16-bit words*/
                             BB_BYTES, cm_path, (sizeof(cm_path)/sizeof(cm_path[0])),
                             BB_U8, (uint8_t)AB_CIP_SECS_PER_TICK,
                             BB_U8, (uint8_t)AB_CIP_TIMEOUT_TICKS,
                             BB_U16, payload_size
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

    if(rc < PLCTAG_STATUS_OK) {
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
    rc = cip_encode_path(buf, ioi_path, BB_U16);
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



int unmarshal_cip_response_header(int prev_rc, bytebuf_p buf, uint8_t *reply_service, uint8_t *status, uint16_t *extended_status)
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




int marshal_cip_cfp_connected(int prev_rc, bytebuf_p buf, uint32_t target_conn_id, uint16_t conn_seq_num)
{
    int rc = PLCTAG_STATUS_OK;
    int payload_length = 0;

    /*
        CPF header looks like this:

              Interface Handle etc.
        uint32_t interface_handle;      ALWAYS 0
        uint16_t router_timeout;        Usually set to a few seconds.

              Common Packet Format - CPF Connected
        uint16_t cpf_item_count;        ALWAYS 2
        uint16_t cpf_cai_item_type;     ALWAYS 0x00A1 Connected Address Item
        uint16_t cpf_cai_item_length;   ALWAYS 2 ?
        uint32_t cpf_targ_conn_id;      the connection id from Forward Open
        uint16_t cpf_cdi_item_type;     ALWAYS 0x00B1, Connected Data Item type
        uint16_t cpf_cdi_item_length;   length in bytes of the rest of the packet

          Connection sequence number
        uint16_t cpf_conn_seq_num;      connection sequence ID, inc for each message

        ... payload ...
    */

    pdebug(DEBUG_INFO,"Starting.");

    /* punt if wrapped call failed. */
    if(prev_rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Aborting due to previous bad return code.");
        return prev_rc;
    }

    /* get the payload size before we change the buffer. */
    payload_length = bytebuf_get_size(buf) + 2; /* add two for the size of the connection sequence number */

   /* two calls, first gets size, second writes data. */
    for(int i=0; rc == PLCTAG_STATUS_OK && i < 2; i++) {
        rc = bytebuf_marshal(i == 0 ? NULL : buf ,
                             BB_U32, 0,     /* interface handle, apparently zero */
                             BB_U16, (uint16_t)AB_DEFAULT_ROUTER_TIMEOUT_SECS,
                             BB_U16, 2,     /* item count */
                             BB_U16, (uint16_t)AB_CIP_ITEM_CAI,
                             BB_U16, (uint16_t)4,   /* Connected address length is four bytes */
                             BB_U32, target_conn_id, /* target connection ID from Forward Open */
                             BB_U16, (uint16_t)AB_CIP_ITEM_CDI,
                             BB_U16, (uint16_t)payload_length,
                             BB_U16, conn_seq_num
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


int unmarshal_cip_cfp_connected(int prev_rc, bytebuf_p buf, uint32_t orig_conn_id, uint16_t conn_seq_num)
{
    int rc = PLCTAG_STATUS_OK;
    uint32_t interface_handle;
    uint16_t router_timeout;
    uint16_t item_count;
    uint16_t item_cai;
    uint16_t item_cai_length;
    uint32_t item_conn_id;
    uint16_t item_cdi;
    uint16_t payload_length;
    uint16_t packet_conn_seq_num;

    /* The CPF header on a reply looks like this:
        Interface Handle etc.
    uint32_t interface_handle;
    uint16_t router_timeout;

        Common Packet Format - CPF Connected
    uint16_t cpf_item_count;        ALWAYS 2
    uint16_t cpf_cai_item_type;     ALWAYS 0x00A1 Connected Address Item
    uint16_t cpf_cai_item_length;   ALWAYS 2 ?
    uint32_t cpf_orig_conn_id;      our connection ID, NOT the target's
    uint16_t cpf_cdi_item_type;     ALWAYS 0x00B1, Connected Data Item type
    uint16_t cpf_cdi_item_length;   length in bytes of the rest of the packet

       connection ID from request
    uint16_t cpf_conn_seq_num;      connection sequence ID, inc for each message



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
                     BB_U16, &item_cai,
                     BB_U16, &item_cai_length,
                     BB_U32, &item_conn_id,
                     BB_U16, &item_cdi,
                     BB_U16, &payload_length,
                     BB_U16, &packet_conn_seq_num
                     );

    if(rc < PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to marshal header!");
        return rc;
    }

    /* is this the right packet? */
    if(item_conn_id != orig_conn_id) {
        pdebug(DEBUG_WARN,"Got unexpected packet from a different connection!");
        return PLCTAG_ERR_NOT_FOUND;
    }

    if(packet_conn_seq_num != conn_seq_num) {
        pdebug(DEBUG_WARN,"Got unexpected packet sequence number!");
        return PLCTAG_ERR_NOT_FOUND;
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

    pdebug(DEBUG_DETAIL,"Sending packet of size %d:", bytebuf_get_size(payload));
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



int cip_encode_path(bytebuf_p buf, const char *ioi_path, bytebuf_arg_type count_type)
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
    rc = bytebuf_marshal(buf, count_type, 0); /* place holder */
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
        rc = bytebuf_marshal(buf, BB_U8, 0); /* padding value */
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
    rc = bytebuf_marshal(buf, count_type, (uint8_t)(ioi_size/2));
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
    int symbol_len = 0;
    int tmp_index = 0;
    int state = START;
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

                if(*p != ']') {
                    pdebug(DEBUG_WARN,"Incorrect array format, must have closing ] character.");
                    return PLCTAG_ERR_BAD_PARAM;
                }

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
