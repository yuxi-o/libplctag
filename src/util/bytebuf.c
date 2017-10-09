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
#include <stdint.h>
#include <lib/libplctag.h>
#include <util/bytebuf.h>
#include <util/debug.h>
#include <util/refcount.h>


#define CHUNK_SIZE (100)
#define NEAREST_CHUNK(n) ((int)(CHUNK_SIZE * ((int)((n) + (CHUNK_SIZE-1))/CHUNK_SIZE)))

struct bytebuf_t {
    int size;
    int capacity;
    int cursor;
    uint8_t *bytes;
};


static int ensure_capacity(bytebuf_p buf, int new_cap);



bytebuf_p bytebuf_create(int initial_cap)
{
    bytebuf_p buf = NULL;

    if(initial_cap < 0) {
        pdebug(DEBUG_WARN,"Initial capacity less than zero!");
        return buf;
    }

    initial_cap = NEAREST_CHUNK(initial_cap);

    buf = mem_alloc(sizeof(struct bytebuf_t));
    if(!buf) {
        pdebug(DEBUG_ERROR,"Unable to allocate byte buffer struct!");
        return NULL;
    }

    buf->size = 0;
    buf->capacity = initial_cap;
    buf->cursor = 0;

    buf->bytes = mem_alloc(buf->capacity);
    if(!buf->bytes) {
        pdebug(DEBUG_ERROR,"Unable to allocate buffer bytes!");
        bytebuf_destroy(buf);
        return NULL;
    }

    return buf;
}


int bytebuf_set_cursor(bytebuf_p buf, int cursor)
{
    int new_cap = cursor;

    pdebug(DEBUG_DETAIL,"Starting to move cursor to %d", cursor);

    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null or invalid reference!");
        return PLCTAG_ERR_NULL_PTR;
    }

    /* if the cursor went off the front end of the buffer, we need to expand accordingly. */
    new_cap = (cursor < 0 ? (buf->size + (-cursor)) : cursor+1);

    if(!ensure_capacity(buf, new_cap) == PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to expand byte buffer capacity!");
        return PLCTAG_ERR_NO_MEM;
    }

    if(cursor < 0) {
        int amount = -cursor;

        pdebug(DEBUG_DETAIL,"Cursor was negative, prepending %d bytes of space.", amount);

        mem_move(&buf->bytes[amount], &buf->bytes[0], buf->size);
        mem_set(&buf->bytes[0], 0, amount);
        buf->size += amount;
        cursor = 0;
    }

    if(cursor >= buf->size) {
        buf->size = cursor + 1;
    }

    buf->cursor = cursor;

    return PLCTAG_STATUS_OK;
}


int bytebuf_put(bytebuf_p buf, uint8_t data)
{
    pdebug(DEBUG_DETAIL,"Starting.");

    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null or invalid reference!");
        return PLCTAG_ERR_NULL_PTR;
    }

    /* make sure we have capacity to push the new byte. */
    if(!ensure_capacity(buf, buf->cursor + 1) == PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to expand byte buffer capacity!");
        return PLCTAG_ERR_NO_MEM;
    }

    buf->bytes[buf->cursor] = data;

    buf->cursor++;

    if(buf->cursor >= buf->size) {
        buf->size = buf->cursor+1;
    }

    pdebug(DEBUG_DETAIL,"Done.");

    return PLCTAG_STATUS_OK;
}


int bytebuf_get(bytebuf_p buf, uint8_t *data)
{
    pdebug(DEBUG_DETAIL,"Starting.");

    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null or invalid reference!");
        return PLCTAG_ERR_NULL_PTR;
    }

    /* make sure we have data for the read. */
    if(buf->cursor < 0 || buf->cursor >= buf->size) {
        pdebug(DEBUG_ERROR, "Cursor out of bounds!");
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    *data = buf->bytes[buf->cursor];

    buf->cursor++;

    pdebug(DEBUG_DETAIL,"Done.");

    return PLCTAG_STATUS_OK;
}


int bytebuf_size(bytebuf_p buf)
{
    pdebug(DEBUG_DETAIL,"Starting.");

    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null or invalid reference!");
        return PLCTAG_ERR_NULL_PTR;
    }

    pdebug(DEBUG_DETAIL, "Done.");

    return buf->size;
}


uint8_t *bytebuf_get_buffer(bytebuf_p buf)
{
    pdebug(DEBUG_DETAIL,"Starting.");

    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null or invalid reference!");
        return NULL;
    }

    pdebug(DEBUG_DETAIL, "Done.");

    return &buf->bytes[0];
}



int bytebuf_destroy(bytebuf_p buf)
{
    pdebug(DEBUG_INFO,"Starting.");

    if(!buf) {
        pdebug(DEBUG_WARN,"Null pointer passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    mem_free(buf->bytes);

    mem_free(buf);

    pdebug(DEBUG_INFO,"Done.");

    return PLCTAG_STATUS_OK;
}





/***********************************************************************
 ***************************** Helper Functions ************************
 **********************************************************************/






int ensure_capacity(bytebuf_p buf, int cap)
{
    uint8_t *bytes = NULL;
    int new_cap = 0;

    pdebug(DEBUG_DETAIL,"Starting");

    /* round cap up to CHUNK_SIZE */
    new_cap = NEAREST_CHUNK(cap);

    pdebug(DEBUG_DETAIL,"New capacity request is %d from actual %d",new_cap, cap);

    if(new_cap <= buf->capacity) {
        pdebug(DEBUG_DETAIL,"No need to allocate new buffer. Current buffer has capacity %d", buf->capacity);
        return PLCTAG_STATUS_OK;
    }

    pdebug(DEBUG_DETAIL,"Allocating more buffer space.");

    /* need to allocate more memory */
    bytes = mem_alloc(new_cap);
    if(!bytes) {
        pdebug(DEBUG_ERROR,"Unable to allocate new byte buffer bytes!");
        return PLCTAG_ERR_NO_MEM;
    }

    mem_copy(bytes, &buf->bytes[0], buf->size);

    mem_free(buf->bytes);

    buf->bytes = bytes;

    pdebug(DEBUG_DETAIL,"Done.");

    return PLCTAG_STATUS_OK;
}
