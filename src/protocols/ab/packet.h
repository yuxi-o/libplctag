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

extern int byte_order_8[1];
extern int byte_order_16[2];
extern int byte_order_32[4];
extern int byte_order_64[8];

#define AB_EIP_VERSION ((uint16_t)0x0001)

/* main commands */
#define AB_EIP_REGISTER_SESSION     ((uint16_t)0x0065)
#define AB_EIP_UNREGISTER_SESSION   ((uint16_t)0x0066)
#define AB_EIP_READ_RR_DATA         ((uint16_t)0x006F)
#define AB_EIP_CONNECTED_SEND       ((uint16_t)0x0070)

/* AB packet info */
#define AB_EIP_DEFAULT_PORT 44818

/* status we care about */
#define AB_EIP_OK   (0)



extern int marshal_eip_header(bytebuf_p buf, uint16_t command, uint32_t session_handle, uint64_t sender_context);
extern int unmarshal_eip_header(bytebuf_p buf, uint16_t *command, uint16_t *length, uint32_t *session_handle, uint32_t *status, uint64_t *sender_context, uint32_t *options);

extern int marshal_register_session(bytebuf_p buf, uint16_t eip_version, uint16_t option_flags);

extern int send_eip_packet(sock_p sock, uint16_t command, uint32_t session_handle, uint64_t sender_context, bytebuf_p payload);
extern int receive_eip_packet(sock_p sock, bytebuf_p buf);

#endif
