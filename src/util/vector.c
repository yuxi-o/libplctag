/***************************************************************************
 *   Copyright (C) 2017 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library/Lesser General Public License as*
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


#include <lib/libplctag.h>
#include <platform.h>
#include <util/refcount.h>
#include <util/debug.h>
#include <util/refcount.h>
#include <util/vector.h>


struct vector_t {
    rc_ref *data;
    int len;
    int capacity;
    int max_inc;
};


typedef struct vector_t *vector_p;



static void vector_destroy(void *data);
static int ensure_capacity(vector_p vec, int capacity);


vector_ref vector_create(int capacity, int max_inc)
{
    vector_p vec = NULL;
    vector_ref result = RC_VECTOR_NULL;

    pdebug(DEBUG_INFO,"Starting");

    if(capacity <= 0) {
        pdebug(DEBUG_WARN, "Called with negative capacity!");
        return result;
    }

    if(max_inc <= 0) {
        pdebug(DEBUG_WARN, "Called with negative maximum size increment!");
        return result;
    }

    vec = mem_alloc(sizeof(struct vector_t));
    if(!vec) {
        pdebug(DEBUG_ERROR,"Unable to allocate memory for vector!");
        return result;
    }

    vec->len = 0;
    vec->capacity = capacity;
    vec->max_inc = max_inc;

    vec->data = mem_alloc(capacity * sizeof(rc_ref));
    if(!vec->data) {
        pdebug(DEBUG_ERROR,"Unable to allocate memory for vector data!");
        vector_destroy(vec);
        return result;
    }

    /* make a ref counted wrapper */
    result = RC_CAST(vector_ref, rc_make_ref(vec, vector_destroy));
    if(!rc_deref(result)) {
        pdebug(DEBUG_WARN,"Unable to create ref wrapper!");
        vector_destroy(vec);
    }

    pdebug(DEBUG_INFO,"Done");

    return result;
}



int vector_length(vector_ref vec_ref)
{
    int len;
    vector_p vec;

    pdebug(DEBUG_DETAIL,"Starting");

    /* check to see if the vector ref is valid */
    if(!(vec = rc_deref(RC_CAST(rc_ref, vec_ref)))) {
        pdebug(DEBUG_WARN,"Null pointer or invalid pointer to vector passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    len = vec->len;

    pdebug(DEBUG_DETAIL,"Done");

    return len;
}



int vector_put_impl(vector_ref vec_ref, int index, rc_ref data_ref)
{
    vector_p vec;
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO,"Starting");

    /* check to see if the vector ref is valid */
    if(!(vec = rc_deref(vec_ref))) {
       pdebug(DEBUG_WARN,"Null pointer or invalid pointer to vector passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(index < 0) {
        pdebug(DEBUG_WARN,"Index is negative!");
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    rc = ensure_capacity(vec, index+1);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to ensure capacity!");
        return rc;
    }

    /* clear any references to existing data. */
    rc_release(vec->data[index]);

    /* acquire a reference to the new data. */
    vec->data[index] = data_ref;

    /* adjust the length, if needed */
    if(index >= vec->len) {
        vec->len = index+1;
    }

    pdebug(DEBUG_INFO,"Done");

    return rc;
}


rc_ref vector_get(vector_ref vec_ref, int index)
{
    vector_p vec;

    pdebug(DEBUG_INFO,"Starting");

    /* check to see if the vector ref is valid */
    if(!(vec = rc_deref(vec_ref))) {
        pdebug(DEBUG_WARN,"Null pointer or invalid pointer to vector passed!");
        return RC_REF_NULL;
    }

    if(index < 0 || index >= vec->len) {
        pdebug(DEBUG_WARN,"Index is out of bounds!");
        return RC_REF_NULL;
    }

    pdebug(DEBUG_INFO,"Done");

    return vec->data[index];
}


rc_ref vector_remove(vector_ref vec_ref, int index)
{
    vector_p vec;
    rc_ref result = RC_REF_NULL;

    pdebug(DEBUG_INFO,"Starting");

    /* check to see if the vector ref is valid */
    if(!(vec = rc_deref(vec_ref))) {
        pdebug(DEBUG_WARN,"Null pointer or invalid pointer to vector passed!");
        return RC_REF_NULL;
    }

    if(index < 0 || index >= vec->len) {
        pdebug(DEBUG_WARN,"Index is out of bounds!");
        return RC_REF_NULL;
    }

    /* get the value in this slot before we overwrite it. */
    result = vec->data[index];

    /* move the rest of the data over this. */
    mem_move(&vec->data[index], &vec->data[index+1], sizeof(rc_ref) * (vec->len - index - 1));

    /* make sure that we do not have bad data hanging around. */
    vec->data[vec->len - 1] = RC_REF_NULL;

    /* adjust the length to the new size */
    vec->len--;

    pdebug(DEBUG_INFO,"Done");

    return result;
}



/***********************************************************************
 *************** Private Helper Functions ******************************
 **********************************************************************/


void vector_destroy(void *data)
{
    vector_p vec = data;

    if(!vec) {
        pdebug(DEBUG_WARN,"Null pointer to vector passed!");
        return;
    }

    /* clear all the data out */
    for(int i=0; i < vec->len; i++) {
        vec->data[i] = rc_release(vec->data[i]);
    }

    mem_free(vec->data);

    vec->data = NULL;

    mem_free(vec);
}




int ensure_capacity(vector_p vec, int capacity)
{
    int new_inc = 0;
    rc_ref *new_data = NULL;

    if(!vec) {
        pdebug(DEBUG_WARN,"Null pointer or invalid pointer to vector passed!");
        return PLCTAG_ERR_NULL_PTR;
    }

    /* is there anything to do? */
    if(capacity <= vec->capacity) {
        /* release the reference */
        return PLCTAG_STATUS_OK;
    }

    /* calculate the new capacity
     *
     * Start by guessing 50% larger.  Clamp that against 1 at the
     * low end and the max increment passed when the vector was created.
     */
    new_inc = vec->capacity / 2;

    if(new_inc > vec->max_inc) {
        new_inc = vec->max_inc;
    }

    if(new_inc < 1) {
        new_inc = 1;
    }

    /* allocate the new data area */
    new_data = (rc_ref *)mem_alloc(sizeof(rc_ref) * (vec->capacity + new_inc));
    if(!new_data) {
        pdebug(DEBUG_ERROR,"Unable to allocate new data area!");
        return PLCTAG_ERR_NO_MEM;
    }

    mem_copy(new_data, vec->data, vec->capacity * sizeof(rc_ref));

    mem_free(vec->data);

    vec->data = new_data;

    vec->capacity += new_inc;

    return PLCTAG_STATUS_OK;
}


