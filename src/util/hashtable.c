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
#include <util/debug.h>
#include <util/vector.h>



struct hashtable_t {
	int bucket_size;
	vector_p buckets;
};


struct hashtable_entry_t {
	void *data;
	int key_len;
	uint8_t key[]; /* use zero-length array trick to avoid allocation. */
};





static void hashtable_destroy(void *data);
static int find_entry(hashtable_p table, void *key, int key_len, uint32_t *bucket_index, vector_p *bucket, uint32_t *index, struct hashtable_entry_t **entry);





hashtable_p hashtable_create(int size, rc_ref_type ref_type)
{
	hashtable_p res = NULL;
	vector_p bucket = NULL;
	
	if(size <= 0) {
		pdebug(DEBUG_WARN,"Size is less than or equal to zero!");
		return NULL;
	}

	res = rc_alloc(sizeof(struct hashtable_t), hashtable_destroy);
	if(!res) {
		pdebug(DEBUG_ERROR,"Unable to allocate memory for hash table!");
		return NULL;
	}

	res->bucket_size = size;
	res->buckets = vector_create(size, 1, RC_STRONG_REF);
	if(!res->buckets) {
		pdebug(DEBUG_ERROR,"Unable to allocate memory for bucket vector!");
		rc_dec(res);
		return NULL;
	}

	for(int i=0; i < size; i++) {
		bucket = vector_create(5, 5, RC_STRONG_REF); /* FIXME - MAGIC */
		if(!bucket) {
			pdebug(DEBUG_ERROR,"Unable to allocate memory for bucket %d!",i);
			rc_dec(res);
			return NULL;
		} else {
			vector_put(res->buckets, i, bucket);
		}
	}

	return res;
}


void *hashtable_get(hashtable_p table, void *key, int key_len)
{
	uint32_t bucket_index = 0;
	vector_p bucket = NULL;
	uint32_t entry_index = 0;
	struct hashtable_entry_t *entry = NULL;
	void *result = NULL;
	int rc = PLCTAG_STATUS_OK;
	
	if(!rc_inc(table)) {
		pdebug(DEBUG_WARN,"Hashtable pointer null or invalid.");
		return NULL;
	}

	if(!key || key_len <=0) {
		pdebug(DEBUG_WARN,"Key missing or of zero length.");
		rc_dec(table);
		return NULL;
	}

	rc = find_entry(table, key, key_len, &bucket_index, &bucket, &entry_index, &entry)
	if(rc == PLCTAG_STATUS_OK) {
		/* found */
		result = rc_inc(entry->data);
	}

	rc_dec(table);

	return result;
}




int hashtable_put(hashtable_p table, void *key, int key_len, void *data)
{
	uint32_t bucket_index = 0;
	vector_p bucket = NULL;
	struct hashtable_entry_t *entry = NULL;
	int rc = PLCTAG_STATUS_OK;
	int first_hole = 0;
	
	if(!rc_inc(table)) {
		pdebug(DEBUG_WARN,"Hashtable pointer null or invalid.");
		return NULL;
	}

	if(!key || key_len <=0) {
		pdebug(DEBUG_WARN,"Key missing or of zero length.");
		rc_dec(table);
		return NULL;
	}

	/* get the right bucket */
	bucket_index = hash(key, key_len, table->bucket_size) % table->bucket_size;
	bucket = vector_get(table->buckets, bucket_index);
	if(!bucket) {
		pdebug(DEBUG_ERROR,"Bucket is NULL!");
		rc_dec(table);
		return NULL;
	}

	first_hole = vector_length(bucket);

	/* The use of "<=" is intentional to make sure that we can insert somewhere. */
	for(int i=0; rc == PLCTAG_STATUS_OK && i <= vector_length(bucket); i++) {
		entry = vector_get(bucket, i);
		if(entry) {
			if(entry_cmp(entry, key, key_len) == 0) {
				/*
				 * oops, collision.
				 *
				 * FIXME - This special case of NULL data has a bad code smell.
				 */
				if(data == NULL) {
					vector_put(bucket, i, NULL);
				} else {
					/* really is a collision */
					
				}
			}

			rc_dec(entry);
		}
		
	
}



int find_entry(hashtable_p table, void *key, int key_len, uint32_t *bucket_index, vector_p *bucket, uint32_t *index, struct hashtable_entry_t **entry)
{
	int rc = PLCTAG_ERR_NOT_FOUND;
	int found = 0;
	
	/* get the right bucket */
	*bucket_index = hash(key, key_len, table->bucket_size) % table->bucket_size;
	*bucket = vector_get(table->buckets, *bucket_index);
	if(!*bucket) {
		pdebug(DEBUG_ERROR,"Bucket is NULL!");
		return PLCTAG_ERR_NO_DATA;
	}

	/* find the entry */
	for(*index=0; rc==PLCTAG_ERR_NOT_FOUND && *index < vector_length(bucket); *index++) {
		*entry = vector_get(bucket, *index);
		if(*entry) {
			if(entry_cmp(*entry, key, key_len) == 0) {
				rc = PLCTAG_STATUS_OK;
			}

			rc_dec(entry);
		}
	}

	rc_dec(bucket);

	return rc;
}



int entry_cmp(struct hashtable_entry_t *entry, void *key, int key_len)
{
	if(!entry) {
		pdebug(DEBUG_WARN,"Bad entry");
		return -1;
	}

	if(!key || key_len <= 0) {
		pdebug(DEBUG_WARN,"Bad key");
		return -1;
	}

	return mem_cmp(&entry->key[0], entry->key_len, key, key_len);
}





void hashtable_destroy(void *arg)
{
	hashtable_p table = arg;

	if(table) {
		table->buckets = rc_dec(buckets);
	}
}



