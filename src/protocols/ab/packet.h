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

#ifndef __PROTOCOLS_AB_LOGIX_PACKET_H__
#define __PROTOCOLS_AB_LOGIX_PACKET_H__ 1

#include <stdint.h>
#include <util/bytebuf.h>


/***********************************************************************
 ***************************** EIP Data ********************************
 **********************************************************************/

#define AB_EIP_VERSION ((uint16_t)0x0001)

/* main EIP commands */
#define AB_EIP_REGISTER_SESSION     ((uint16_t)0x0065)
#define AB_EIP_UNREGISTER_SESSION   ((uint16_t)0x0066)
#define AB_EIP_UNCONNECTED_SEND     ((uint16_t)0x006F)
#define AB_EIP_CONNECTED_SEND       ((uint16_t)0x0070)

/* AB packet info */
#define AB_EIP_DEFAULT_PORT 44818

/* status we care about */
#define AB_EIP_OK   (0)

#define AB_BYTE_ORDER_INT16     (0x10)
#define AB_BYTE_ORDER_INT32     (0x3210)
#define AB_BYTE_ORDER_INT64     (0x76543210)
#define AB_BYTE_ORDER_FLOAT32   (0x3210)
#define AB_BYTE_ORDER_FLOAT64   (0x76543210)


/***********************************************************************
 *************************** CM/MR Data ********************************
 **********************************************************************/

#define AB_CIP_CMD_UNCONNECTED_SEND     ((uint8_t)0x52)

/* CPF item definitions */
#define AB_CIP_ITEM_NAI ((uint16_t)0x0000) /* NULL Address Item */
#define AB_CIP_ITEM_CAI ((uint16_t)0x00A1) /* connected address item */
#define AB_CIP_ITEM_CDI ((uint16_t)0x00B1) /* connected data item */
#define AB_CIP_ITEM_UDI ((uint16_t)0x00B2) /* Unconnected data item */

#define AB_CIP_SECS_PER_TICK 0x0A
#define AB_CIP_TIMEOUT_TICKS 0x05
#define AB_DEFAULT_ROUTER_TIMEOUT_SECS ((uint16_t)(5))




/***********************************************************************
 ***************************** CIP Data ********************************
 **********************************************************************/
#define AB_CIP_OK ((uint8_t)0)
#define AB_CIP_CMD_OK ((uint8_t)0x80)


/* CIP embedded packet commands */
#define AB_CIP_READ             ((uint8_t)0x4C)
#define AB_CIP_WRITE            ((uint8_t)0x4D)
#define AB_CIP_READ_FRAG        ((uint8_t)0x52)
#define AB_CIP_WRITE_FRAG       ((uint8_t)0x53)

#define AB_CIP_CMD_GET_INSTANCE_ATTRIB_LIST ((uint8_t)0x55)

/* CIP status */
#define AB_CIP_STATUS_OK                ((uint8_t)0x00)
#define AB_CIP_STATUS_FRAG              ((uint8_t)0x06)

/* CIP Data Types */


#define AB_CIP_DATA_BIT         ((uint8_t)0xC1) /* Boolean value, 1 bit */
#define AB_CIP_DATA_SINT        ((uint8_t)0xC2) /* Signed 8–bit integer value */
#define AB_CIP_DATA_INT         ((uint8_t)0xC3) /* Signed 16–bit integer value */
#define AB_CIP_DATA_DINT        ((uint8_t)0xC4) /* Signed 32–bit integer value */
#define AB_CIP_DATA_LINT        ((uint8_t)0xC5) /* Signed 64–bit integer value */
#define AB_CIP_DATA_USINT       ((uint8_t)0xC6) /* Unsigned 8–bit integer value */
#define AB_CIP_DATA_UINT        ((uint8_t)0xC7) /* Unsigned 16–bit integer value */
#define AB_CIP_DATA_UDINT       ((uint8_t)0xC8) /* Unsigned 32–bit integer value */
#define AB_CIP_DATA_ULINT       ((uint8_t)0xC9) /* Unsigned 64–bit integer value */
#define AB_CIP_DATA_REAL        ((uint8_t)0xCA) /* 32–bit floating point value, IEEE format */
#define AB_CIP_DATA_LREAL       ((uint8_t)0xCB) /* 64–bit floating point value, IEEE format */
#define AB_CIP_DATA_STIME       ((uint8_t)0xCC) /* Synchronous time value */
#define AB_CIP_DATA_DATE        ((uint8_t)0xCD) /* Date value */
#define AB_CIP_DATA_TIME_OF_DAY ((uint8_t)0xCE) /* Time of day value */
#define AB_CIP_DATA_DATE_AND_TIME ((uint8_t)0xCF) /* Date and time of day value */
#define AB_CIP_DATA_STRING      ((uint8_t)0xD0) /* Character string, 1 byte per character */
#define AB_CIP_DATA_BYTE        ((uint8_t)0xD1) /* 8-bit bit string */
#define AB_CIP_DATA_WORD        ((uint8_t)0xD2) /* 16-bit bit string */
#define AB_CIP_DATA_DWORD       ((uint8_t)0xD3) /* 32-bit bit string */
#define AB_CIP_DATA_LWORD       ((uint8_t)0xD4) /* 64-bit bit string */
#define AB_CIP_DATA_STRING2     ((uint8_t)0xD5) /* Wide char character string, 2 bytes per character */
#define AB_CIP_DATA_FTIME       ((uint8_t)0xD6) /* High resolution duration value */
#define AB_CIP_DATA_LTIME       ((uint8_t)0xD7) /* Medium resolution duration value */
#define AB_CIP_DATA_ITIME       ((uint8_t)0xD8) /* Low resolution duration value */
#define AB_CIP_DATA_STRINGN     ((uint8_t)0xD9) /* N-byte per char character string */
#define AB_CIP_DATA_SHORT_STRING ((uint8_t)0xDA) /* Counted character sting with 1 byte per character and 1 byte length indicator */
#define AB_CIP_DATA_TIME        ((uint8_t)0xDB) /* Duration in milliseconds */
#define AB_CIP_DATA_EPATH       ((uint8_t)0xDC) /* CIP path segment(s) */
#define AB_CIP_DATA_ENGUNIT     ((uint8_t)0xDD) /* Engineering units */
#define AB_CIP_DATA_STRINGI     ((uint8_t)0xDE) /* International character string (encoding?) */

/* aggregate data type byte values */
#define AB_CIP_DATA_ABREV_STRUCT    ((uint8_t)0xA0) /* Data is an abbreviated struct type, i.e. a CRC of the actual type descriptor */
#define AB_CIP_DATA_ABREV_ARRAY     ((uint8_t)0xA1) /* Data is an abbreviated array type. The limits are left off */
#define AB_CIP_DATA_FULL_STRUCT     ((uint8_t)0xA2) /* Data is a struct type descriptor */
#define AB_CIP_DATA_FULL_ARRAY      ((uint8_t)0xA3) /* Data is an array type descriptor */





extern int marshal_eip_header(int prev_rc, bytebuf_p buf, uint16_t command, uint32_t session_handle, uint64_t sender_context);
extern int unmarshal_eip_header(bytebuf_p buf, uint16_t *command, uint16_t *length, uint32_t *session_handle, uint32_t *status, uint64_t *sender_context);

extern int marshal_register_session(bytebuf_p buf, uint16_t eip_version, uint16_t option_flags);

extern int marshal_cip_get_tag_info(bytebuf_p buf, uint32_t start_instance);

extern int marshal_cip_read(bytebuf_p buf, const char *name, int elem_count, int offset);
extern int unmarshal_cip_read(int prev_rc, bytebuf_p buf);

extern int marshal_cip_write(bytebuf_p buf, const char *name, bytebuf_p tag_data);

extern int marshal_cip_cm_unconnected(int prev_rc, bytebuf_p buf, const char *ioi_path);
extern int unmarshal_cip_cm_unconnected(int prev_rc, bytebuf_p buf, uint8_t *reply_service, uint8_t *status, uint16_t *extended_status);

extern int marshal_cip_cfp_unconnected(int prev_rc, bytebuf_p buf);
extern int unmarshal_cip_cfp_unconnected(int prev_rc, bytebuf_p buf);

extern int send_eip_packet(sock_p sock, bytebuf_p payload);
extern int receive_eip_packet(sock_p sock, bytebuf_p buf);

#endif
