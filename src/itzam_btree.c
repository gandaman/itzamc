/*
    Itzam/C (version 6.0) is an embedded database engine written in Standard C.

    Copyright 2011 Scott Robert Ladd. All rights reserved.

    Older versions of Itzam/C are:
        Copyright 2002, 2004, 2006, 2008 Scott Robert Ladd. All rights reserved.

    Ancestral code, from Java and C++ books by the author, is:
        Copyright 1992, 1994, 1996, 2001 Scott Robert Ladd.  All rights reserved.

    Itzam/C is user-supported open source software. It's continued development is dependent on
    financial support from the community. You can provide funding by visiting the Itzam/C
    website at:

        http://www.coyotegulch.com

    You may license Itzam/C in one of two fashions:

    1) Simplified BSD License (FreeBSD License)

    Redistribution and use in source and binary forms, with or without modification, are
    permitted provided that the following conditions are met:

    1.  Redistributions of source code must retain the above copyright notice, this list of
        conditions and the following disclaimer.

    2.  Redistributions in binary form must reproduce the above copyright notice, this list
        of conditions and the following disclaimer in the documentation and/or other materials
        provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY SCOTT ROBERT LADD ``AS IS'' AND ANY EXPRESS OR IMPLIED
    WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
    FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SCOTT ROBERT LADD OR
    CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
    ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
    ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    The views and conclusions contained in the software and documentation are those of the
    authors and should not be interpreted as representing official policies, either expressed
    or implied, of Scott Robert Ladd.

    2) Closed-Source Proprietary License

    If your project is a closed-source or proprietary project, the Simplified BSD License may
    not be appropriate or desirable. In such cases, contact the Itzam copyright holder to
    arrange your purchase of an appropriate license.

    The author can be contacted at:

          scott.ladd@coyotegulch.com
          scott.ladd@gmail.com
          http:www.coyotegulch.com
*/

#include "itzam.h"

#include <stdlib.h>
#include <ctype.h>

static pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;

static itzam_state update_header(itzam_btree * btree)
{
    itzam_state result = ITZAM_FAILED;

    /* rewrite file header
     */
    itzam_ref where = itzam_datafile_write_flags(btree->m_datafile,
                                                 btree->m_header,
                                                 sizeof(itzam_btree_header),
                                                 btree->m_header->m_where,
                                                 ITZAM_RECORD_BTREE_HEADER);

    if (where == btree->m_header->m_where)
        result = ITZAM_OKAY;

    return result;
}

static itzam_btree_page * dupe_page(const itzam_btree * btree, const itzam_btree_page * source_page)
{
    itzam_btree_page * page = NULL;

    /* allocate page
     */
    page = (itzam_btree_page *)malloc(sizeof(itzam_btree_page));

    if (page != NULL)
    {
        /* allocate data space
         */
        page->m_data = (itzam_byte *)malloc(btree->m_header->m_sizeof_page);

        if (page->m_data != NULL)
        {
            page->m_header = (itzam_btree_page_header *)page->m_data;
            page->m_keys   = (itzam_byte *)(page->m_data + sizeof(itzam_btree_page_header));
            page->m_links  = (itzam_ref *)(page->m_data + sizeof(itzam_btree_page_header) + btree->m_header->m_sizeof_key * btree->m_header->m_order);
            memcpy(page->m_data, source_page->m_data, btree->m_header->m_sizeof_page);
        }
        else
        {
            free(page);
            page = NULL;
        }
    }

    return page;
}

static void set_page_pointers(const itzam_btree * btree, itzam_btree_page * page)
{
    page->m_header = (itzam_btree_page_header *)page->m_data;
    page->m_keys   = (itzam_byte *)(page->m_data + sizeof(itzam_btree_page_header));
    page->m_links  = (itzam_ref *)(page->m_data + sizeof(itzam_btree_page_header) + btree->m_header->m_sizeof_key * btree->m_header->m_order);
}

static void init_page(const itzam_btree * btree, itzam_btree_page * page)
{
    int n;

    page->m_header->m_where     = ITZAM_NULL_REF;
    page->m_header->m_parent    = ITZAM_NULL_REF;
    page->m_header->m_key_count = 0;

    memset(page->m_keys, 0, btree->m_header->m_sizeof_key * btree->m_header->m_order);

    for (n = 0; n < btree->m_header->m_order + 1; ++n)
        page->m_links[n] = ITZAM_NULL_REF;
}

static void set_page(const itzam_btree * btree, itzam_btree_page * page, itzam_byte * memory)
{
    if ((page != NULL) && (memory != NULL))
    {
        page->m_data = memory;
        set_page_pointers(btree,page);
    }
}

static itzam_btree_page * alloc_page(const itzam_btree * btree)
{
    itzam_btree_page * page = NULL;

    /* allocate page
     */
    page = (itzam_btree_page *)malloc(sizeof(itzam_btree_page));

    if (page != NULL)
    {
        /* allocate data space
         */
        page->m_data = (itzam_byte *)malloc(btree->m_header->m_sizeof_page);

        if (page->m_data != NULL)
        {
            set_page_pointers(btree,page);
            init_page(btree,page);
        }
        else
        {
            /* error; clean up and return NULL
             */
            free(page);
            page = NULL;
        }
    }

    return page;
}

static void free_page(itzam_btree_page * page)
{
    if ((page != NULL) && (page->m_header->m_parent != ITZAM_NULL_REF))
    {
        if (page->m_data != NULL)
            free(page->m_data);

        free(page);
    }
}

static void set_root(itzam_btree * btree, itzam_btree_page * new_root)
{
    memcpy(btree->m_root_data, new_root->m_data, btree->m_header->m_sizeof_page);
    btree->m_header->m_root_where = new_root->m_header->m_where;
    update_header(btree);
    new_root->m_data = NULL;
}

/* can't be static because it's useful for debug/analysis routines
 */
itzam_btree_page * read_page(itzam_btree * btree, itzam_ref where)
{
    itzam_btree_page * page = NULL;

    /* position datafile
     */
    if (ITZAM_OKAY == itzam_datafile_seek(btree->m_datafile,where))
    {
        /* allocate a buffer
         */
        page = alloc_page(btree);

        if (page != NULL)
        {
            /* read the ref
             */
            if (ITZAM_OKAY != itzam_datafile_read(btree->m_datafile,page->m_data,btree->m_header->m_sizeof_page))
            {
                free_page(page);
                page = NULL;
            }
        }
    }

    return page;
}

static itzam_ref write_page(itzam_btree * btree, itzam_btree_page * page)
{
    itzam_ref where;

    /* does this page have a location, i.e., is it new?
     */
    if (page->m_header->m_where == ITZAM_NULL_REF)
        page->m_header->m_where = itzam_datafile_get_next_open(btree->m_datafile, btree->m_header->m_sizeof_page);

    where = itzam_datafile_write_flags(btree->m_datafile,
                                        page->m_data,
                                        btree->m_header->m_sizeof_page,
                                        page->m_header->m_where,
                                        ITZAM_RECORD_BTREE_PAGE);

    /* make sure things got put where we thought they did
     */
    if (where != page->m_header->m_where)
        where = ITZAM_NULL_REF;

    return where;
}

int itzam_comparator_int32(const void * key1, const void * key2)
{
    int result = 0;

    int32_t * k1 = (int32_t *)key1;
    int32_t * k2 = (int32_t *)key2;

    if (*k1 < *k2)
        result = -1;
    else if (*k1 > *k2)
        result = 1;

    return result;
}

int itzam_comparator_uint32(const void * key1, const void * key2)
{
    int result = 0;

    uint32_t * k1 = (uint32_t *)key1;
    uint32_t * k2 = (uint32_t *)key2;

    if (*k1 < *k2)
        result = -1;
    else if (*k1 > *k2)
        result = 1;

    return result;
}

int itzam_comparator_string(const void * key1, const void * key2)
{
    return strcmp((const char *)key1,(const char *)key2);
}

static char * get_shared_name(const char * fmt, const char * filename)
{
    char * result = (char *)malloc(strlen(fmt) + strlen(filename) + 1);

    char * norm = strdup(filename);
    char * c = norm;

    while (*c)
    {
        if (!isalnum(*c))
            *c = '_';
        else
            if (isalpha(*c))
                *c = tolower(*c);

        ++c;
    }

    sprintf(result, fmt, norm);

    free(norm);

    return result;
}

#if defined(ITZAM_UNIX)
static const char * HDR_NAME_MASK = "/%s-ItzamBTreeHeader";
static const char * ROOT_NAME_MASK = "/%s-ItzamBTreeRoot";
#else
static const char * HDR_NAME_MASK = "Global\\%s-ItzamBTreeHeader";
static const char * ROOT_NAME_MASK = "Global\\%s-ItzamBTreeRoot";
#endif

#define MAKE_ITZAM_BHNAME(basename) get_shared_name(HDR_NAME_MASK,basename)
#define MAKE_ITZAM_ROOT_NAME(basename) get_shared_name(ROOT_NAME_MASK,basename)

itzam_state itzam_btree_create(itzam_btree * btree,
                               const char * filename,
                               uint16_t order,
                               itzam_int key_size,
                               itzam_key_comparator * key_comparator,
                               itzam_error_handler * error_handler)
{
    itzam_state result = ITZAM_FAILED;
    itzam_bool creator;

    pthread_mutex_lock(&global_mutex);

    /* make sure the arguments make sense
     */
    if ((btree != NULL) && (filename != NULL) && (key_size > 0) && (key_comparator != NULL))
    {
        /* allocate datafile
         */
        btree->m_datafile = (itzam_datafile *)malloc(sizeof(itzam_datafile));

        if (btree->m_datafile != NULL)
        {
            /* create data file
             */
            result = itzam_datafile_create(btree->m_datafile,filename);

            if (ITZAM_OKAY == result)
            {
                /* If an error handler was provided, assign it to the datafile
                */
                if (error_handler != NULL)
                    itzam_datafile_set_error_handler(btree->m_datafile,error_handler);

                /* allocate memory for shared header
                 */
                btree->m_shmem_header_name = MAKE_ITZAM_BHNAME(filename);

                btree->m_shmem_header = itzam_shmem_obtain(btree->m_shmem_header_name, sizeof(itzam_btree_header),&creator);
                btree->m_header = (itzam_btree_header *)itzam_shmem_getptr(btree->m_shmem_header, sizeof(itzam_btree_header));

                /* fill in structure
                 */
                if (order < ITZAM_BTREE_ORDER_MINIMUM)
                    order = ITZAM_BTREE_ORDER_MINIMUM;

                btree->m_header->m_version     = ITZAM_BTREE_VERSION;
                btree->m_header->m_order       = order;
                btree->m_header->m_count       = 0;
                btree->m_header->m_ticker      = 0;
                btree->m_header->m_schema_ref  = ITZAM_NULL_REF;

                btree->m_free_datafile         = itzam_true;
                btree->m_links_size            = btree->m_header->m_order + 1;
                btree->m_min_keys              = btree->m_header->m_order / 2;
                btree->m_key_comparator        = key_comparator;
                btree->m_cursor_count          = 0;

                btree->m_header->m_where       = itzam_datafile_get_next_open(btree->m_datafile,sizeof(itzam_btree_header));
                btree->m_header->m_root_where  = 0;
                btree->m_header->m_sizeof_key  = key_size;
                btree->m_header->m_sizeof_page = sizeof(itzam_btree_page_header)
                                               + btree->m_header->m_sizeof_key * btree->m_header->m_order
                                               + sizeof(itzam_ref) * btree->m_links_size;

                /* write header for first time (lacks root pointer info, but needs to occupy space in the file)
                 */
                if (btree->m_header->m_where == itzam_datafile_write_flags(btree->m_datafile, btree->m_header, sizeof(itzam_btree_header), btree->m_header->m_where, ITZAM_RECORD_BTREE_HEADER))
                {
                    /* allocate memory for shared header
                     */
                    btree->m_shmem_root_name = MAKE_ITZAM_ROOT_NAME(filename);
                    btree->m_shmem_root = itzam_shmem_obtain(btree->m_shmem_root_name, btree->m_header->m_sizeof_page, &creator);
                    btree->m_root_data  = (itzam_byte *)itzam_shmem_getptr(btree->m_shmem_root, btree->m_header->m_sizeof_page);
                    set_page(btree, &btree->m_root, btree->m_root_data);
                    init_page(btree, &btree->m_root);

                    /* assign root a file position
                     */
                    btree->m_root.m_header->m_where = itzam_datafile_get_next_open(btree->m_datafile, btree->m_header->m_sizeof_page);

                    if (btree->m_root.m_header->m_where != ITZAM_NULL_REF)
                    {
                        /* write root page
                         */
                        btree->m_header->m_root_where = itzam_datafile_write_flags(btree->m_datafile,
                                                                                   btree->m_root.m_data,
                                                                                   btree->m_header->m_sizeof_page,
                                                                                   btree->m_root.m_header->m_where,
                                                                                   ITZAM_RECORD_BTREE_PAGE);


                        /* make certain that the root was written where we expect it to be
                         */
                        if ((btree->m_header->m_root_where == btree->m_root.m_header->m_where) &&  (btree->m_header->m_root_where != ITZAM_NULL_REF))
                            result = update_header(btree);

                        /* make certain root was written
                         */
                        itzam_file_commit(btree->m_datafile->m_file);
                    }
                }

                /* remember to deallocate this datafile at closing
                 */
                btree->m_free_datafile = itzam_true;

                result = ITZAM_OKAY;
            }
        }
    }
    else
        default_error_handler("itzam_btree_create",ITZAM_ERROR_INVALID_DATAFILE_OBJECT);

    pthread_mutex_unlock(&global_mutex);

    return result;
}

itzam_state itzam_btree_open(itzam_btree * btree,
                             const char * filename,
                             itzam_key_comparator * key_comparator,
                             itzam_error_handler * error_handler,
                             itzam_bool recover,
                             itzam_bool read_only)
{
    /* what we return
     */
    itzam_state result = ITZAM_FAILED;
    itzam_bool creator;

    pthread_mutex_lock(&global_mutex);

    /* make sure the arguments make sense
     */
    if ((btree != NULL) && (filename != NULL) && (key_comparator != NULL))
    {
        /* allocate datafile
         */
        btree->m_datafile = (itzam_datafile *)malloc(sizeof(itzam_datafile));

        if (btree->m_datafile != NULL)
        {
            /* create data file
             */
            result = itzam_datafile_open(btree->m_datafile,filename,recover,read_only);

            if (ITZAM_OKAY == result)
            {
                /* If an error handler was provided, assign it to the datafile
                 */
                if (error_handler != NULL)
                    itzam_datafile_set_error_handler(btree->m_datafile,error_handler);

                /* do the actual create
                 */
                btree->m_free_datafile = itzam_true;
                btree->m_key_comparator = key_comparator;
                btree->m_cursor_count = 0;

                /* allocate memory for embedded header
                 */
                btree->m_shmem_header_name = MAKE_ITZAM_BHNAME(filename);
                btree->m_shmem_header = itzam_shmem_obtain(btree->m_shmem_header_name, sizeof(itzam_btree_header),&creator);
                btree->m_header = (itzam_btree_header *)itzam_shmem_getptr(btree->m_shmem_header, sizeof(itzam_btree_header));

                /* assumes first record is header
                 */
                if (ITZAM_OKAY == itzam_datafile_rewind(btree->m_datafile))
                {
                    if (creator)
                        result = itzam_datafile_read(btree->m_datafile,btree->m_header,sizeof(itzam_btree_header));

                    if (ITZAM_OKAY == result)
                    {
                        /* verify version
                         */
                        if (btree->m_header->m_version == ITZAM_BTREE_VERSION)
                        {
                            /* finish intializing btree
                             */
                            btree->m_links_size = btree->m_header->m_order + 1;
                            btree->m_min_keys   = btree->m_header->m_order / 2;

                            /* allocate memory for shared header
                             */
                            btree->m_shmem_root_name = MAKE_ITZAM_ROOT_NAME(filename);
                            btree->m_shmem_root = itzam_shmem_obtain(btree->m_shmem_root_name, btree->m_header->m_sizeof_page,&creator);
                            btree->m_root_data  = (itzam_byte *)itzam_shmem_getptr(btree->m_shmem_root, btree->m_header->m_sizeof_page);
                            set_page(btree, &btree->m_root, btree->m_root_data);

                            /* read root
                             */
                            if (creator)
                            {
                                itzam_datafile_seek(btree->m_datafile, btree->m_header->m_root_where);
                                itzam_datafile_read(btree->m_datafile, btree->m_root_data, btree->m_header->m_sizeof_page);
                            }

                            result = ITZAM_OKAY;
                        }
                        else
                            result = ITZAM_VERSION_ERROR;
                    }
                }

                result = ITZAM_OKAY;
            }
        }
    }
    else
        default_error_handler("itzam_btree_open",ITZAM_ERROR_INVALID_DATAFILE_OBJECT);

    pthread_mutex_unlock(&global_mutex);

    return result;
}

/* return number of active records in database
 */
uint64_t itzam_btree_count(itzam_btree * btree)
{
    uint64_t result = 0;

    if (btree != NULL)
    {
        itzam_datafile_mutex_lock(btree->m_datafile);

        if (ITZAM_OKAY == result)
            result = btree->m_header->m_count;

        itzam_datafile_mutex_unlock(btree->m_datafile);
    }

    return result;
}

/* return ticker, which is a count of every record ever added to the database
 */
uint64_t itzam_btree_ticker(itzam_btree * btree)
{
    uint64_t result = 0;

    if (btree != NULL)
    {
        itzam_datafile_mutex_lock(btree->m_datafile);

        if (ITZAM_OKAY == result)
            result = btree->m_header->m_ticker;

        itzam_datafile_mutex_unlock(btree->m_datafile);
    }
    else
    {
        default_error_handler("itzam_btree_ticker",ITZAM_ERROR_INVALID_DATAFILE_OBJECT);
    }

    return result;
}

/* close file
 */
itzam_state itzam_btree_close(itzam_btree * btree)
{
    itzam_state result = ITZAM_FAILED;

    pthread_mutex_lock(&global_mutex);

    /* make sure the arguments make sense
     */
    if ((btree != NULL) && (btree->m_cursor_count == 0))
    {
        if (!btree->m_datafile->m_read_only)
        {
            itzam_datafile_mutex_unlock(btree->m_datafile);
            update_header(btree);
            itzam_datafile_mutex_unlock(btree->m_datafile);
        }

        if (result == ITZAM_OKAY)
        {
            free(btree->m_datafile);
            btree->m_datafile = NULL;
        }

        itzam_shmem_freeptr(btree->m_root_data, btree->m_header->m_sizeof_page);
        itzam_shmem_close(btree->m_shmem_root, btree->m_shmem_root_name);
        free(btree->m_shmem_root_name);

        itzam_shmem_freeptr(btree->m_header, sizeof(itzam_btree_header));
        itzam_shmem_close(btree->m_shmem_header,btree->m_shmem_header_name);
        free(btree->m_shmem_header_name);

        result = ITZAM_OKAY;
    }
    else
        default_error_handler("itzam_btree_close",ITZAM_ERROR_INVALID_DATAFILE_OBJECT);

    pthread_mutex_unlock(&global_mutex);

    return result;
}

void itzam_btree_mutex_lock(itzam_btree * btree)
{
    if (btree != NULL)
        itzam_datafile_mutex_lock(btree->m_datafile);
    else
        default_error_handler("itzam_btree_lock",ITZAM_ERROR_INVALID_DATAFILE_OBJECT);
}

void itzam_btree_mutex_unlock(itzam_btree * btree)
{
    if (btree != NULL)
        itzam_datafile_mutex_unlock(btree->m_datafile);
    else
        default_error_handler("itzam_btree_unlock",ITZAM_ERROR_INVALID_DATAFILE_OBJECT);
}

/* lock btree file
 */
itzam_bool itzam_btree_file_lock(itzam_btree * btree)
{
    itzam_bool result = itzam_false;

    if (btree != NULL)
    {
        itzam_datafile_mutex_lock(btree->m_datafile);
        result = itzam_datafile_file_lock(btree->m_datafile);
        itzam_datafile_mutex_unlock(btree->m_datafile);
    }
    else
        default_error_handler("itzam_btree_lock",ITZAM_ERROR_INVALID_DATAFILE_OBJECT);

    return result;
}

/* unlock btree file
 */
itzam_bool itzam_btree_file_unlock(itzam_btree * btree)
{
    itzam_bool result = itzam_false;

    if (btree != NULL)
    {
        itzam_datafile_mutex_lock(btree->m_datafile);
        result = itzam_datafile_file_unlock(btree->m_datafile);
        itzam_datafile_mutex_unlock(btree->m_datafile);
    }
    else
        default_error_handler("itzam_btree_unlock",ITZAM_ERROR_INVALID_DATAFILE_OBJECT);

    return result;
}

itzam_bool itzam_btree_is_open(itzam_btree * btree)
{
    itzam_bool result = itzam_false;

    if (btree != NULL)
    {
        itzam_datafile_mutex_lock(btree->m_datafile);
        result = itzam_datafile_is_open(btree->m_datafile);
        itzam_datafile_mutex_unlock(btree->m_datafile);
    }
    else
    {
        default_error_handler("itzam_btree_is_open",ITZAM_ERROR_INVALID_DATAFILE_OBJECT);
    }

    return result;
}

void itzam_btree_set_error_handler(itzam_btree * btree,
                                   itzam_error_handler * error_handler)
{
    if (btree != NULL)
    {
        itzam_datafile_mutex_lock(btree->m_datafile);
        itzam_datafile_set_error_handler(btree->m_datafile,error_handler);
        itzam_datafile_mutex_unlock(btree->m_datafile);
    }
    else
    {
        default_error_handler("itzam_btree_set_error_handler",ITZAM_ERROR_INVALID_DATAFILE_OBJECT);
    }
}

/* structure used to return search information
 */
typedef struct
{
    itzam_btree_page * m_page;
    int                m_index;
    itzam_bool         m_found;
} search_result;

static void search(itzam_btree * btree, const void * key, search_result * result)
{
    int index;

    /* duplicate root
     */
    itzam_btree_page * page = &btree->m_root;

    while (itzam_true)
    {
       index = 0;

        /* if page is empty, we didn't find the key
         */
        if ((page == NULL) || (page->m_header->m_key_count == 0))
        {
            result->m_page  = page;
            result->m_index = index;
            result->m_found = itzam_false;
            return;
        }
        else
        {
            /* loop through keys
             */
            while (index < page->m_header->m_key_count)
            {
                int comp = btree->m_key_comparator(key,(const void *)(page->m_keys + index * btree->m_header->m_sizeof_key));

                /* do we move on, or have we found it?
                 */
                if (comp > 0)
                    ++index;
                else
                {
                    if (comp == 0)
                    {
                        result->m_page  = page;
                        result->m_index = index;
                        result->m_found = itzam_true;
                        return;
                    }

                    break;
                }
            }

            /* if we're in a leaf, the key hasn't been found
             */
            if (page->m_links[index] == ITZAM_NULL_REF)
            {
                result->m_page  = page;
                result->m_index = index;
                result->m_found = itzam_false;
                return;
            }
            else
            {
                /* read and search next page
                 */
                itzam_btree_page * next_page = read_page(btree,page->m_links[index]);

                if (page->m_header->m_parent != ITZAM_NULL_REF)
                    free_page(page);

                page = next_page;
            }
        }
    }
}

itzam_bool itzam_btree_find(itzam_btree * btree, const void * key, void * returned_key)
{
    search_result s;

    s.m_found = itzam_false;
    s.m_index = 0;
    s.m_page  = NULL;

    if ((btree != NULL) && (key != NULL))
    {
        itzam_datafile_mutex_lock(btree->m_datafile);

        search(btree,key,&s);

        if ((s.m_found) && (returned_key != NULL))
            memcpy(returned_key,s.m_page->m_keys + s.m_index * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);

        if (s.m_page->m_header->m_parent != ITZAM_NULL_REF)
            free_page(s.m_page);

        itzam_datafile_mutex_unlock(btree->m_datafile);
    }
    else
    {
        default_error_handler("itzam_btree_find",ITZAM_ERROR_INVALID_DATAFILE_OBJECT);
    }

    return s.m_found;
}

/* promote key by creating new root
 */
static void promote_root(itzam_btree * btree,
                         itzam_byte * key,
                         itzam_btree_page * page_after)
{
    /* create a new root page
     */
    itzam_btree_page * new_root   = alloc_page(btree);
    itzam_btree_page * new_before = dupe_page(btree, &btree->m_root);

    /* add key and links to root
     */
    memcpy(new_root->m_keys,key,btree->m_header->m_sizeof_key);
    new_root->m_links[0] = new_before->m_header->m_where;
    new_root->m_links[1] = page_after->m_header->m_where;
    new_root->m_header->m_key_count = 1;

    /* write new root to tree, and make it actual root internally
     */
    write_page(btree,new_root);
    set_root(btree,new_root);

    /* update children
     */
    new_before->m_header->m_parent = new_root->m_header->m_where;
    page_after->m_header->m_parent = new_root->m_header->m_where;

    write_page(btree,new_before);
    write_page(btree,page_after);

    free_page(new_before);
    free_page(new_root);
}

/* promote key into parent
 */
static void promote_internal(itzam_btree * btree,
                             itzam_btree_page * page_insert,
                             itzam_byte * key,
                             itzam_ref link)
{
    if (page_insert->m_header->m_key_count == btree->m_header->m_order)
    {
        int nt  = 0;
        int ni  = 0;
        int insert_index = 0;

        itzam_btree_page * page_sibling;
        itzam_btree_page * child;

        /* temporary array
         */
        itzam_byte * temp_keys  = (itzam_byte *)malloc(btree->m_header->m_sizeof_key * (btree->m_header->m_order + 1));
        itzam_ref * temp_links = (itzam_ref *)malloc(sizeof(itzam_ref) * (btree->m_header->m_order + 2));

        if ((temp_keys == NULL) || (temp_links == NULL))
            btree->m_datafile->m_error_handler("promote_internal", ITZAM_ERROR_MALLOC);

        temp_links[0] = page_insert->m_links[0];

        /* find insertion point
         */
        while ((insert_index < page_insert->m_header->m_key_count) && (btree->m_key_comparator(key,(const void *)(page_insert->m_keys + insert_index * btree->m_header->m_sizeof_key)) >= 0))
            ++insert_index;

        /* store new info
         */
        memcpy(temp_keys + insert_index * btree->m_header->m_sizeof_key, key, btree->m_header->m_sizeof_key);
        temp_links[insert_index + 1] = link;

        /* copy existing keys
         */
        while (ni < btree->m_header->m_order)
        {
            if (ni == insert_index)
                ++nt;

            memcpy(temp_keys + nt * btree->m_header->m_sizeof_key, page_insert->m_keys + ni * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
            temp_links[nt + 1] = page_insert->m_links[ni + 1];

            ++ni;
            ++nt;
        }

        /* generate a new leaf node
         */
        page_sibling = alloc_page(btree);
        page_sibling->m_header->m_parent = page_insert->m_header->m_parent;

        /* clear key counts
         */
        page_insert->m_header->m_key_count = 0;
        page_sibling->m_header->m_key_count = 0;
        page_insert->m_links[0] = temp_links[0];

        /* copy keys from temp to pages
         */
        for (ni = 0; ni < btree->m_min_keys; ++ni)
        {
            memcpy(page_insert->m_keys + ni * btree->m_header->m_sizeof_key, temp_keys + ni * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
            page_insert->m_links[ni + 1] = temp_links[ni + 1];
            ++page_insert->m_header->m_key_count;
        }

        page_sibling->m_links[0] = temp_links[btree->m_min_keys + 1];

        for (ni = btree->m_min_keys + 1; ni <= btree->m_header->m_order; ++ni)
        {
            memcpy(page_sibling->m_keys + (ni - 1 - btree->m_min_keys) * btree->m_header->m_sizeof_key, temp_keys + ni * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
            page_sibling->m_links[ni - btree->m_min_keys]    = temp_links[ni + 1];
            ++page_sibling->m_header->m_key_count;
        }

        /* replace unused entries with null
         */
        for (ni = btree->m_min_keys; ni < btree->m_header->m_order; ++ni)
        {
            memset(page_insert->m_keys + ni * btree->m_header->m_sizeof_key, 0, btree->m_header->m_sizeof_key);
            page_insert->m_links[ni + 1] = ITZAM_NULL_REF;
        }

        /* write pages
         */
        write_page(btree,page_insert);
        write_page(btree,page_sibling);

        if (page_insert->m_header->m_parent == ITZAM_NULL_REF)
            set_root(btree,page_insert);

        /* update parent links in child nodes
         */
        for (ni = 0; ni <= page_sibling->m_header->m_key_count; ++ni)
        {
            child = read_page(btree,page_sibling->m_links[ni]);

            if (child != NULL)
            {
                child->m_header->m_parent = page_sibling->m_header->m_where;
                write_page(btree,child);
            }
            else
                btree->m_datafile->m_error_handler("promote_internal",ITZAM_ERROR_PAGE_NOT_FOUND);

            free_page(child);
        }

        /* promote key and pointer
         */
        if (page_insert->m_header->m_parent == ITZAM_NULL_REF)
        {
            /* create a new root
             */
            promote_root(btree,
                         temp_keys + btree->m_min_keys * btree->m_header->m_sizeof_key,
                         page_sibling);
        }
        else
        {
            /* read parent and promote key
             */
            itzam_btree_page * parent_page = read_page(btree,page_insert->m_header->m_parent);

            promote_internal(btree,
                             parent_page,
                             temp_keys + btree->m_min_keys * btree->m_header->m_sizeof_key,
                             page_sibling->m_header->m_where);

            free_page(parent_page);
        }

        /* release resources
         */
        free_page(page_sibling);
        free(temp_keys);
        free(temp_links);
    }
    else
    {
        int n, insert_index = 0;

        /* find insertion point
         */
        while ((insert_index < page_insert->m_header->m_key_count)
            && (btree->m_key_comparator(key,(void *)(page_insert->m_keys + insert_index * btree->m_header->m_sizeof_key)) >= 0))
        {
            ++insert_index;
        }

        /* shift keys right
         */
        for (n = page_insert->m_header->m_key_count; n > insert_index; --n)
        {
            memcpy(page_insert->m_keys + n * btree->m_header->m_sizeof_key, page_insert->m_keys + (n - 1) * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
            page_insert->m_links[n + 1] = page_insert->m_links[n];
        }

        memcpy(page_insert->m_keys + insert_index * btree->m_header->m_sizeof_key, key, btree->m_header->m_sizeof_key);
        page_insert->m_links[insert_index + 1] = link;

        ++page_insert->m_header->m_key_count;

        /* write updated page
         */
        write_page(btree,page_insert);

        if (page_insert->m_header->m_parent == ITZAM_NULL_REF)
            set_root(btree, page_insert);
    }
}

static void write_key(itzam_btree * btree,
                      search_result * insert_info,
                      const itzam_byte * key)
{
    itzam_btree_page * page_sibling;
    itzam_btree_page * page_parent;

    /* check to see if page is full
     */
    if (insert_info->m_page->m_header->m_key_count == btree->m_header->m_order)
    {
        int nt, ni;

        /* temporary array to store new items
         */
        itzam_byte * temp_keys = (itzam_byte *)malloc(btree->m_header->m_sizeof_key * (btree->m_header->m_order + 1));
        memcpy(temp_keys + insert_info->m_index * btree->m_header->m_sizeof_key, key, btree->m_header->m_sizeof_key);

        /* copy entries from insertion page to temps
         */
        nt = 0;
        ni = 0;

        while (ni < btree->m_header->m_order)
        {
            /* skip over inserted data
             */
            if (ni == insert_info->m_index)
                ++nt;

            /* copy data
             */
            memcpy(temp_keys + nt * btree->m_header->m_sizeof_key, insert_info->m_page->m_keys + ni * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);

            /* next one
             */
            ++ni;
            ++nt;
        }

        /* create a new leaf
         */
        page_sibling = alloc_page(btree);
        page_sibling->m_header->m_parent = insert_info->m_page->m_header->m_parent;

        /* clear key counts
         */
        insert_info->m_page->m_header->m_key_count = 0;
        page_sibling->m_header->m_key_count        = 0;

        /* copy keys from temp to pages
         */
        for (ni = 0; ni < btree->m_min_keys; ++ni)
        {
            memcpy(insert_info->m_page->m_keys + ni * btree->m_header->m_sizeof_key, temp_keys + ni * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
            ++insert_info->m_page->m_header->m_key_count;
        }

        for (ni = btree->m_min_keys + 1; ni <= btree->m_header->m_order; ++ni)
        {
            memcpy(page_sibling->m_keys + (ni - 1 - btree->m_min_keys) * btree->m_header->m_sizeof_key, temp_keys + ni * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
            ++page_sibling->m_header->m_key_count;
        }

        /* replace remaining entries with null
         */
        for (ni = btree->m_min_keys; ni < btree->m_header->m_order; ++ni)
            memset(insert_info->m_page->m_keys + ni * btree->m_header->m_sizeof_key,0,btree->m_header->m_sizeof_key);

        /* write pages
         */
        write_page(btree,insert_info->m_page);
        write_page(btree,page_sibling);

        /* promote key and its pointer
         */
        if (insert_info->m_page->m_header->m_parent == ITZAM_NULL_REF)
        {
            /* creating a new root page
             */
            promote_root(btree,
                         temp_keys + btree->m_min_keys * btree->m_header->m_sizeof_key,
                         page_sibling);
        }
        else
        {
            /* read parent
             */
            page_parent = read_page(btree,insert_info->m_page->m_header->m_parent);

            /* promote key into parent page
             */
            promote_internal(btree,
                             page_parent,
                             temp_keys + btree->m_min_keys * btree->m_header->m_sizeof_key,
                             page_sibling->m_header->m_where);

            free_page(page_parent);
        }

        /* release sibling page
         */
        if (page_sibling->m_header->m_parent != ITZAM_NULL_REF)
            free_page(page_sibling);

        free(temp_keys);
    }
    else
    {
        int n;

        /* move keys to make room for new one
         */
        for (n = insert_info->m_page->m_header->m_key_count; n > insert_info->m_index; --n)
            memcpy(insert_info->m_page->m_keys + n * btree->m_header->m_sizeof_key, insert_info->m_page->m_keys + (n - 1) * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);

        memcpy(insert_info->m_page->m_keys + insert_info->m_index * btree->m_header->m_sizeof_key, key, btree->m_header->m_sizeof_key);

        ++insert_info->m_page->m_header->m_key_count;

        /* write updated page
         */
        write_page(btree,insert_info->m_page);
    }
}

itzam_state itzam_btree_insert(itzam_btree * btree,
                               const void * key)
{
    itzam_state result = ITZAM_FAILED;
    search_result insert_info;

    if ((btree != NULL) && (key != NULL) && (btree->m_cursor_count == 0))
    {
        itzam_datafile_mutex_lock(btree->m_datafile);

        if (!btree->m_datafile->m_read_only)
        {
            search(btree,key,&insert_info);

            if (!insert_info.m_found)
            {
                write_key(btree,&insert_info,(const itzam_byte *)key);
                ++btree->m_header->m_count;
                ++btree->m_header->m_ticker;

                result = update_header(btree);
            }
            else
            {
                result = ITZAM_DUPLICATE;
            }

            if (insert_info.m_page->m_header->m_parent != ITZAM_NULL_REF)
                free_page(insert_info.m_page);
        }
        else
            result = ITZAM_READ_ONLY;

        itzam_datafile_mutex_unlock(btree->m_datafile);
    }

    //return result;
    return result;
}

static void redistribute(itzam_btree * btree,
                         int index,
                         itzam_btree_page * page_before,
                         itzam_btree_page * page_parent,
                         itzam_btree_page * page_after)
{
    if ((btree != NULL) && (page_before != NULL) && (page_parent != NULL) && (page_after != NULL))
    {
       /* check for leaf page
       */
        if (page_before->m_links[0] == ITZAM_NULL_REF)
        {
            if (page_before->m_header->m_key_count > page_after->m_header->m_key_count)
            {
                int n;

                /* move a key from page_before to page_after
                 */
                for (n = page_after->m_header->m_key_count; n > 0; --n)
                    memcpy(page_after->m_keys + n * btree->m_header->m_sizeof_key, page_after->m_keys + (n - 1) * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);

                /* store parent separator in page_after
                 */
                memcpy(page_after->m_keys, page_parent->m_keys + index * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);

                /* increment page_after key count
                 */
                ++page_after->m_header->m_key_count;

                /* decrement page_before key count
                 */
                --page_before->m_header->m_key_count;

                /* move last key in page_before to page_parent as separator
                 */
                memcpy(page_parent->m_keys + index * btree->m_header->m_sizeof_key, page_before->m_keys + page_before->m_header->m_key_count * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);

                /* clear last key in page_before
                 */
                memset(page_before->m_keys + page_before->m_header->m_key_count * btree->m_header->m_sizeof_key, 0, btree->m_header->m_sizeof_key);
            }
            else
            {
                int n;

                /* add parent key to lesser page
                 */
                memcpy(page_before->m_keys + page_before->m_header->m_key_count * btree->m_header->m_sizeof_key, page_parent->m_keys + index * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);

                /* increment page_before key count
                 */
                ++page_before->m_header->m_key_count;

                /* move first key in page_after to page_parent as separator
                 */
                memcpy(page_parent->m_keys + index * btree->m_header->m_sizeof_key, page_after->m_keys, btree->m_header->m_sizeof_key);

                /* decrement page_after key count
                 */
                --page_after->m_header->m_key_count;

                /* move a key from page_after to page_before
                 */
                for (n = 0; n < page_after->m_header->m_key_count; ++n)
                    memcpy(page_after->m_keys + n * btree->m_header->m_sizeof_key, page_after->m_keys + (n + 1) * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);

                /* clear last key in page_after
                 */
                memset(page_after->m_keys + n * btree->m_header->m_sizeof_key, 0, btree->m_header->m_sizeof_key);
            }
        }
        else
        {
            itzam_btree_page * page_child;

            if (page_before->m_header->m_key_count > page_after->m_header->m_key_count)
            {
                int n;

                /* move a key from page_before to page_after
                 */
                for (n = page_after->m_header->m_key_count; n > 0; --n)
                {
                    memcpy(page_after->m_keys + n * btree->m_header->m_sizeof_key, page_after->m_keys + (n - 1) * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
                    page_after->m_links[n + 1] = page_after->m_links[n];
                }

                page_after->m_links[1] = page_after->m_links[0];

                /* store page_parent separator key in page_after
                 */
                memcpy(page_after->m_keys, page_parent->m_keys + index * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
                page_after->m_links[0] = page_before->m_links[page_before->m_header->m_key_count];

                /* update child link
                 */
                page_child = read_page(btree,page_after->m_links[0]);

                if (page_child != NULL)
                {
                    page_child->m_header->m_parent = page_after->m_header->m_where;
                    write_page(btree,page_child);
                    free_page(page_child);
                }
                else
                    btree->m_datafile->m_error_handler("redistribute",ITZAM_ERROR_PAGE_NOT_FOUND);

                /* increment page_after key count
                 */
                ++page_after->m_header->m_key_count;

                /* decrement page_before key count
                 */
                --page_before->m_header->m_key_count;

                /* move last key in page_before to page_parent as separator
                 */
                memcpy(page_parent->m_keys + index * btree->m_header->m_sizeof_key, page_before->m_keys + page_before->m_header->m_key_count * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);

                /* clear last key in page_before
                 */
                memset(page_before->m_keys + page_before->m_header->m_key_count * btree->m_header->m_sizeof_key, 0, btree->m_header->m_sizeof_key);
                page_before->m_links[page_before->m_header->m_key_count + 1] = ITZAM_NULL_REF;
            }
            else
            {
                int n;

                /* store page_parent separator key in page_before
                 */
                memcpy(page_before->m_keys + page_before->m_header->m_key_count * btree->m_header->m_sizeof_key, page_parent->m_keys + index * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
                page_before->m_links[page_before->m_header->m_key_count + 1] = page_after->m_links[0];

                /* update child link
                 */
                page_child = read_page(btree,page_after->m_links[0]);

                if (page_child != NULL)
                {
                    page_child->m_header->m_parent = page_before->m_header->m_where;
                    write_page(btree,page_child);
                    free_page(page_child);
                }
                else
                    btree->m_datafile->m_error_handler("redistribute",ITZAM_ERROR_PAGE_NOT_FOUND);

                /* increment page_before key count
                 */
                ++page_before->m_header->m_key_count;

                /* move last key in page_after to page_parent as separator
                 */
                memcpy(page_parent->m_keys + index * btree->m_header->m_sizeof_key, page_after->m_keys, btree->m_header->m_sizeof_key);

                /* decrement page_after key count
                 */
                --page_after->m_header->m_key_count;

                /* move a key from page_after to page_before
                 */
                for (n = 0; n < page_after->m_header->m_key_count; ++n)
                {
                    memcpy(page_after->m_keys + n * btree->m_header->m_sizeof_key, page_after->m_keys + (n + 1) * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
                    page_after->m_links[n] = page_after->m_links[n + 1];
                }

                page_after->m_links[n] = page_after->m_links[n + 1];

                /* clear last key in page_after
                 */
                memset(page_after->m_keys + n * btree->m_header->m_sizeof_key, 0, btree->m_header->m_sizeof_key);
                page_after->m_links[n + 1] = ITZAM_NULL_REF;
            }
        }

        /* write updated pages
         */
        write_page(btree,page_before);
        write_page(btree,page_after);
        write_page(btree,page_parent);

        if(page_parent->m_header->m_parent == ITZAM_NULL_REF)
            set_root(btree, page_parent);
    }
}

static void adjust_tree(itzam_btree * btree, itzam_btree_page * page);

static void concatenate(itzam_btree * btree, int index, itzam_btree_page * page_before, itzam_btree_page * page_parent, itzam_btree_page * page_after)
{
    int n, n2;

    /* move separator key from page_parent into page_before
     */
    memcpy(page_before->m_keys + page_before->m_header->m_key_count * btree->m_header->m_sizeof_key, page_parent->m_keys + index * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
    page_before->m_links[page_before->m_header->m_key_count + 1] = page_after->m_links[0];

    /* increment page_before key count
     */
    ++page_before->m_header->m_key_count;

    /* delete separator from page_parent
     */
    --page_parent->m_header->m_key_count;

    for (n = index; n < page_parent->m_header->m_key_count; ++n)
    {
        memcpy(page_parent->m_keys + n * btree->m_header->m_sizeof_key, page_parent->m_keys + (n + 1) * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
        page_parent->m_links[n + 1] = page_parent->m_links[n + 2];
    }

    /* clear unused key from parent
     */
    memset(page_parent->m_keys + n * btree->m_header->m_sizeof_key, 0, btree->m_header->m_sizeof_key);
    page_parent->m_links[n + 1] = ITZAM_NULL_REF;

    /* copy keys from page_after to page_before
     */
    n2 = 0;
    n = page_before->m_header->m_key_count;

    /* combine pages
     */
    while (n2 < page_after->m_header->m_key_count)
    {
        ++page_before->m_header->m_key_count;
        memcpy(page_before->m_keys + n * btree->m_header->m_sizeof_key, page_after->m_keys + n2 * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);
        page_before->m_links[n + 1] = page_after->m_links[n2 + 1];
        ++n2;
        ++n;
    }

    /* delete page_after
     */
    itzam_datafile_seek(btree->m_datafile,page_after->m_header->m_where);
    itzam_datafile_remove(btree->m_datafile);

    /* is this an inner page?
     */
    if (page_before->m_links[0] != ITZAM_NULL_REF)
    {
        /* adjust child pointers
         */
        itzam_btree_page * page_child;

        for (n = 0; n <= page_before->m_header->m_key_count; ++n)
        {
            /* read child
             */
            page_child = read_page(btree,page_before->m_links[n]);

            /* make sure the child was actually read
             */
            if (page_child != NULL)
            {
                page_child->m_header->m_parent = page_before->m_header->m_where;
                write_page(btree,page_child);
                free_page(page_child);
            }
            else
                btree->m_datafile->m_error_handler("concatenate",ITZAM_ERROR_PAGE_NOT_FOUND);
        }
    }

    /* write page_before and parent
     */
    if (page_parent->m_header->m_key_count == 0)
    {
        /* update before page with old parent's parent
         */
        page_before->m_header->m_parent = page_parent->m_header->m_parent;
        write_page(btree,page_before);

        if (page_before->m_header->m_parent == ITZAM_NULL_REF)
        {
            /* update the header if this is the new root
             */
            set_root(btree,page_before);
        }
        else
        {
            /* read parents's parent
             */
            itzam_btree_page * parents_parent = read_page(btree,page_parent->m_header->m_parent);

            /* find parent reference and replace with before page
             */
            if (parents_parent != NULL)
            {
                for (n = 0; n < btree->m_links_size; ++n)
                {
                    if (parents_parent->m_links[n] == page_parent->m_header->m_where)
                    {
                        parents_parent->m_links[n] = page_before->m_header->m_where;
                        write_page(btree,parents_parent);
                    }
                }

                free_page(parents_parent);
            }
        }

        /* remove empty parent page
         */
        itzam_datafile_seek(btree->m_datafile,page_parent->m_header->m_where);
        itzam_datafile_remove(btree->m_datafile);
    }
    else
    {
        /* write pages
         */
        write_page(btree,page_parent);
        write_page(btree,page_before);

        /* reset root page if needed
         */
        if (page_parent->m_header->m_parent == ITZAM_NULL_REF)
            set_root(btree,page_parent);

        /* if parent is too small, adjust
         */
        if (page_parent->m_header->m_key_count < btree->m_min_keys)
            adjust_tree(btree,page_parent);
    }
}

static void adjust_tree(itzam_btree * btree, itzam_btree_page * page)
{
    if ((btree != NULL) && (page != NULL) && (page->m_header->m_parent != ITZAM_NULL_REF))
    {
        /* get parent page
         */
        itzam_btree_page * page_parent = read_page(btree,page->m_header->m_parent);

        if (page_parent != NULL)
        {
            itzam_btree_page * page_sibling_after  = NULL;
            itzam_btree_page * page_sibling_before = NULL;

            /* find pointer to page
             */
            int n = 0;

            while (page_parent->m_links[n] != page->m_header->m_where)
                ++n;

            /* read sibling pages
             */
            if (n < page_parent->m_header->m_key_count)
                page_sibling_after = read_page(btree,page_parent->m_links[n+1]);

            if (n > 0)
                page_sibling_before = read_page(btree,page_parent->m_links[n-1]);

            /* figure out what to do
             */
            if (page_sibling_before != NULL)
            {
                --n;

                if (page_sibling_before->m_header->m_key_count > btree->m_min_keys)
                    redistribute(btree,n,page_sibling_before,page_parent,page);
                else
                    concatenate(btree,n,page_sibling_before,page_parent,page);
            }
            else
            {
                if (page_sibling_after != NULL)
                {
                    if (page_sibling_after->m_header->m_key_count > btree->m_min_keys)
                        redistribute(btree,n,page,page_parent,page_sibling_after);
                    else
                        concatenate(btree,n,page,page_parent,page_sibling_after);
                }
            }

            if (page_sibling_before != NULL)
                free_page(page_sibling_before);

            if (page_sibling_after != NULL)
                free_page(page_sibling_after);

            if (page_parent->m_header->m_parent != ITZAM_NULL_REF)
                free_page(page_parent);
        }
    }
}

itzam_state itzam_btree_remove(itzam_btree * btree, const void * key)
{
    itzam_state result = ITZAM_FAILED;
    search_result remove_info;

    if ((btree != NULL) && (key != NULL) && (btree->m_cursor_count == 0))
    {
        itzam_datafile_mutex_lock(btree->m_datafile);

        if (btree->m_datafile->m_read_only)
            result = ITZAM_READ_ONLY;
        else
        {
            search(btree,key,&remove_info);

            if (remove_info.m_found)
            {
                /* is this a leaf node?
                */
                if (remove_info.m_page->m_links[0] == ITZAM_NULL_REF)
                {
                    int n;

                    /* removing key from leaf
                    */
                    --remove_info.m_page->m_header->m_key_count;

                    /* slide keys left over removed one
                    */
                    for (n = remove_info.m_index; n < remove_info.m_page->m_header->m_key_count; ++n)
                        memcpy(remove_info.m_page->m_keys + n * btree->m_header->m_sizeof_key, remove_info.m_page->m_keys + (n + 1) * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);

                    memset( remove_info.m_page->m_keys + remove_info.m_page->m_header->m_key_count * btree->m_header->m_sizeof_key, 0, btree->m_header->m_sizeof_key);

                    /* save page
                    */
                    write_page(btree,remove_info.m_page);

                    /* adjust the tree, if needed
                    */
                    if (remove_info.m_page->m_header->m_key_count < btree->m_min_keys)
                        adjust_tree(btree,remove_info.m_page);

                    result = ITZAM_OKAY;
                }
                else /* removing from an internal page */
                {
                    /* get the successor page
                    */
                    itzam_btree_page * page_successor = read_page(btree,remove_info.m_page->m_links[remove_info.m_index + 1]);

                    if (page_successor != NULL)
                    {
                        int n;

                        while (page_successor->m_links[0] != ITZAM_NULL_REF)
                        {
                            itzam_btree_page * next_successor = read_page(btree,page_successor->m_links[0]);

                            /* check for null page in case of corrupted index
                            */
                            if (next_successor != NULL)
                            {
                                free_page(page_successor);
                                page_successor = next_successor;
                            }
                            else
                                btree->m_datafile->m_error_handler("itzam_btree_remove",ITZAM_ERROR_PAGE_NOT_FOUND);
                        }

                        /* first key is the "swappee"
                        */
                        memcpy(remove_info.m_page->m_keys + remove_info.m_index * btree->m_header->m_sizeof_key, page_successor->m_keys, btree->m_header->m_sizeof_key);

                        /* remove swapped key from successor page
                        */
                        --page_successor->m_header->m_key_count;

                        for (n = 0; n < page_successor->m_header->m_key_count; ++n)
                            memcpy(page_successor->m_keys + n * btree->m_header->m_sizeof_key, page_successor->m_keys + (n + 1) * btree->m_header->m_sizeof_key, btree->m_header->m_sizeof_key);

                        memset(page_successor->m_keys + page_successor->m_header->m_key_count * btree->m_header->m_sizeof_key, 0, btree->m_header->m_sizeof_key);

                        /* write modified pages
                        */
                        write_page(btree,remove_info.m_page);
                        write_page(btree,page_successor);

                        /* adjust tree for leaf node
                        */
                        if (page_successor->m_header->m_key_count < btree->m_min_keys)
                            adjust_tree(btree,page_successor);

                        result = ITZAM_OKAY;

                        free_page(page_successor);
                    }
                }

                /* decrement number of records in file
                */
                --btree->m_header->m_count;
                update_header(btree);
            }
            else
            {
                result = ITZAM_NOT_FOUND;
            }

            if (remove_info.m_page->m_header->m_parent != ITZAM_NULL_REF)
                free_page(remove_info.m_page);
        }

        itzam_datafile_mutex_unlock(btree->m_datafile);
    }

    return result;
}

uint16_t itzam_btree_cursor_count(itzam_btree * btree)
{
    uint16_t result = 0;

    if (btree != NULL)
    {
        itzam_datafile_mutex_lock(btree->m_datafile);
        result = btree->m_cursor_count;
        itzam_datafile_mutex_unlock(btree->m_datafile);
    }

    return result;
}

itzam_state itzam_btree_transaction_start(itzam_btree * btree)
{
    itzam_state result = ITZAM_FAILED;

    if (btree != NULL)
    {
        itzam_datafile_mutex_lock(btree->m_datafile);
        result = itzam_datafile_transaction_start(btree->m_datafile);

        if (result == ITZAM_OKAY)
            btree->m_saved_header = itzam_datafile_write(btree->m_datafile->m_tran_file, btree->m_header, sizeof(itzam_btree_header), ITZAM_NULL_REF);
    }

    return result;
}

itzam_state itzam_btree_transaction_commit(itzam_btree * btree)
{
    itzam_state result = ITZAM_FAILED;

    if (btree != NULL)
    {
        result = itzam_datafile_transaction_commit(btree->m_datafile);
        itzam_datafile_mutex_unlock(btree->m_datafile);
    }

    return result;
}

itzam_state itzam_btree_transaction_rollback(itzam_btree * btree)
{
    itzam_state result = ITZAM_FAILED;
    itzam_btree_page * old_root;

    if (btree != NULL)
    {
        /* turn off transaction processing so we can restore
         */
        btree->m_datafile->m_in_transaction = itzam_false;
        itzam_datafile_seek(btree->m_datafile->m_tran_file, btree->m_saved_header);
        itzam_datafile_read(btree->m_datafile->m_tran_file, btree->m_header, sizeof(itzam_btree_header));
        update_header(btree);

        /* turn transaction processing on again
         */
        btree->m_datafile->m_in_transaction = itzam_true;
        result = itzam_datafile_transaction_rollback(btree->m_datafile);

        /* restore the root
         */
        old_root = read_page(btree,btree->m_header->m_root_where);
        //free(btree->m_root.m_data);
        memcpy(btree->m_root_data, old_root->m_data, btree->m_header->m_sizeof_page);

        /* done here
         */
        itzam_datafile_mutex_unlock(btree->m_datafile);
    }

    return result;
}

/**
 *------------------------------------------------------------
 * B-tree cursor functions
 */

static itzam_bool reset_cursor(itzam_btree_cursor * cursor)
{
    itzam_bool result = itzam_false;
    itzam_bool looking = itzam_true;

    /* read root */
    itzam_btree_page * next_page = NULL;
    itzam_btree_cursor_memory * next_memory = NULL;
    itzam_btree_page * page = &cursor->m_btree->m_root;

    /* follow the tree to the first key in the sequence */
    cursor->m_parent_memory = NULL;
    cursor->m_page = NULL;
    cursor->m_index = 0;

    while (looking && (page != NULL))
    {
        if (page->m_header->m_key_count > 0)
        {
            if (page->m_links[0] == ITZAM_NULL_REF)
            {
                /* found the first key */
                cursor->m_page = page;
                cursor->m_index = 0;
                result = itzam_true;
                looking = itzam_false;
            }
            else
            {
                /* remember position in parent page */
                next_memory = (itzam_btree_cursor_memory *)malloc(sizeof(itzam_btree_cursor_memory));

                if (next_memory == NULL)
                    cursor->m_btree->m_datafile->m_error_handler("reset_sursor", ITZAM_ERROR_MALLOC);

                next_memory->m_prev = cursor->m_parent_memory;
                next_memory->m_index = 0;
                cursor->m_parent_memory = next_memory;

                /* move to next page */
                next_page = read_page(cursor->m_btree,page->m_links[0]);

                if (page->m_header->m_parent != ITZAM_NULL_REF)
                    free_page(page);

                page = next_page;
            }
        }
        else
        {
            free_page(page);
            cursor->m_page = NULL;
            cursor->m_index = 0;
            cursor->m_parent_memory = NULL;
            looking = itzam_false;
        }
    }

    return result;
}

itzam_state itzam_btree_cursor_create(itzam_btree_cursor * cursor, itzam_btree * btree)
{
    itzam_state result = ITZAM_FAILED;

    if ((cursor != NULL) && (btree != NULL))
    {
        /* keep reference to target tree */
        cursor->m_btree = btree;

        /* set cursor to first index key */
        if (reset_cursor(cursor))
        {
            /* increment count of cursors in btree */
            ++cursor->m_btree->m_cursor_count;
            result = ITZAM_OKAY;
        }
    }

    return result;
}

itzam_bool itzam_btree_cursor_valid(itzam_btree_cursor * cursor)
{
    return (cursor->m_page != NULL);
}

itzam_state itzam_btree_cursor_free(itzam_btree_cursor * cursor)
{
    itzam_state result = ITZAM_FAILED;

    if ((cursor != NULL) && (cursor->m_page != NULL))
    {
        /* decrement btree cursor count */
        if (cursor->m_btree->m_cursor_count > 0)
        {
            --cursor->m_btree->m_cursor_count;
            result = ITZAM_OKAY;
        }
        else
            cursor->m_btree->m_datafile->m_error_handler("itzam_btree_cursor_free",ITZAM_ERROR_CURSOR_COUNT);
    }

    return result;
}

itzam_bool itzam_btree_cursor_next(itzam_btree_cursor * cursor)
{
    itzam_bool result = itzam_false;
    itzam_bool looking = itzam_true;
    itzam_btree_page * next_page = NULL;
    itzam_btree_cursor_memory * temp_memory = NULL;

    if ((cursor != NULL) && (cursor->m_page != NULL))
    {
        ++cursor->m_index;

        /* is this a leaf page? */
        if (cursor->m_page->m_links[cursor->m_index] == ITZAM_NULL_REF)
        {
            while (looking)
            {
                /* are we at the end of the leaf page? */
                if (cursor->m_index == cursor->m_page->m_header->m_key_count)
                {
                    /* do we have a oarent to go back to? */
                    if (cursor->m_page->m_header->m_parent == ITZAM_NULL_REF)
                    {
                        /* at end of keys */
                        result = itzam_false;
                        looking = itzam_false;
                    }
                    else
                    {
                        /* return to parent */
                        next_page = read_page(cursor->m_btree,cursor->m_page->m_header->m_parent);

                        /* make new page our current page */
                        if (cursor->m_page->m_header->m_parent != ITZAM_NULL_REF)
                            free_page(cursor->m_page);

                        cursor->m_page = next_page;

                        /* restore parent page position, remove old parent memory */
                        temp_memory = cursor->m_parent_memory;
                        cursor->m_index = cursor->m_parent_memory->m_index;
                        cursor->m_parent_memory = cursor->m_parent_memory->m_prev;
                        free(temp_memory);

                        result = itzam_true;
                        looking = itzam_true;
                    }
                }
                else
                {
                    /* we're on the next key */
                    result = itzam_true;
                    looking = itzam_false;
                }
            }
        }
        else /* inner page */
        {
            while (itzam_true)
            {
                /* read next page */
                next_page = read_page(cursor->m_btree,cursor->m_page->m_links[cursor->m_index]);

                /* remember position in parent page */
                temp_memory = (itzam_btree_cursor_memory *)malloc(sizeof(itzam_btree_cursor_memory));
                temp_memory->m_prev = cursor->m_parent_memory;
                temp_memory->m_index = cursor->m_index;
                cursor->m_parent_memory = temp_memory;

                /* start at beginning of new page */
                cursor->m_index = 0;

                /* make new page our current page */
                if (cursor->m_page->m_header->m_parent != ITZAM_NULL_REF)
                    free_page(cursor->m_page);

                cursor->m_page = next_page;

                /* do we need to push and move up again? */
                if ((cursor->m_index == 0) && (cursor->m_page->m_header->m_parent != ITZAM_NULL_REF) && (cursor->m_page->m_links[0] != ITZAM_NULL_REF))
                    continue;
                else
                    break;
            }

            result = itzam_true;
        }
    }

    return result;
}

itzam_bool itzam_btree_cursor_reset(itzam_btree_cursor * cursor)
{
    return reset_cursor(cursor);
}

itzam_state itzam_btree_cursor_read(itzam_btree_cursor * cursor, void * returned_key)
{
    itzam_state result = ITZAM_NOT_FOUND;

    if ((cursor != NULL) && (returned_key != NULL) && (cursor->m_page != NULL))
    {
        memcpy(returned_key, cursor->m_page->m_keys + cursor->m_index * cursor->m_btree->m_header->m_sizeof_key, cursor->m_btree->m_header->m_sizeof_key);
        result = ITZAM_OKAY;
    }

    return result;
}
