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
#include <util/async_socket.h>
#include <util/debug.h>
#include <util/refcount.h>


struct async_socket_t {
    char *host;
    int port;
    int status;
    sock_p sock;
    thread_p worker_thread;
};


static void async_socket_destroy(void *sock_arg, int arg_count, void **args);
static THREAD_FUNC(async_socket_setup_handler);

async_socket_p async_tcp_socket_create(const char *host, int port)
{
    async_socket_p async_sock = NULL;
    int rc = PLCTAG_STATUS_OK;
    async_socket_p tmp_sock = NULL;

    async_sock = rc_alloc(sizeof(struct async_socket_t), async_socket_destroy, 0);
    if(!async_sock) {
        pdebug(DEBUG_ERROR,"Unable to allocate async socket!");
        return NULL;
    }

    rc = socket_create(&async_sock->sock);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR,"Unable to create new socket!");
        rc_dec(async_sock);
        return NULL;
    }

    async_sock->port = port;
    async_sock->host = str_dup(host);

    if(!async_sock->host) {
        pdebug(DEBUG_ERROR,"Unable to duplicate host string!");
        rc_dec(async_sock);
        return NULL;
    }

    tmp_sock = rc_inc(async_sock);
    if(!tmp_sock) {
        pdebug(DEBUG_WARN,"Unable to get strong reference to async socket!");
        rc_dec(async_sock);
        return NULL;
    }

    /* start a thread to handle the socket opening and binding, which means possible DNS. */
    rc = thread_create(&async_sock->worker_thread, async_socket_setup_handler, 32*1024, tmp_sock);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR,"Unable to create handler thread!");

        /* do this twice because we just incremented the reference count. */
        rc_dec(async_sock);
        rc_dec(async_sock);

        return NULL;
    }

    return async_socket;
}



int async_socket_status(async_socket_p async_socket)
{
    pdebug(DEBUG_DETAIL,"Starting.");

    if(!async_socket) {
        pdebug(DEBUG_WARN,"Null pointer passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return async_socket->status;
}



int async_tcp_socket_write(async_socket_p async_socket, uint8_t *data, int data_len)
{
    if(!async_socket) {
        pdebug(DEBUG_WARN,"Null pointer passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(async_socket->status != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Socket not ready!");
        return async_socket->status;
    }

    return socket_write(async_sock->sock, data, data_len);
}


int async_tcp_socket_read(async_socket_p async_socket, uint8_t *data, int data_len)
{
    if(!async_socket) {
        pdebug(DEBUG_WARN,"Null pointer passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(async_socket->status != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Socket not ready!");
        return async_socket->status;
    }

    return socket_read(async_sock->sock, data, data_len);
}



/***********************************************************************
 **************************** Helper Functions *************************
 **********************************************************************/


 void async_socket_destroy(void *sock_arg, int arg_count, void **args)
 {
    async_socket_p async_sock = sock_arg;

    pdebug(DEBUG_INFO,"Starting.");

    if(!async_sock) {
        pdebug(DEBUG_WARN,"Null pointer passed!");
        return;
    }

    if(async_sock->sock) {
        socket_close(async_sock->sock);
        socket_destroy(&async_sock->sock);
        async->sock = NULL;
    }

    if(async_sock->host) {
        mem_free(async_sock->host);
        async_sock->host = NULL;
    }

    pdebug(DEBUG_INFO,"Done.");
 }



 THREAD_FUNC(async_socket_setup_handler)
 {
    async_socket_p async_sock = arg;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO,"Starting.");

    thread_detach();

    if(!async_sock) {
        pdebug(DEBUG_WARN,"Argument is NULL!");
        thread_stop();
    }

    /* try to open the socket */
    rc = socket_connect_tcp(async_sock->sock, async_sock->host, async_sock->port);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to connect to socket!");
    }

    /* capture the status, good or bad. */
    async_sock->status = rc;

    /* clean up */
    async_sock->worker_thread = NULL;

    /* release our reference to the async socket object. */
    rc_dec(async_sock);

    pdebug(DEBUG_INFO,"Done.");

    thread_stop();

    /* never get here */
    THREAD_RETURN(0);
 }
