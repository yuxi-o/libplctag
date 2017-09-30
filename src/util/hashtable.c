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
#include <util/hash.h>
#include <util/hashtable.h>
#include <util/vector.h>



struct hashtable_t {
	rc_ref_type ref_type;
	int bucket_size;
	vector_p buckets;
};


struct hashtable_entry_t {
	rc_ref_type ref_type;
	void *data;
	int key_len;
	uint8_t key[]; /* use zero-length array trick to avoid allocation. */
};





static void hashtable_destroy(void *data);
static int find_entry(hashtable_p table, void *key, int key_len, uint32_t *bucket_index, vector_p *bucket, uint32_t *index, struct hashtable_entry_t **entry);
static int entry_cmp(struct hashtable_entry_t *entry, void *key, int key_len);
struct hashtable_entry_t *hashtable_entry_create(void *key, int key_len, void *data, rc_ref_type ref_type);
void hashtable_entry_destroy(void *arg);





hashtable_p hashtable_create(int size, rc_ref_type ref_type)
{
	hashtable_p res = NULL;
	vector_p bucket = NULL;

	pdebug(DEBUG_INFO,"Starting");
	
	if(size <= 0) {
		pdebug(DEBUG_WARN,"Size is less than or equal to zero!");
		return NULL;
	}

	res = rc_alloc(sizeof(struct hashtable_t), hashtable_destroy);
	if(!res) {
		pdebug(DEBUG_ERROR,"Unable to allocate memory for hash table!");
		return NULL;
	}

	res->ref_type = ref_type;
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

	pdebug(DEBUG_INFO,"Done");
	
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

	pdebug(DEBUG_INFO,"Starting");
	
	if(!rc_inc(table)) {
		pdebug(DEBUG_WARN,"Hashtable pointer null or invalid.");
		return NULL;
	}

	if(!key || key_len <=0) {
		pdebug(DEBUG_WARN,"Key missing or of zero length.");
		rc_dec(table);
		return NULL;
	}

	rc = find_entry(table, key, key_len, &bucket_index, &bucket, &entry_index, &entry);
	if(rc == PLCTAG_STATUS_OK) {
		/* found */
		result = rc_inc(entry->data);
	}

	rc_dec(table);

	pdebug(DEBUG_INFO,"Done");
	
	return result;
}




int hashtable_put(hashtable_p table, void *key, int key_len, void *data)
{
	uint32_t bucket_index = 0;
	vector_p bucket = NULL;
	uint32_t entry_index = 0;
	struct hashtable_entry_t *entry = NULL;
	int rc = PLCTAG_STATUS_OK;

	pdebug(DEBUG_INFO,"Starting");
	
	if(!rc_inc(table)) {
		pdebug(DEBUG_WARN,"Hashtable pointer null or invalid.");
		return PLCTAG_ERR_NULL_PTR;
	}

	if(!key || key_len <=0) {
		pdebug(DEBUG_WARN,"Key missing or of zero length.");
		rc_dec(table);
		return PLCTAG_ERR_OUT_OF_BOUNDS;
	}

	rc = find_entry(table, key, key_len, &bucket_index, &bucket, &entry_index, &entry);
	if(rc == PLCTAG_ERR_NOT_FOUND) {
		/* not found, this is good. */

		/* create a new entry and insert it into the bucket. */
		struct hashtable_entry_t *new_entry = hashtable_entry_create(key, key_len, data, table->ref_type);
		if(!new_entry) {
			pdebug(DEBUG_ERROR,"Unable to allocate new hashtable entry!");
			rc = PLCTAG_ERR_NO_MEM;
		} else {
			/* add this to the bucket vector. */
			vector_put(bucket, vector_length(bucket), new_entry);

			/* remove extra strong reference.   Adding the entry to the vector creates another one. */
			rc_dec(new_entry);

			rc = PLCTAG_STATUS_OK;
		}
	}

	pdebug(DEBUG_INFO,"Done");

	rc_dec(table);

	return rc;
}



int hashtable_remove(hashtable_p table, void *key, int key_len)
{
	uint32_t bucket_index = 0;
	vector_p bucket = NULL;
	uint32_t entry_index = 0;
	struct hashtable_entry_t *entry = NULL;
	int rc = PLCTAG_STATUS_OK;

	pdebug(DEBUG_INFO,"Starting");
	
	if(!rc_inc(table)) {
		pdebug(DEBUG_WARN,"Hashtable pointer null or invalid.");
		return PLCTAG_ERR_NULL_PTR;
	}

	if(!key || key_len <=0) {
		pdebug(DEBUG_WARN,"Key missing or of zero length.");
		rc_dec(table);
		return PLCTAG_ERR_OUT_OF_BOUNDS;
	}

	rc = find_entry(table, key, key_len, &bucket_index, &bucket, &entry_index, &entry);
	if(rc == PLCTAG_STATUS_OK) {
		/* the entry was found. */
		entry = vector_remove(bucket, entry_index);
		entry = rc_dec(entry);
	}

	rc_dec(table);

	pdebug(DEBUG_INFO,"Done");

	return rc;
}



/***********************************************************************
 *************************** Helper Functions **************************
 **********************************************************************/




void hashtable_destroy(void *arg)
{
	hashtable_p table = arg;

	pdebug(DEBUG_INFO,"Starting");

	if(table) {
		table->buckets = rc_dec(table->buckets);
	}

	pdebug(DEBUG_INFO,"Done");
}






int find_entry(hashtable_p table, void *key, int key_len, uint32_t *bucket_index, vector_p *bucket, uint32_t *index, struct hashtable_entry_t **entry)
{
	int rc = PLCTAG_ERR_NOT_FOUND;

	pdebug(DEBUG_INFO,"Starting");
	
	/* get the right bucket */
	*bucket_index = hash(key, key_len, table->bucket_size) % table->bucket_size;
	*bucket = vector_get(table->buckets, *bucket_index);
	if(!*bucket) {
		pdebug(DEBUG_ERROR,"Bucket is NULL!");
		return PLCTAG_ERR_NO_DATA;
	}

	/* find the entry */
	for(*index=0; rc==PLCTAG_ERR_NOT_FOUND && (int)(*index) < vector_length(*bucket); *index = *index + 1) {
		*entry = vector_get(*bucket, *index);
		if(*entry) {
			if(entry_cmp(*entry, key, key_len) == 0) {
				rc = PLCTAG_STATUS_OK;
			}

			rc_dec(*entry);
		}
	}

	rc_dec(*bucket);

	pdebug(DEBUG_INFO,"Done.");

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



struct hashtable_entry_t *hashtable_entry_create(void *key, int key_len, void *data, rc_ref_type ref_type)
{
	struct hashtable_entry_t *new_entry = rc_alloc(sizeof(struct hashtable_entry_t) + key_len, hashtable_entry_destroy);
	
	if(!new_entry) {
		pdebug(DEBUG_ERROR,"Unable to allocate new hashtable entry!");
	} else {
		new_entry->data = (ref_type == RC_STRONG_REF ? rc_inc(data) : rc_weak_inc(data));
		new_entry->ref_type = ref_type;
		new_entry->key_len = key_len;
		mem_copy((uint8_t *)(new_entry + 1), key, key_len);
		pdebug(DEBUG_INFO,"Done creating new hashtable entry.");
	}

	return new_entry;
}


void hashtable_entry_destroy(void *arg)
{
	struct hashtable_entry_t *entry = arg;

	pdebug(DEBUG_INFO,"Starting");

	if(!entry) {
		pdebug(DEBUG_WARN,"Invalid reference passed!");
	} else {
		entry->data = (entry->ref_type == RC_STRONG_REF ? rc_dec(entry->data) : rc_weak_dec(entry->data));
	}

	pdebug(DEBUG_INFO,"Done");
}

