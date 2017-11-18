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


#define CHUNK_SIZE (100)
#define NEAREST_CHUNK(n) ((int)(CHUNK_SIZE * ((int)((n) + (CHUNK_SIZE-1))/CHUNK_SIZE)))


struct bytebuf_t {
    int size;
    int capacity;
    int cursor;
    uint32_t bo_int16;
    uint32_t bo_int32;
    uint32_t bo_int64;
    uint32_t bo_float32;
    uint32_t bo_float64;
    uint8_t *bytes;
};




static int get_int8(bytebuf_p buf, int8_t *val);
static int set_int8(bytebuf_p buf, int8_t val);

static int get_int16(bytebuf_p buf, int16_t *val);
static int set_int16(bytebuf_p buf, int16_t val);

static int get_int32(bytebuf_p buf, int32_t *val);
static int set_int32(bytebuf_p buf, int32_t val);

static int get_int64(bytebuf_p buf, int64_t *val);
static int set_int64(bytebuf_p buf, int64_t val);

static int get_float32(bytebuf_p buf, float *val);
static int set_float32(bytebuf_p buf, float val);

static int get_float64(bytebuf_p buf, double *val);
static int set_float64(bytebuf_p buf, double val);

static int get_int(bytebuf_p buf, int size, uint32_t byte_order, int64_t *val);
static int set_int(bytebuf_p buf, int size, uint32_t byte_order, int64_t val);





bytebuf_p bytebuf_create(int initial_cap, uint32_t bo_int16, uint32_t bo_int32, uint32_t bo_int64, uint32_t bo_float32, uint32_t bo_float64)
{
    bytebuf_p buf = NULL;

    pdebug(DEBUG_INFO,"Starting.");

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
    buf->bo_int16 = bo_int16;
    buf->bo_int32 = bo_int32;
    buf->bo_int64 = bo_int64;
    buf->bo_float32 = bo_float32;
    buf->bo_float64 = bo_float64;

    buf->bytes = mem_alloc(buf->capacity);
    if(!buf->bytes) {
        pdebug(DEBUG_ERROR,"Unable to allocate buffer bytes!");
        bytebuf_destroy(buf);
        return NULL;
    }

    pdebug(DEBUG_INFO,"Done.");

    return buf;
}


int bytebuf_set_cursor(bytebuf_p buf, int cursor)
{
    int new_cap = cursor;

    pdebug(DEBUG_SPEW,"Starting to move cursor to %d", cursor);

    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null or invalid reference!");
        return PLCTAG_ERR_NULL_PTR;
    }

    /* if the cursor went off the front end of the buffer, we need to expand accordingly. */
    new_cap = (cursor < 0 ? (buf->size + (-cursor)) : cursor+1);

    if(!bytebuf_set_capacity(buf, new_cap) == PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to expand byte buffer capacity!");
        return PLCTAG_ERR_NO_MEM;
    }

    if(cursor < 0) {
        int amount = -cursor;

        pdebug(DEBUG_SPEW,"Cursor was negative, prepending %d bytes of space.", amount);

        mem_move(&buf->bytes[amount], &buf->bytes[0], buf->size);
        mem_set(&buf->bytes[0], 0, amount);
        buf->size += amount;
        cursor = 0;
    } else {
        if(cursor > buf->size) {
            buf->size = cursor;
            pdebug(DEBUG_SPEW,"Increasing size (%d) based on new cursor (%d).", buf->size, cursor);
        }
    }

    buf->cursor = cursor;

    pdebug(DEBUG_SPEW,"Done.");

    return PLCTAG_STATUS_OK;
}



int bytebuf_get_cursor(bytebuf_p buf)
{
    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    return buf->cursor;
}



int bytebuf_set_capacity(bytebuf_p buf, int cap)
{
    uint8_t *bytes = NULL;
    int new_cap = 0;

    pdebug(DEBUG_SPEW,"Starting");

    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null pointer!");
        return PLCTAG_ERR_NULL_PTR;
    }

    /* round cap up to CHUNK_SIZE */
    new_cap = NEAREST_CHUNK(cap);

    pdebug(DEBUG_SPEW,"New capacity request is %d from actual %d",new_cap, cap);

    if(new_cap <= buf->capacity) {
        pdebug(DEBUG_SPEW,"No need to allocate new buffer. Current buffer has capacity %d", buf->capacity);
        return PLCTAG_STATUS_OK;
    }

    pdebug(DEBUG_SPEW,"Allocating more buffer space.");

    /* need to allocate more memory */
    bytes = mem_alloc(new_cap);
    if(!bytes) {
        pdebug(DEBUG_ERROR,"Unable to allocate new byte buffer bytes!");
        return PLCTAG_ERR_NO_MEM;
    }

    mem_copy(bytes, &buf->bytes[0], buf->size);

    mem_free(buf->bytes);

    buf->bytes = bytes;

    pdebug(DEBUG_SPEW,"Done.");

    return PLCTAG_STATUS_OK;
}


int bytebuf_marshal_impl(bytebuf_p buf, int arg_count, ...)
{
    int index = 0;
    int length = 0;
    int rc = PLCTAG_STATUS_OK;
    va_list args;

    if(arg_count < 1) {
        pdebug(DEBUG_WARN,"Insufficient arguments!");
        return PLCTAG_ERR_BAD_PARAM;
    }

    va_start(args, arg_count);
    index = 0;
    while(index < arg_count && rc == PLCTAG_STATUS_OK) {
        bytebuf_arg_type arg_type;

        arg_type = va_arg(args, bytebuf_arg_type);

        switch(arg_type) {
            case BB_I8:
                if(index < (arg_count - 1)) {
                    int8_t val = (int8_t)va_arg(args, int);

                    length += 1;
                    index++;

                    if(buf) {
                        rc = set_int8(buf, (int8_t)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_I8 requires an int8_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_U8:
                if(index < (arg_count - 1)) {
                    uint8_t val = (uint8_t)(int8_t)va_arg(args, int);

                    length += 1;
                    index++;

                    if(buf) {
                        rc = set_int8(buf, (int8_t)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_U8 requires an uint8_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_I16:
                if(index < (arg_count - 1)) {
                    int16_t val = (int16_t)va_arg(args, int);

                    length += 2;
                    index++;

                    if(buf) {
                        rc = set_int16(buf, (int16_t)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_I16 requires an int16_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_U16:
                if(index < (arg_count - 1)) {
                    uint16_t val = (uint16_t)(int16_t)va_arg(args, int);

                    length += 2;
                    index++;

                    if(buf) {
                        rc = set_int16(buf, (int16_t)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_U16 requires an uint16_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_I32:
                if(index < (arg_count - 1)) {
                    int32_t val = va_arg(args, int32_t);

                    length += 4;
                    index++;

                    if(buf) {
                        rc = set_int32(buf, (int32_t)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_I32 requires an int32_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_U32:
                if(index < (arg_count - 1)) {
                    uint32_t val = va_arg(args, uint32_t);

                    length += 4;
                    index++;

                    if(buf) {
                        rc = set_int32(buf, (int32_t)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_U32 requires an uint32_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_I64:
                if(index < (arg_count - 1)) {
                    int64_t val = va_arg(args, int64_t);

                    length += 8;
                    index++;

                    if(buf) {
                        rc = set_int64(buf, (int64_t)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_I64 requires an int64_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_U64:
                if(index < (arg_count - 1)) {
                    uint64_t val = va_arg(args, uint64_t);

                    length += 8;
                    index++;

                    if(buf) {
                        rc = set_int64(buf, (int64_t)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_U64 requires an uint64_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_F32:
                if(index < (arg_count - 1)) {
                    float val = (float)va_arg(args, double);

                    length += 4;
                    index++;

                    if(buf) {
                        rc = set_float32(buf, (float)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_F32 requires a float argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_F64:
                if(index < (arg_count - 1)) {
                    double val = va_arg(args, double);

                    length += 8;
                    index++;

                    if(buf) {
                        rc = set_float64(buf, (double)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_F64 requires a double argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_BYTES:
                if(index < (arg_count - 2)) {
                    uint8_t *bytes = va_arg(args, uint8_t *);
                    int byte_count = va_arg(args, int);

                    length += byte_count;
                    index += 2;

                    if(buf) {
                        for(int i=0; i < byte_count && rc == PLCTAG_STATUS_OK; i++) {
                            rc = set_int8(buf, (int8_t)bytes[i]);
                        }
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_BYTES requires an uint8_t* argument and an integer count argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;
        }

        index++;
    }
    va_end(args);

    return length;
}



int bytebuf_unmarshal_impl(bytebuf_p buf, int arg_count, ...)
{
    int index = 0;
    int length = 0;
    int rc = PLCTAG_STATUS_OK;
    va_list args;

    if(arg_count < 1) {
        pdebug(DEBUG_WARN,"Insufficient arguments!");
        return PLCTAG_ERR_BAD_PARAM;
    }

    va_start(args, arg_count);
    index = 0;
    while(index < arg_count && rc == PLCTAG_STATUS_OK) {
        bytebuf_arg_type arg_type;

        arg_type = va_arg(args, bytebuf_arg_type);

        switch(arg_type) {
            case BB_I8:
                if(index < (arg_count - 1)) {
                    int8_t *val = va_arg(args, int8_t*);

                    length += 1;
                    index++;

                    if(buf) {
                        rc = get_int8(buf, (int8_t*)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_I8 requires an int8_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_U8:
                if(index < (arg_count - 1)) {
                    uint8_t *val = va_arg(args, uint8_t*);

                    length += 1;
                    index++;

                    if(buf) {
                        rc = get_int8(buf, (int8_t*)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_U8 requires an uint8_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_I16:
                if(index < (arg_count - 1)) {
                    int16_t *val = va_arg(args, int16_t*);

                    length += 2;
                    index++;

                    if(buf) {
                        rc = get_int16(buf, (int16_t*)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_I16 requires an int16_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_U16:
                if(index < (arg_count - 1)) {
                    uint16_t *val = va_arg(args, uint16_t*);

                    length += 2;
                    index++;

                    if(buf) {
                        rc = get_int16(buf, (int16_t*)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_U16 requires an uint16_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_I32:
                if(index < (arg_count - 1)) {
                    int32_t *val = va_arg(args, int32_t*);

                    length += 4;
                    index++;

                    if(buf) {
                        rc = get_int32(buf, (int32_t*)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_I32 requires an int32_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_U32:
                if(index < (arg_count - 1)) {
                    uint32_t *val = va_arg(args, uint32_t*);

                    length += 4;
                    index++;

                    if(buf) {
                        rc = get_int32(buf, (int32_t*)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_U32 requires an uint32_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_I64:
                if(index < (arg_count - 1)) {
                    int64_t *val = va_arg(args, int64_t*);

                    length += 8;
                    index++;

                    if(buf) {
                        rc = get_int64(buf, (int64_t*)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_I64 requires an int64_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_U64:
                if(index < (arg_count - 1)) {
                    uint64_t *val = va_arg(args, uint64_t*);

                    length += 8;
                    index++;

                    if(buf) {
                        rc = get_int64(buf, (int64_t*)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_U64 requires an uint64_t argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_F32:
                if(index < (arg_count - 1)) {
                    float *val = va_arg(args, float*);

                    length += 4;
                    index++;

                    if(buf) {
                        rc = get_float32(buf, (float*)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_F32 requires a float argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_F64:
                if(index < (arg_count - 1)) {
                    double *val = va_arg(args, double*);

                    length += 8;
                    index++;

                    if(buf) {
                        rc = get_float64(buf, (double*)val);
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_F64 requires a double argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;

            case BB_BYTES:
                if(index < (arg_count - 2)) {
                    uint8_t *bytes = va_arg(args, uint8_t *);
                    int byte_count = va_arg(args, int);

                    length += byte_count;
                    index += 2;

                    if(buf) {
                        for(int i=0; i < byte_count && rc == PLCTAG_STATUS_OK; i++) {
                            rc = get_int8(buf, (int8_t*)&bytes[i]);
                        }
                    }
                } else {
                    pdebug(DEBUG_WARN,"Arg %d of type BB_BYTES requires an uint8_t* argument and an integer count argument!", index);
                    rc = PLCTAG_ERR_BAD_PARAM;
                }

                break;
        }

        index++;
    }
    va_end(args);

    return length;
}





int bytebuf_get_size(bytebuf_p buf)
{
    pdebug(DEBUG_SPEW,"Starting.");

    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null or invalid reference!");
        return PLCTAG_ERR_NULL_PTR;
    }

    pdebug(DEBUG_SPEW,"Done size = %d", buf->size);

    return buf->size;
}



uint8_t *bytebuf_get_buffer(bytebuf_p buf)
{
    pdebug(DEBUG_SPEW,"Starting.");

    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null or invalid reference!");
        return NULL;
    }

    pdebug(DEBUG_SPEW, "Done.");

    return &buf->bytes[buf->cursor];
}



int bytebuf_reset(bytebuf_p buf)
{
    pdebug(DEBUG_SPEW,"Starting.");

    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null or invalid reference!");
        return PLCTAG_ERR_NULL_PTR;
    }

    buf->size = 0;
    buf->cursor = 0;

    mem_set(buf->bytes, 0, buf->capacity);

    pdebug(DEBUG_SPEW, "Done.");

    return PLCTAG_STATUS_OK;
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






int get_int8(bytebuf_p buf, int8_t *val)
{
    int64_t tmp;
    int rc;

    rc = get_int(buf, 1, 0x00, &tmp);
    if(rc != PLCTAG_STATUS_OK) {
        *val = 0;
        return rc;
    }

    *val = (int8_t)tmp;

    return PLCTAG_STATUS_OK;
}


int set_int8(bytebuf_p buf, int8_t val)
{
    int64_t tmp = (int64_t)val;

    return set_int(buf, 1, 0x00, tmp);
}





int get_int16(bytebuf_p buf, int16_t *val)
{
    int64_t tmp;
    int rc;

    if(!buf) {
        pdebug(DEBUG_WARN,"Buffer pointer is NULL!");
        return PLCTAG_ERR_NULL_PTR;
    }

    rc = get_int(buf, 2, buf->bo_int16, &tmp);
    if(rc != PLCTAG_STATUS_OK) {
        *val = 0;
        return rc;
    }

    *val = (int16_t)tmp;

    return PLCTAG_STATUS_OK;
}


int set_int16(bytebuf_p buf, int16_t val)
{
    int64_t tmp = (int64_t)val;

    if(!buf) {
        pdebug(DEBUG_WARN,"Buffer pointer is NULL!");
        return PLCTAG_ERR_NULL_PTR;
    }

    return set_int(buf, 2, buf->bo_int16, tmp);
}







int get_int32(bytebuf_p buf, int32_t *val)
{
    int64_t tmp;
    int rc;

    if(!buf) {
        pdebug(DEBUG_WARN,"Buffer pointer is NULL!");
        return PLCTAG_ERR_NULL_PTR;
    }

    rc = get_int(buf, 4, buf->bo_int32, &tmp);
    if(rc != PLCTAG_STATUS_OK) {
        *val = 0;
        return rc;
    }

    *val = (int32_t)tmp;

    return PLCTAG_STATUS_OK;
}


int set_int32(bytebuf_p buf, int32_t val)
{
    int64_t tmp = (int64_t)val;

    if(!buf) {
        pdebug(DEBUG_WARN,"Buffer pointer is NULL!");
        return PLCTAG_ERR_NULL_PTR;
    }

    return set_int(buf, 4, buf->bo_int32, tmp);
}






int get_int64(bytebuf_p buf, int64_t *val)
{
    int rc;

    if(!buf) {
        pdebug(DEBUG_WARN,"Buffer pointer is NULL!");
        return PLCTAG_ERR_NULL_PTR;
    }

    rc = get_int(buf, 8, buf->bo_int64, val);
    if(rc != PLCTAG_STATUS_OK) {
        *val = 0;
        return rc;
    }

    return PLCTAG_STATUS_OK;
}


int set_int64(bytebuf_p buf, int64_t val)
{
    if(!buf) {
        pdebug(DEBUG_WARN,"Buffer pointer is NULL!");
        return PLCTAG_ERR_NULL_PTR;
    }

    return set_int(buf, 8, buf->bo_int64, val);
}






int get_float32(bytebuf_p buf, float *val)
{
    int64_t tmp64 = 0;
    int32_t tmp32 = 0;
    int rc;

    if(!buf) {
        pdebug(DEBUG_WARN,"Buffer pointer is NULL!");
        return PLCTAG_ERR_NULL_PTR;
    }

    rc = get_int(buf, 4, buf->bo_float32, &tmp64);
    if(rc != PLCTAG_STATUS_OK) {
        *val = 0.0;
        return rc;
    }

    tmp32 = (int32_t)tmp64;

    mem_copy(val, &tmp32, (sizeof(*val) < sizeof(tmp32) ? sizeof(*val) : sizeof(tmp32)));

    return PLCTAG_STATUS_OK;
}

int set_float32(bytebuf_p buf, float val)
{
    int64_t tmp64 = 0;
    int32_t tmp32 = 0;

    if(!buf) {
        pdebug(DEBUG_WARN,"Buffer pointer is NULL!");
        return PLCTAG_ERR_NULL_PTR;
    }

    mem_copy(&tmp32, &val, (sizeof(val) < sizeof(tmp32) ? sizeof(val) : sizeof(tmp32)));

    tmp64 = (int64_t)tmp32;

    return set_int(buf, 4, buf->bo_float32, tmp64);

}







int get_float64(bytebuf_p buf, double *val)
{
    int64_t tmp64 = 0;
    int rc;

    if(!buf) {
        pdebug(DEBUG_WARN,"Buffer pointer is NULL!");
        return PLCTAG_ERR_NULL_PTR;
    }

    rc = get_int(buf, 8, buf->bo_float64, &tmp64);
    if(rc != PLCTAG_STATUS_OK) {
        *val = 0.0;
        return rc;
    }

    mem_copy(val, &tmp64, (sizeof(*val) < sizeof(tmp64) ? sizeof(*val) : sizeof(tmp64)));

    return PLCTAG_STATUS_OK;
}

int set_float64(bytebuf_p buf, double val)
{
    int64_t tmp64 = 0;

    if(!buf) {
        pdebug(DEBUG_WARN,"Buffer pointer is NULL!");
        return PLCTAG_ERR_NULL_PTR;
    }

    mem_copy(&tmp64, &val, (sizeof(val) < sizeof(tmp64) ? sizeof(val) : sizeof(tmp64)));

    return set_int(buf, 8, buf->bo_float64, tmp64);
}







#define BYTE_ORDER_VAL(bo, i) (((uint32_t)bo >> (i*4)) & (uint32_t)0xf)


int get_int(bytebuf_p buf, int size, uint32_t bo, int64_t *val)
{
    pdebug(DEBUG_SPEW,"Starting.");

    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null or invalid reference!");
        return PLCTAG_ERR_NULL_PTR;
    }

    /* make sure we have data for the read. */
    if(buf->cursor < 0 || (buf->cursor + size) > buf->size) {
        pdebug(DEBUG_ERROR, "Cursor out of bounds!");
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    /* start the process by zeroing out the value. */
    *val = 0;

    /* Start with the most significant byte. */
    for(int i = size-1; i >= 0; i--) {
        int index = buf->cursor + (int)BYTE_ORDER_VAL(bo,i);

        /* rotate the end value */
        *val = *val << 8;

        if(i == (size - 1)) {
            /* this is the sign byte. */
            *val = (int8_t)(buf->bytes[index]);
        } else {
            /* just regular bytes */
            *val = (int64_t)((uint64_t)(*val) | (uint64_t)(buf->bytes[index]));
        }
    }

    buf->cursor = buf->cursor + size;

    pdebug(DEBUG_SPEW,"Done.");

    return PLCTAG_STATUS_OK;
}


int set_int(bytebuf_p buf, int size, uint32_t bo, int64_t val)
{
    pdebug(DEBUG_SPEW,"Starting.");

    if(!buf) {
        pdebug(DEBUG_WARN,"Called with null or invalid reference!");
        return PLCTAG_ERR_NULL_PTR;
    }

    /* make sure we have the capacity to set this int */
    if(!bytebuf_set_capacity(buf, buf->cursor + size) == PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to expand byte buffer capacity!");
        return PLCTAG_ERR_NO_MEM;
    }

    /* Start with the least significant byte. */
    for(int i = 0; i < size; i++) {
        int index = buf->cursor + (int)BYTE_ORDER_VAL(bo,i);

        buf->bytes[index] = (uint8_t)((uint64_t)val & (uint64_t)0xFF);

        val = val >> 8;
    }

    buf->cursor = buf->cursor + size;

    if(buf->cursor > buf->size) {
        buf->size = buf->cursor;
    }

    pdebug(DEBUG_SPEW,"Done.");

    return PLCTAG_STATUS_OK;
}


