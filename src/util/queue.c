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
#include <util/queue.h>
#include <util/refcount.h>
#include <util/vector.h>


struct queue_t {
	vector_p vec;
};



static void queue_destroy(void *data);


queue_p queue_create(int capacity, int max_inc, rc_ref_type ref_type)
{
	queue_p result = NULL;

	if(capacity <= 0) {
		pdebug(DEBUG_WARN, "Called with negative capacity!");
		return NULL;
	}

	if(max_inc <= 0) {
		pdebug(DEBUG_WARN, "Called with negative maximum size increment!");
		return NULL;
	}

	result = rc_alloc(sizeof(struct queue_t), queue_destroy);
	if(!result) {
		pdebug(DEBUG_ERROR,"Unable to allocate memory for queue!");
		return NULL;
	}

	result->vec = vector_create(capacity, max_inc, ref_type);
	if(!result->vec) {
		pdebug(DEBUG_ERROR,"Unable to allocate memory for queue data!");
		rc_dec(result);
		return NULL;
	}

	return result;
}



int queue_length(queue_p q)
{
	int len;

	/*
	 * We grab a strong reference to the queue.   This ensures that it cannot
	 * disappear out from underneath us.  If that fails (perhaps because there
	 * are only weak refs), then we punt.
	 */
	if(!rc_inc(q)) {
		pdebug(DEBUG_WARN,"Null pointer or invalid pointer to queue passed!");
		return PLCTAG_ERR_NULL_PTR;
	}

	len = vector_length(q->vec);

	/* new we can release the ref */
	rc_dec(q);

	return len;
}



int queue_put(queue_p q, void *data)
{
	int rc = PLCTAG_STATUS_OK;
	
	/*
	 * We grab a strong reference to the queue.   This ensures that it cannot
	 * disappear out from underneath us.  If that fails (perhaps because there
	 * are only weak refs), then we punt.
	 */
	if(!rc_inc(q)) {
		pdebug(DEBUG_WARN,"Null pointer or invalid pointer to queue passed!");
		return PLCTAG_ERR_NULL_PTR;
	}

	/* we always add at the end. */
	rc = vector_put(q->vec, vector_length(q->vec), data);

	rc_dec(q);

	return rc;
}



void *queue_get(queue_p q)
{
	void *result = NULL;
	
	/*
	 * We grab a strong reference to the queue.   This ensures that it cannot
	 * disappear out from underneath us.  If that fails (perhaps because there
	 * are only weak refs), then we punt.
	 */
	if(!rc_inc(q)) {
		pdebug(DEBUG_WARN,"Null pointer or invalid pointer to queue passed!");
		return NULL;
	}

	/* pop the first element of the vector out */
	result = vector_remove(q->vec, 0);

	/* free the reference */
	rc_dec(q);

	return result;
}




/***********************************************************************
 *************** Private Helper Functions ******************************
 **********************************************************************/


void queue_destroy(void *data)
{
	queue_p q = data;

	/* note, no deref here!   We are called when refs are zero. */
	if(!q) {
		pdebug(DEBUG_WARN,"Null pointer to queue passed!");
		return;
	}

	/* release the reference to the vector */
	q->vec = rc_dec(q->vec);
}


