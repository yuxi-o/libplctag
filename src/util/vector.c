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
	rc_ref_type ref_type;
	void **data;
	int len;
	int capacity;
	int max_inc;
};



static void vector_destroy(void *data);
static int ensure_capacity(vector_p vec, int capacity);


vector_p vector_create(int capacity, int max_inc, rc_ref_type ref_type)
{
	vector_p result = NULL;

	if(capacity <= 0) {
		pdebug(DEBUG_WARN, "Called with negative capacity!");
		return NULL;
	}

	if(max_inc <= 0) {
		pdebug(DEBUG_WARN, "Called with negative maximum size increment!");
		return NULL;
	}

	result = rc_alloc(sizeof(struct vector_t), vector_destroy);
	if(!result) {
		pdebug(DEBUG_ERROR,"Unable to allocate memory for vector!");
		return NULL;
	}

	result->data = mem_alloc(capacity * sizeof(void *));
	if(!result->data) {
		pdebug(DEBUG_ERROR,"Unable to allocate memory for vector data!");
		rc_dec(result);
		return NULL;
	}

	result->ref_type = ref_type;
	result->len = 0;
	result->capacity = capacity;
	result->max_inc = max_inc;

	return result;
}



int vector_length(vector_p vec)
{
	int len;

	/*
	 * We grab a strong reference to the vector.   This ensures that it cannot
	 * disappear out from underneath us.  If that fails (perhaps because there
	 * are only weak refs), then we punt.
	 */
	if(!rc_inc(vec)) {
		pdebug(DEBUG_WARN,"Null pointer or invalid pointer to vector passed!");
		return PLCTAG_ERR_NULL_PTR;
	}

	len = vec->len;

	/* new we can release the ref */
	rc_dec(vec);

	return len;
}



int vector_put(vector_p vec, int index, void *data)
{
	int rc = PLCTAG_STATUS_OK;
	
	/*
	 * We grab a strong reference to the vector.   This ensures that it cannot
	 * disappear out from underneath us.  If that fails (perhaps because there
	 * are only weak refs), then we punt.
	 */
	if(!rc_inc(vec)) {
		pdebug(DEBUG_WARN,"Null pointer or invalid pointer to vector passed!");
		return PLCTAG_ERR_NULL_PTR;
	}

	if(index < 0) {
		rc_dec(vec);
		pdebug(DEBUG_WARN,"Index is negative!");
		return PLCTAG_ERR_OUT_OF_BOUNDS;
	}

	rc = ensure_capacity(vec, index);
	if(rc != PLCTAG_STATUS_OK) {
		rc_dec(vec);
		pdebug(DEBUG_WARN,"Unable to ensure capacity!");
		return rc;
	}

	/* clear any references to existing data. */
	vec->data[index] = (vec->ref_type == RC_STRONG_REF ? rc_dec(vec->data[index]) : rc_weak_dec(vec->data[index]));

	/* acquire a reference to the new data. */
	vec->data[index] = (vec->ref_type == RC_STRONG_REF ? rc_inc(data) : rc_weak_inc(data));

    /* adjust the length, if needed */
	if(index >= vec->len) {
		vec->len = index+1;
	}

	/* now free the reference as we are done modifying the vector's memory */
	rc_dec(vec);

	return rc;
}


void *vector_get(vector_p vec, int index)
{
	void *result = NULL;
	
	/*
	 * We grab a strong reference to the vector.   This ensures that it cannot
	 * disappear out from underneath us.  If that fails (perhaps because there
	 * are only weak refs), then we punt.
	 */
	if(!rc_inc(vec)) {
		pdebug(DEBUG_WARN,"Null pointer or invalid pointer to vector passed!");
		return NULL;
	}

	if(index < 0 || index >= vec->len) {
		rc_dec(vec);
		pdebug(DEBUG_WARN,"Index is out of bounds!");
		return NULL;
	}

	/*
	 * This line does several things:
	 * 1. if the reference was weak and invalid, it sets it to NULL.
	 * 2. if the reference was valid, it gets a strong reference to it.
	 * Because of 1, it cleans up any invalid references as we get them. This
	 * sets the slot to NULL as a side effect.
	 */
	result = vec->data[index] = rc_inc(vec->data[index]);
	
	/* free the reference to the vector itself. */
	rc_dec(vec);

	return result;
}


void *vector_remove(vector_p vec, int index)
{
	void *result = NULL;

	/*
	 * We grab a strong reference to the vector.   This ensures that it cannot
	 * disappear out from underneath us.  If that fails (perhaps because there
	 * are only weak refs), then we punt.
	 */
	if(!rc_inc(vec)) {
		pdebug(DEBUG_WARN,"Null pointer or invalid pointer to vector passed!");
		return NULL;
	}

	if(index < 0 || index >= vec->len) {
		rc_dec(vec);
		pdebug(DEBUG_WARN,"Index is out of bounds!");
		return NULL;
	}

	/* We do not need to get a new reference.   This has move semantics.  */
	result = vec->data[index];

	/* move the rest of the data over this. */
	mem_move(&vec->data[index], &vec->data[index+1], sizeof(void*) * (vec->len - index - 1));

	/* make sure that we do not have bad data hanging around. */
	vec->data[vec->len - 1] = NULL;

	/* adjust the length to the new size */
	--vec->len;

	/* now that we are done mutating the vector, free the reference */
	rc_dec(vec);

	return result;
}



/***********************************************************************
 *************** Private Helper Functions ******************************
 **********************************************************************/


void vector_destroy(void *data)
{
	vector_p vec = data;

	/* note, no deref here!   We are called when refs are zero. */
	if(!vec) {
		pdebug(DEBUG_WARN,"Null pointer to vector passed!");
		return;
	}

	/* clear all the data out */
	for(int i=0; i < vec->len; i++) {
		vec->data[i] = (vec->ref_type == RC_STRONG_REF ? rc_dec(vec->data[i]) : rc_weak_dec(vec->data[i]));
	}

	mem_free(vec->data);

	vec->data = NULL;
}




int ensure_capacity(vector_p vec, int capacity)
{
	int new_inc = 0;
	void **new_data = NULL;

	/* this is a bit tricky, get a strong reference to the vector.
	 * If we get a NULL back, then we probably only had weak refs to it
	 * and we should punt because the vector is gone.
	 */
	if(!rc_inc(vec)) {
		pdebug(DEBUG_WARN,"Null pointer or invalid pointer to vector passed!");
		return PLCTAG_ERR_NULL_PTR;
	}

	/* is there anything to do? */
	if(capacity <= vec->capacity) {
		/* release the reference */
		rc_dec(vec);
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
	new_data = (void **)mem_alloc(vec->capacity + new_inc);
	if(!new_data) {
		pdebug(DEBUG_ERROR,"Unable to allocate new data area!");
		return PLCTAG_ERR_NO_MEM;
	}

	mem_copy(new_data, vec->data, vec->capacity);

	mem_free(vec->data);

	vec->data = new_data;
	vec->capacity += new_inc;

	return PLCTAG_STATUS_OK;
}


