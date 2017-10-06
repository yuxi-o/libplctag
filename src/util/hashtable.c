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
    int bucket_size;
    vector_ref buckets;
};


struct hashtable_entry_t {
    rc_ref data_ref;
    int key_len;
    void *key;
};


typedef struct hashtable_t *hashtable_p;


RC_MAKE_TYPE(hashtable_entry_ref);
//~ typedef struct { int *counter; } hashtable_entry_ref;

static void hashtable_destroy(void *data);
static int find_entry(hashtable_p table, void *key, int key_len, uint32_t *bucket_index, vector_ref *bucket, uint32_t *index, hashtable_entry_ref *entry_ref);
static int entry_cmp(hashtable_entry_ref entry_ref, void *key, int key_len);
static hashtable_entry_ref hashtable_entry_create(void *key, int key_len, rc_ref data_ref);
void hashtable_entry_destroy(void *arg);





hashtable_ref hashtable_create(int size)
{
    hashtable_p tab = NULL;
    vector_ref bucket = RC_VECTOR_NULL;
    hashtable_ref res = RC_HASHTABLE_NULL;

    pdebug(DEBUG_INFO,"Starting");

    if(size <= 0) {
        pdebug(DEBUG_WARN,"Size is less than or equal to zero!");
        return res;
    }

    tab = mem_alloc(sizeof(struct hashtable_t));
    if(!tab) {
        pdebug(DEBUG_ERROR,"Unable to allocate memory for hash table!");
        return res;
    }

    tab->bucket_size = size;
    tab->buckets = vector_create(size, 1);
    if(!rc_deref(tab->buckets)) {
        pdebug(DEBUG_ERROR,"Unable to allocate memory for bucket vector!");
        hashtable_destroy(tab);
        return res;
    }

    for(int i=0; i < size; i++) {
        bucket = vector_create(5, 5); /* FIXME - MAGIC */
        if(!rc_deref(bucket)) {
            pdebug(DEBUG_ERROR,"Unable to allocate memory for bucket %d!",i);
            hashtable_destroy(tab);
            return res;
        } else {
            /* store the bucket. */
            vector_put(tab->buckets, i, bucket);
        }
    }

    res = RC_CAST(hashtable_ref, rc_make_ref(tab, hashtable_destroy));

    pdebug(DEBUG_INFO,"Done");

    return res;
}


rc_ref hashtable_get(hashtable_ref tab_ref, void *key, int key_len)
{
    uint32_t bucket_index = 0;
    vector_ref bucket = RC_VECTOR_NULL;
    uint32_t entry_index = 0;
    hashtable_entry_ref entry_ref;
    struct hashtable_entry_t *entry = NULL;
    rc_ref result = RC_REF_NULL;
    int rc = PLCTAG_STATUS_OK;
    hashtable_p table;

    pdebug(DEBUG_INFO,"Starting");

    if(!(table = rc_deref(tab_ref))) {
        pdebug(DEBUG_WARN,"Hashtable pointer null or invalid.");
        return result;
    }

    if(!key || key_len <=0) {
        pdebug(DEBUG_WARN,"Key missing or of zero length.");
        return result;
    }

    rc = find_entry(table, key, key_len, &bucket_index, &bucket, &entry_index, &entry_ref);
    if(rc == PLCTAG_STATUS_OK) {
        /* found */
        if((entry = rc_deref(entry_ref))) {
            result = entry->data_ref;
        }

        /* clean up empty slots */
        if(!entry || !rc_deref(result)) {
            hashtable_remove(tab_ref, key, key_len);
        }
    }

    pdebug(DEBUG_INFO,"Done");

    return result;
}




int hashtable_put_impl(hashtable_ref tab_ref, void *key, int key_len, rc_ref data_ref)
{
    uint32_t bucket_index = 0;
    vector_ref bucket = RC_VECTOR_NULL;
    uint32_t entry_index = 0;
    hashtable_entry_ref entry_ref;
    int rc = PLCTAG_STATUS_OK;
    hashtable_p table;

    pdebug(DEBUG_INFO,"Starting");

    if(!(table = rc_deref(tab_ref))) {
        pdebug(DEBUG_WARN,"Hashtable pointer null or invalid.");
        return PLCTAG_ERR_NULL_PTR;
    }

    if(!key || key_len <=0) {
        pdebug(DEBUG_WARN,"Key missing or of zero length.");
        return PLCTAG_ERR_OUT_OF_BOUNDS;
    }

    rc = find_entry(table, key, key_len, &bucket_index, &bucket, &entry_index, &entry_ref);
    if(rc == PLCTAG_ERR_NOT_FOUND) {
        /* not found, this is good. */

        /* create a new entry and insert it into the bucket. */
        entry_ref = hashtable_entry_create(key, key_len, data_ref);
        if(!rc_deref(entry_ref)) {
            pdebug(DEBUG_ERROR,"Unable to allocate new hashtable entry!");
            rc = PLCTAG_ERR_NO_MEM;
        } else {
            /*
             * Add this to the bucket vector.
             * Put it in the first empty entry.  Note the use of "<=" for the guard.  This ensures
             * that we insert at the end of the vector if there are no empty slots.
             */

            rc = PLCTAG_ERR_NOT_FOUND;

            for(entry_index = 0; (int)entry_index <= vector_length(bucket); entry_index++) {
                hashtable_entry_ref tmp = RC_CAST(hashtable_entry_ref, vector_get(bucket, entry_index));

                if(!rc_deref(tmp)) {
                    /* found a hole. */
                    rc = vector_put(bucket, entry_index, entry_ref);

                    break;
                }
            }
        }
    }

    pdebug(DEBUG_INFO,"Done");

    return rc;
}



rc_ref hashtable_remove(hashtable_ref table_ref, void *key, int key_len)
{
    uint32_t bucket_index = 0;
    vector_ref bucket = RC_VECTOR_NULL;
    uint32_t entry_index = 0;
    hashtable_entry_ref entry_ref;
    struct hashtable_entry_t *entry = NULL;
    int rc = PLCTAG_STATUS_OK;
    hashtable_p table = NULL;
    rc_ref result = RC_REF_NULL;

    pdebug(DEBUG_INFO,"Starting");

    if(!(table = rc_deref(table_ref))) {
        pdebug(DEBUG_WARN,"Hashtable pointer null or invalid.");
        return result;
    }

    if(!key || key_len <=0) {
        pdebug(DEBUG_WARN,"Key missing or of zero length.");
        return result;
    }

    rc = find_entry(table, key, key_len, &bucket_index, &bucket, &entry_index, &entry_ref);
    if(rc == PLCTAG_STATUS_OK) {
        entry_ref = RC_CAST(hashtable_entry_ref, vector_remove(bucket, entry_index));

        /* the entry was found. */
        entry = rc_deref(entry_ref);

        if(entry) {
            result = entry->data_ref;

            /* clear out the data reference so that it does not get cleaned up. */
            entry->data_ref = RC_REF_NULL;
        }

        /* clean up the entry */
        rc_release(entry_ref);
    }

    pdebug(DEBUG_INFO,"Done");

    return result;
}



/***********************************************************************
 *************************** Helper Functions **************************
 **********************************************************************/




void hashtable_destroy(void *arg)
{
    hashtable_p table = arg;

    pdebug(DEBUG_INFO,"Starting");

    if(table) {
        rc_release(table->buckets);
        mem_free(table);
    }

    pdebug(DEBUG_INFO,"Done");
}






int find_entry(hashtable_p table, void *key, int key_len, uint32_t *bucket_index, vector_ref *bucket, uint32_t *index, hashtable_entry_ref *entry)
{
    int rc = PLCTAG_ERR_NOT_FOUND;

    pdebug(DEBUG_INFO,"Starting");

    /* get the right bucket */
    *bucket_index = hash(key, key_len, table->bucket_size) % table->bucket_size;
    *bucket = RC_CAST(vector_ref, vector_get(table->buckets, *bucket_index));
    if(!rc_deref(*bucket)) {
        pdebug(DEBUG_ERROR,"Bucket is NULL!");
        return PLCTAG_ERR_NO_DATA;
    }

    /* find the entry */
    for(*index=0; rc==PLCTAG_ERR_NOT_FOUND && (int)(*index) < vector_length(*bucket); *index = *index + 1) {
        *entry = RC_CAST(hashtable_entry_ref, vector_get(*bucket, *index));
        if(rc_deref(*entry)) {
            if(entry_cmp(*entry, key, key_len) == 0) {
                rc = PLCTAG_STATUS_OK;
                break; /* punt out and do not increment index. */
            }
        }
    }

    pdebug(DEBUG_INFO,"Done.");

    return rc;
}



int entry_cmp(hashtable_entry_ref entry_ref, void *key, int key_len)
{
    struct hashtable_entry_t *entry = NULL;

    if(!(entry = rc_deref(entry_ref))) {
        pdebug(DEBUG_WARN,"Bad entry");
        return -1;
    }

    if(!key || key_len <= 0) {
        pdebug(DEBUG_WARN,"Bad key");
        return -1;
    }

    return mem_cmp(entry->key, entry->key_len, key, key_len);
}



hashtable_entry_ref hashtable_entry_create(void *key, int key_len, rc_ref data_ref)
{
    struct hashtable_entry_t *new_entry = mem_alloc(sizeof(struct hashtable_entry_t) + key_len);
    hashtable_entry_ref result = RC_CAST(hashtable_entry_ref, RC_REF_NULL);

    if(!new_entry) {
        pdebug(DEBUG_ERROR,"Unable to allocate new hashtable entry!");
    } else {
        new_entry->data_ref = data_ref;
        new_entry->key_len = key_len;
        new_entry->key = (void *)(new_entry + 1);
        mem_copy(new_entry->key, key, key_len);
        pdebug(DEBUG_INFO,"Done creating new hashtable entry.");

        result = RC_CAST(hashtable_entry_ref, rc_make_ref(new_entry, hashtable_entry_destroy));

        /* did it work? */
        if(!rc_deref(result)) {
            /* nope.   Clean up.
             * FIXME - will this result in a double free? */
            mem_free(new_entry);
        }
    }

    return result;
}


void hashtable_entry_destroy(void *arg)
{
    struct hashtable_entry_t *entry = arg;

    pdebug(DEBUG_INFO,"Starting");

    if(!entry) {
        pdebug(DEBUG_WARN,"Invalid reference passed!");
    } else {
        rc_release(entry->data_ref);
        mem_free(entry);
    }

    pdebug(DEBUG_INFO,"Done");
}

