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
#include <lib/libplctag.h>
#include <ab/logix/packet.h>
#include <util/bytebuf.h>
#include <util/debug.h>




#define EIP_ENCAP_SIZE (24)


/* byte order data */
int byte_order_8[1]  = {0};
int byte_order_16[2] = {0,1};
int byte_order_32[4] = {0,1,2,3};
int byte_order_64[8] = {0,1,2,3,4,5,6,7};



#define MARSHAL_FIELD(BUF, BITS, FIELD)                                \
    do {                                                               \
        rc = bytebuf_set_int(BUF, (BITS)/8 , byte_order_ ##BITS , (int64_t)FIELD);\
        if(rc != PLCTAG_STATUS_OK) {                                   \
            pdebug(DEBUG_WARN,"Error inserting " #FIELD " field!");    \
            return rc;                                                 \
        }                                                              \
    } while(0)


#define UNMARSHAL_FIELD(BUF, BITS, TYPE, FIELD)                        \
    do {                                                               \
        rc = bytebuf_get_int(BUF, (BITS)/8 , byte_order_ ##BITS , &val);\
        if(rc != PLCTAG_STATUS_OK) {                                   \
            pdebug(DEBUG_WARN,"Error unmarshalling " #FIELD " field!");\
            return rc;                                                 \
        }                                                              \
        if(FIELD) {                                                    \
            *FIELD = (TYPE)(int ## BITS ## _t)val;                     \
        }                                                              \
    } while(0)




int marshal_eip_header(bytebuf_p buf,
                        uint16_t command,         /* command, like Register Session*/
                        uint32_t session_handle,  /* from session set up */
                        uint64_t sender_context)  /* identifies the unconnected session packet */
{
    int rc = PLCTAG_STATUS_OK;
    int payload_size;
    int status = 0;
    int options = 0;

    pdebug(DEBUG_DETAIL,"Starting.");

    payload_size = bytebuf_get_size(buf);

    /* make space at the beginning of the buffer. */
    rc = bytebuf_set_cursor(buf, -24); /* FIXME - MAGIC */
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error setting buffer cursor!");
        return rc;
    }

    /* command */
    MARSHAL_FIELD(buf, 16, command);
    //~ rc = bytebuf_set_int(buf, 2, byte_order_16, (int64_t)command);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error inserting command field!");
        //~ return rc;
    //~ }

    /* payload length */
    MARSHAL_FIELD(buf, 16, payload_size);
    //~ rc = bytebuf_set_int(buf, 2, byte_order_16, (int64_t)payload_size);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error inserting payload size field!");
        //~ return rc;
    //~ }

    /* session handle */
    MARSHAL_FIELD(buf, 32, session_handle);
    //~ rc = bytebuf_set_int(buf, 4, byte_order_32, (int64_t)session_handle);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error inserting session handle field!");
        //~ return rc;
    //~ }

    /* status, always zero on send */
    status = 0;
    MARSHAL_FIELD(buf, 32, status);
    //~ bytebuf_set_int(buf, 4, byte_order_32, (int64_t)0);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error inserting status field!");
        //~ return rc;
    //~ }

    /* session packet identifier */
    MARSHAL_FIELD(buf, 64, sender_context);
    //~ rc = bytebuf_set_int(buf, 8, byte_order_64, (int64_t)sender_context);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error inserting sender context field!");
        //~ return rc;
    //~ }

    /* options, zero always? */
    options = 0;
    MARSHAL_FIELD(buf, 32, options);
    //~ rc = bytebuf_set_int(buf, 4, byte_order_32, (int64_t)0);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error inserting options field!");
        //~ return rc;
    //~ }

    pdebug(DEBUG_DETAIL,"Done.");

    return PLCTAG_STATUS_OK;
}


int unmarshal_eip_header(bytebuf_p buf, uint16_t *command, uint16_t *length, uint32_t *session_handle, uint32_t *status, uint64_t *sender_context, uint32_t *options)
{
    int rc = PLCTAG_STATUS_OK;
    int64_t val;

    pdebug(DEBUG_DETAIL,"Starting.");

    UNMARSHAL_FIELD(buf, 16, uint16_t, command);
    //~ rc = bytebuf_get_int(buf, 2, byte_order_16, &val);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error getting command field!");
        //~ return rc;
    //~ }
    //~ *command = (uint16_t)(int16_t)val;

    UNMARSHAL_FIELD(buf, 16, uint16_t, length);
    //~ rc = bytebuf_get_int(buf, 2, byte_order_16, &val);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error getting length field!");
        //~ return rc;
    //~ }
    //~ *length = (uint16_t)(int16_t)val;

    UNMARSHAL_FIELD(buf, 32, uint32_t, session_handle);
    //~ rc = bytebuf_get_int(buf, 4, byte_order_32, &val);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error getting session handle field!");
        //~ return rc;
    //~ }
    //~ *session_handle = (uint32_t)(int32_t)val;

    UNMARSHAL_FIELD(buf, 32, uint32_t, status);
    //~ rc = bytebuf_get_int(buf, 4, byte_order_32, &val);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error getting status field!");
        //~ return rc;
    //~ }
    //~ *status = (uint32_t)(int32_t)val;

    UNMARSHAL_FIELD(buf, 64, uint64_t, sender_context);
    //~ rc = bytebuf_get_int(buf, 8, byte_order_64, &val);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error getting sender context field!");
        //~ return rc;
    //~ }
    //~ *sender_context = (uint64_t)val;

    UNMARSHAL_FIELD(buf, 32, uint32_t, options);
    //~ rc = bytebuf_get_int(buf, 4, byte_order_32, &val);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error getting options field!");
        //~ return rc;
    //~ }
    //~ *options = (uint32_t)(int32_t)val;

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}


int marshal_register_session(bytebuf_p buf, uint16_t eip_version, uint16_t option_flags)
{
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_DETAIL,"Starting.");

    /* requested protocol version */
    MARSHAL_FIELD(buf, 16, eip_version);
    //~ rc = bytebuf_set_int(buf, 2, byte_order_16, (int64_t)eip_version);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error inserting eip_version field!");
        //~ return rc;
    //~ }

    /* option flags, always zero? */
    MARSHAL_FIELD(buf, 16, option_flags);
    //~ bytebuf_set_int(buf, 2, byte_order_16, (int64_t)option_flags);
    //~ if(rc != PLCTAG_STATUS_OK) {
        //~ pdebug(DEBUG_WARN,"Error inserting option_flags field!");
        //~ return rc;
    //~ }

    pdebug(DEBUG_DETAIL,"Done.");

    return PLCTAG_STATUS_OK;
}





int send_eip_packet(sock_p sock, uint16_t command, uint32_t session_handle, uint64_t sender_context, bytebuf_p payload)
{
    int rc = PLCTAG_STATUS_OK;
    int offset = 0;

    pdebug(DEBUG_DETAIL,"Starting.");

    /* inject an EIP encapsulation header. */
    rc = marshal_eip_header(payload,command, session_handle, sender_context);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Error inserting EIP encapsulation header!");
        return rc;
    }

    /* set the cursor back to the beginning. */
    bytebuf_set_cursor(payload, 0);

    pdebug(DEBUG_DETAIL,"Sending packet:");
    pdebug_dump_bytes(DEBUG_DETAIL,bytebuf_get_buffer(payload), bytebuf_get_size(payload));

    /* send the data. */
    do {
        rc = socket_write(sock, bytebuf_get_buffer(payload), bytebuf_get_size(payload) - offset);
        if(rc > 0) {
            offset += rc;
            if(offset < bytebuf_get_size(payload)) {
                bytebuf_set_cursor(payload, offset);
            }
        }
    } while(rc >= 0 && offset < bytebuf_get_size(payload));

    if(rc > 0) {
        rc = PLCTAG_STATUS_OK;
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}



int receive_eip_packet(sock_p sock, bytebuf_p buf)
{
    int rc = 0;
    int total_read = 0;
    int data_needed = EIP_ENCAP_SIZE; /* MAGIC */

    pdebug(DEBUG_DETAIL,"Starting.");

    bytebuf_reset(buf);

    /* make sure that there is room for the eip header. */
    bytebuf_set_cursor(buf, EIP_ENCAP_SIZE); /* MAGIC */

    /* now set it back to start reading into it at zero. */
    bytebuf_set_cursor(buf, 0);

    /* read the header first, then figure out how much more we need to read. */
    do {
        rc = socket_read(sock, bytebuf_get_buffer(buf) + total_read,
                         data_needed - total_read);

        if (rc < 0) {
            if (rc != PLCTAG_ERR_NO_DATA) {
                /* error! */
                pdebug(DEBUG_WARN,"Error reading socket! rc=%d",rc);
                return rc;
            }
        } else {
            total_read += rc;

            pdebug(DEBUG_DETAIL,"Got %d more bytes of data.", rc);

            /* recalculate the amount of data needed if we have just completed the read of an encap header */
            if (total_read >= EIP_ENCAP_SIZE && data_needed == EIP_ENCAP_SIZE) {
                uint16_t command;
                uint16_t length;
                uint32_t session_handle;
                uint32_t status;
                uint64_t sender_context;
                uint32_t options;

                bytebuf_set_cursor(buf, 0);

                /* parse the header. */
                rc = unmarshal_eip_header(buf, &command, &length, &session_handle, &status, &sender_context, &options);
                if(rc != PLCTAG_STATUS_OK) {
                    pdebug(DEBUG_WARN,"Error parsing EIP encapsulation header!");
                    return rc;
                }

                data_needed = EIP_ENCAP_SIZE + length;

                /* ensure the capacity */
                bytebuf_set_cursor(buf, data_needed);

                /* set back to the end of the data */
                bytebuf_set_cursor(buf, total_read);
            }
        }
    } while (rc > 0 && total_read < data_needed);

    if(rc >= 0) {
        rc = PLCTAG_STATUS_OK;

        bytebuf_set_cursor(buf, 0);

        pdebug(DEBUG_DETAIL,"Received packet:");
        pdebug_dump_bytes(DEBUG_DETAIL, bytebuf_get_buffer(buf), bytebuf_get_size(buf));
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return rc;
}
