/***************************************************************************
 *   Copyright (C) 2017 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
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
#include <lib/libplctag.h>
#include <util/debug.h>
#include <util/vector.h>
#include <util/pt.h>
#include <util/refcount.h>



typedef enum { WAITING, RUNNING, DEAD } pt_state;

struct protothread_t {
    pt_state state;
    int pt_line;
    const char *calling_function;
    int calling_line_num;
    const char *function_name;
    int arg_count;
    void** args;
    pt_func func;
};


static volatile vector_p pt_list = NULL;

/* global PT mutex protecting the PT list. */
static volatile mutex_p global_pt_mut = NULL;

/* pt runner thread */
static volatile thread_p pt_thread = NULL;
#ifdef _WIN32
DWORD __stdcall pt_runner(LPVOID not_used);
#else
void* pt_runner(void* not_used);
#endif


static volatile int library_terminating = 0;



static void protothread_destroy(void *);


/*
 * Library initialization point for protothreads.
 */


int pt_service_init(void)
{
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO,"Initializing Protothread utility.");

    /* this is a mutex used to synchronize most activities in this protocol */
    rc = mutex_create((mutex_p*)&global_pt_mut);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to create global tag mutex!");
        return rc;
    }

    /* create the vector in which we will have the PTs stored. */
    pt_list = vector_create(100, 50);
    if(!pt_list ) {
        pdebug(DEBUG_ERROR,"Unable to allocate a vector!");
        return PLCTAG_ERR_CREATE;
    }

    /* create the background PT runner thread */
    rc = thread_create((thread_p*)&pt_thread, pt_runner, 32*1024, NULL);
    if(rc != PLCTAG_STATUS_OK) {
        vector_destroy(pt_list);
        pdebug(DEBUG_INFO,"Unable to create PT runner thread!");
        return rc;
    }

    pdebug(DEBUG_INFO,"Finished initializing Protothread utility.");

    return rc;
}


/*
 * called when the whole program is going to terminate.
 */
void pt_service_teardown(void)
{
    pdebug(DEBUG_INFO,"Releasing global Protothread resources.");

    pdebug(DEBUG_INFO,"Terminating PT thread.");
    /* kill the IO thread first. */
    library_terminating = 1;

    /* wait for the thread to die */
    thread_join(pt_thread);
    thread_destroy((thread_p*)&pt_thread);

    pdebug(DEBUG_INFO,"Freeing PT list.");
    /* free the vector of PTs */
    vector_destroy(pt_list);

    pdebug(DEBUG_INFO,"Freeing global PT mutex.");
    /* clean up the mutex */
    mutex_destroy((mutex_p*)&global_pt_mut);

    pdebug(DEBUG_INFO,"Finished tearing down Protothread utility resources.");
}



protothread_p pt_create_impl(const char *calling_function, int calling_line_num, const char *func_name, pt_func func, int arg_count, ...)
{
    protothread_p result = NULL;
    va_list va;
    protothread_p entry = mem_alloc(sizeof(struct protothread_t));
    int status = PLCTAG_STATUS_OK;

    if(!entry) {
        pdebug(DEBUG_ERROR,"Cannot allocate new pt entry!");
        return result;
    }

    entry->state = WAITING;
    entry->func = func;
    entry->calling_function = calling_function;
    entry->calling_line_num = calling_line_num;
    entry->function_name = func_name;
    entry->arg_count = arg_count;
    entry->args = mem_alloc(arg_count * sizeof(void*));

    if(!entry->args) {
        pdebug(DEBUG_ERROR,"Unable to allocate argument array!");
        mem_free(entry);
        return result;
    }

    va_start(va, arg_count);
    for(int i=0; i < arg_count; i++) {
        void *arg = va_arg(va, void *);

        /* this handles only ref counted data! */
        entry->args[i] = rc_inc(arg);
    }
    va_end(va);

    result = rc_make_ref(entry, protothread_destroy);

    if(!result) {
        pdebug(DEBUG_WARN,"Unable to make PT entry!");
        protothread_destroy(entry);
        return result;
    }

    critical_block(global_pt_mut) {
        /* just insert at the end.   Insert a weak reference. */
        if((status = vector_put(pt_list, vector_length(pt_list), result)) != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_ERROR,"Unable to insert new protothread into list!");
        }
    }

    /* if we did not insert it into the pt list successfully, punt. */
    if(status != PLCTAG_STATUS_OK) {
        result = rc_dec(result);
    }

    return result;
}




#ifdef _WIN32
DWORD __stdcall pt_runner(LPVOID not_used)
#else
void* pt_runner(void* not_used)
#endif
{
    pdebug(DEBUG_INFO,"PT Runner starting with arg %p",not_used);

    while(!library_terminating) {
        /* scan over all the entries looking for one that is valid and not running. */
        int index = 0;
        int done = 0;

        do {
            protothread_p pt = NULL;

            done = 1;

            /* find a protothread that is ready to run. */
            critical_block(global_pt_mut) {
                for(int i=index; i < vector_length(pt_list); i++) {
                    /* get a reference */
                    pt = rc_inc(vector_get(pt_list,i));

                    /* if it is dead or an invalid reference, then remove it. */
                    if(!pt) {
                        pdebug(DEBUG_INFO,"protothread %d is invalid.", i);
                        vector_remove(pt_list, i);
                        i--; /* the list changed size.*/

                        continue;
                    }

                    if(pt && pt->state == WAITING) {
                        /* yes, so mark it as running to prevent other threads from changing it. */
                        pt->state = RUNNING;
                        index = i;
                        done = 0;

                        break;
                    }
                }
            }

            if(pt) {
                pdebug(DEBUG_DETAIL,"Calling function %s created in function %s at line %d", pt->function_name, pt->calling_function, pt->calling_line_num);
                if(pt->func(&pt->pt_line, pt->arg_count, pt->args) == PT_TERMINATE) {
                    pt->state = DEAD;
                }

                /* do not rerun the PT again. */
                index++;
            }
        } while(!done);

        /* reschedule the CPU. */
        sleep_ms(1);
    }

    pdebug(DEBUG_DETAIL,"PT Runner thread function exiting.");

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}




void protothread_destroy(void *arg)
{
    protothread_p entry = arg;

    if(!entry) {
        pdebug(DEBUG_WARN,"Null pointer passed in!");
        return;
    }

    pdebug(DEBUG_INFO,"Destroying protothread with function %s created in function %s at line %d", entry->function_name, entry->calling_function, entry->calling_line_num);

    /* remove the entry from the list. */
    critical_block(global_pt_mut) {
        for(int i=0; i < vector_length(pt_list); i++) {
            protothread_p tmp = vector_get(pt_list, i);

            if(tmp == entry) {
                vector_remove(pt_list, i);
                break;
            }
        }
    }

    for(int i=0; i < entry->arg_count; i++) {
        void *arg = entry->args[i];
        rc_dec(arg);
    }

    mem_free(entry->args);

    mem_free(entry);
}
