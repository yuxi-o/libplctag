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

#ifndef __UTIL_ASYNC_SOCKET_H__
#define __UTIL_ASYNC_SOCKET_H__ 1


typedef struct async_socket_t *async_socket_p;

extern async_socket_p async_tcp_socket_create(const char *host, int port);
extern int async_tcp_socket_status(async_socket_p async_socket);
extern int async_tcp_socket_write(async_socket_p async_socket, uint8_t *data, int data_len);
extern int async_tcp_socket_read(async_socket_p async_socket, uint8_t *data, int data_len);

#endif
