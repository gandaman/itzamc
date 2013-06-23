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

#if !defined(LIBITZAM_ITZAM_H)
#define LIBITZAM_ITZAM_H

#if defined(__cplusplus)
extern "C" {
#endif

#include <string.h>
#include <stdio.h>

/*-----------------------------------------------------------------------------
    Operating system identification and settings
 */
#if !defined(ITZAM32) && !defined(ITZAM64)
  #if defined(__LP64__) || defined(_M_X64)
    #undef  ITZAM32
    #define ITZAM64 1
    #define ITZAM_MAX_FILE_SIZE 9223372036854775808
    #define REF_BYTES 8
  #else
    #undef  ITZAM64
    #define ITZAM32 1
    #define ITZAM_MAX_FILE_SIZE 2147483648
    #define REF_BYTES 4
  #endif
#endif

#if defined(_WIN32) || defined(_WIN64) || defined (_MSC_VER)
    #define ITZAM_WINDOWS
    #include <io.h>
    #include <fcntl.h>
    #include <share.h>
    #include <windows.h>
    #include <process.h>
    #if _MSC_VER >= 0x0A00
        #include <stdint.h>
    #else
        typedef unsigned __int64 uint64_t;
        typedef __int64          int64_t;
        typedef unsigned long    uint32_t;
        typedef long             int32_t;
        typedef unsigned short   uint16_t;
        typedef short            int16_t;
        typedef unsigned char    uint8_t;
        typedef signed char      int8_t;
    #endif
    #define ITZAM_SHMEM_TYPE HANDLE
    #define ITZAM_FILE_TYPE  HANDLE
    #define ITZAM_GOOD_FILE(file) (file != INVALID_HANDLE_VALUE)
    #define ITZAM_SEEK_BEGIN   FILE_BEGIN
    #define ITZAM_SEEK_END     FILE_END
    #define ITZAM_SEEK_CURRENT FILE_CURRENT
    #define DLL_EXPORT
    #pragma warning (disable : 4786 4244 4267 4101 4996 4761 4127)
#elif  defined(__unix__) || defined(unix)
    #if defined(__linux__) || defined(__linux)
        #define ITZAM_LINUX
    #endif
    #define ITZAM_UNIX
    #include <unistd.h>
    #include <pthread.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <fcntl.h>
    #define ITZAM_SHMEM_TYPE int
    #define ITZAM_FILE_TYPE  int
    #define ITZAM_SEEK_BEGIN   SEEK_SET
    #define ITZAM_SEEK_END     SEEK_END
    #define ITZAM_SEEK_CURRENT SEEK_CUR
    #define ITZAM_GOOD_FILE(file) (file != -1)
#else
    #error "Itzam is designed for 32- and 64-bit Windows and POSIX"
#endif

#if defined(ITZAM_UNIX) || defined(ITZAM_LINUX)
    #include <unistd.h>
    #if _POSIX_VERSION < 200809L
        #error "Itzam requires POSIX.1-2008 or later"
    #endif
#endif

/*-----------------------------------------------------------------------------
 * simple types and values
 */

#pragma pack(push)
#pragma pack(8)

/* create boolean type to ensure exact size on all platforms */
typedef int16_t itzam_bool;
static const int16_t itzam_true  = -1;
static const int16_t itzam_false =  0;

typedef uint8_t itzam_byte;

#if defined(ITZAM64)
typedef int64_t itzam_ref;
typedef int64_t itzam_int;
#else
typedef int32_t itzam_ref;
typedef int32_t itzam_int;
#endif

static const itzam_ref ITZAM_NULL_REF = -1;
static const itzam_ref ITZAM_NULL_KEY = -1;

typedef enum
{
    ITZAM_ERROR_SIGNATURE,              // 00
    ITZAM_ERROR_VERSION,
    ITZAM_ERROR_64BIT_DB,
    ITZAM_ERROR_WRITE_FAILED,
    ITZAM_ERROR_OPEN_FAILED,
    ITZAM_ERROR_READ_FAILED,
    ITZAM_ERROR_CLOSE_FAILED,
    ITZAM_ERROR_SEEK_FAILED,
    ITZAM_ERROR_TELL_FAILED,
    ITZAM_ERROR_DUPE_REMOVE,
    ITZAM_ERROR_FLUSH_FAILED,           // 10
    ITZAM_ERROR_TOO_SMALL,
    ITZAM_ERROR_PAGE_NOT_FOUND,
    ITZAM_ERROR_LOST_KEY,
    ITZAM_ERROR_KEY_NOT_WRITTEN,
    ITZAM_ERROR_KEY_SEEK_FAILED,
    ITZAM_ERROR_KEY_REMOVE_FAILED,
    ITZAM_ERROR_REC_SEEK_FAILED,
    ITZAM_ERROR_REC_REMOVE_FAILED,
    ITZAM_ERROR_DELLIST_NOT_READ,
    ITZAM_ERROR_DELLIST_NOT_WRITTEN,    // 20
    ITZAM_ERROR_ITERATOR_COUNT,
    ITZAM_ERROR_REWRITE_DELETED,
    ITZAM_ERROR_INVALID_COL,
    ITZAM_ERROR_INVALID_ROW,
    ITZAM_ERROR_INVALID_HASH,
    ITZAM_ERROR_MALLOC,
    ITZAM_ERROR_READ_DELETED,
    ITZAM_ERROR_INVALID_RECORD,
    ITZAM_ERROR_INVALID_FILE_LOCK_MODE,
    ITZAM_ERROR_FILE_LOCK_FAILED,       // 30
    ITZAM_ERROR_UNLOCK_FAILED,
    ITZAM_ERROR_REC_SIZE,
    ITZAM_ERROR_TRANSACTION_OVERLAP,
    ITZAM_ERROR_NO_TRANSACTION,
    ITZAM_ERROR_CURSOR_COUNT,
    ITZAM_ERROR_INVALID_DATAFILE_OBJECT,
    ITZAM_ERROR_SIZE_T,
    ITZAM_ERROR_FILE_CREATE,
    ITZAM_ERROR_SHMEM_PRIVILEGE,
    ITZAM_ERROR_SHMEM,                  // 40
    ITZAM_ERROR_ALREADY_CREATED,
    ITZAM_ERROR_READ_ONLY,
    ITZAM_ERROR_TOO_LONG
} itzam_error;

typedef enum
{
    ITZAM_OKAY,
    ITZAM_FAILED,
    ITZAM_VERSION_ERROR,
    ITZAM_AT_END,
    ITZAM_AT_BEGIN,
    ITZAM_NOT_FOUND,
    ITZAM_DUPLICATE,
    ITZAM_32BIT_OVERFLOW,
    ITZAM_DATA_WRITE_FAILED,
    ITZAM_REF_SIZE_MISMATCH,
    ITZAM_READ_ONLY,
    ITZAM_OVERWRITE_TOO_LONG
} itzam_state;

/*-----------------------------------------------------------------------------
 * shared memory
 */

ITZAM_SHMEM_TYPE itzam_shmem_obtain(const char * name, size_t len, itzam_bool * creator);

void itzam_shmem_close(ITZAM_SHMEM_TYPE shmem, const char * name);

void * itzam_shmem_getptr(ITZAM_SHMEM_TYPE shmem, size_t len);

itzam_bool itzam_shmem_commit(void * shmem_ptr);

itzam_bool itzam_shmem_freeptr(void * shmem_ptr, size_t len);

/*-----------------------------------------------------------------------------
 * generalized file operations
 */

ITZAM_FILE_TYPE itzam_file_create(const char * filename);

ITZAM_FILE_TYPE itzam_file_open(const char * filename);

itzam_bool itzam_file_close(ITZAM_FILE_TYPE datafile);

itzam_bool itzam_file_read(ITZAM_FILE_TYPE datafile, void * data, size_t len);

itzam_bool itzam_file_write(ITZAM_FILE_TYPE datafile, const void * data, size_t len);

itzam_ref itzam_file_seek(ITZAM_FILE_TYPE datafile, itzam_ref pos, int mode);

itzam_ref itzam_file_tell(ITZAM_FILE_TYPE datafile);

itzam_bool itzam_file_commit(ITZAM_FILE_TYPE datafile);

itzam_bool itzam_file_remove(const char * filename);

itzam_bool itzam_file_lock(ITZAM_FILE_TYPE datafile);

itzam_bool itzam_file_unlock(ITZAM_FILE_TYPE datafile);

/*-----------------------------------------------------------------------------
 * general function types
 */

typedef void itzam_error_handler(const char * function_name, itzam_error error);

extern itzam_error_handler * default_error_handler;

void itzam_set_default_error_handler(itzam_error_handler * handler);

/*-----------------------------------------------------------------------------
 * utility function for normalizing system objects' names (mutex, shm, ...)
 */

void itzam_build_normalized_name(char * buffer, const char * format, const char * basename);

/* functions of this type must return:
 *      < 0    key1 is before key2
 *        0    keys are equal
 *      > 0    key1 is after key2
 */
typedef int itzam_key_comparator(const void * key1, const void * key2);

/* function to select keys to be included in a subset
 *      itzam_true    include key
 *      itzam_false   don't include this key
 */
typedef itzam_bool itzam_key_selector(const void * key);

/* built-in key comparisons
 */
int itzam_comparator_int32(const void * key1, const void * key2);
int itzam_comparator_uint32(const void * key1, const void * key2);
int itzam_comparator_string(const void * key1, const void * key2);

/* a callback function used to retrieve whatever data is associated with a reference
 */
typedef itzam_bool itzam_export_callback(itzam_ref ref, void ** record, itzam_int * rec_len);

/*-----------------------------------------------------------------------------
 * datafile structures
 */

/* data file header, prefix for entire data file
 */
static const uint32_t  ITZAM_DATAFILE_SIGNATURE   = 0x4D5A5449; /* ITZM */
static const uint32_t  ITZAM_RECORD_SIGNATURE     = 0x525A5449; /* ITZR */

#if defined(ITZAM64)
static const uint32_t ITZAM_DATAFILE_VERSION      = 0x40050100; /* 64.MM.MM.MM */
#else
static const uint32_t ITZAM_DATAFILE_VERSION      = 0x20050100; /* 32.MM.MM.MM */
#endif

static const itzam_int ITZAM_DELLIST_BLOCK_SIZE   = 256;

/* flags and masks for record identification
 */
static const int32_t ITZAM_RECORD_FLAGS_GENERAL   = 0x000000ff;
static const int32_t ITZAM_RECORD_IN_USE          = 0x00000001;
static const int32_t ITZAM_RECORD_DELLIST         = 0x00000002;
static const int32_t ITZAM_RECORD_SCHEMA          = 0x00000004;
static const int32_t ITZAM_RECORD_TRAN_HEADER     = 0x00000010;
static const int32_t ITZAM_RECORD_TRAN_RECORD     = 0x00000020;

static const int32_t ITZAM_RECORD_FLAGS_BTREE     = 0x00000f00;
static const int32_t ITZAM_RECORD_BTREE_HEADER    = 0x00000100;
static const int32_t ITZAM_RECORD_BTREE_PAGE      = 0x00000200;
static const int32_t ITZAM_RECORD_BTREE_KEY       = 0x00000400;

static const int32_t ITZAM_RECORD_FLAGS_MATRIX    = 0x0000f000;
static const int32_t ITZAM_RECORD_MATRIX_HEADER   = 0x00001000;
static const int32_t ITZAM_RECORD_MATRIX_TABLE    = 0x00002000;

static const int32_t ITZAM_RECORD_FLAGS_HASH      = 0x000f0000;
static const int32_t ITZAM_RECORD_HASH_HEADER     = 0x00010000;
static const int32_t ITZAM_RECORD_HASH_TABLE      = 0x00020000;
static const int32_t ITZAM_RECORD_HASH_KEY        = 0x00040000;
static const int32_t ITZAM_RECORD_HASH_LIST_ENTRY = 0x00080000;

/* record header, prefix for every record in a datafile
 */
typedef struct t_itzam_record_header
{
    uint32_t  m_signature; /* four bytes that identify this as an itzam record */
    uint32_t  m_flags;     /* a set of ITZAM_RECORD_* bit settings */
    itzam_int m_length;    /* record length */
    itzam_int m_rec_len;   /* number of bytes in use by actual data */
}
itzam_record_header;

/* structures for list of deleted records
 */
typedef struct t_itzam_dellist_header
{
    itzam_int   m_table_size; /* number of entries in the table */
}
itzam_dellist_header;

typedef struct t_itzam_dellist_entry
{
    itzam_ref   m_where;    /* reference for this deleted record */
    itzam_int   m_length;   /* length of the deleted record */
}
itzam_dellist_entry;

/* transaction definitions
 */
typedef enum
{
    ITZAM_TRAN_OP_WRITE,
    ITZAM_TRAN_OP_REMOVE,
    ITZAM_TRAN_OP_OVERWRITE
}
itzam_op_type;

/* data file header
 */
typedef struct t_itzam_datafile_header
{
    uint32_t  m_signature;        /* file type signature */
    uint32_t  m_version;          /* version of this file type */
    itzam_ref m_dellist_ref;      /* position of deleted list in file */
    itzam_ref m_schema_ref;       /* position of XML schema that describes this file */
    itzam_ref m_index_list_ref;   /* position of index reference list */
    itzam_ref m_transaction_tail; /* last item in the current transaction */
}
itzam_datafile_header;

/* transaction operation header
 */
typedef struct t_itzam_op_header
{
    itzam_op_type         m_type;
    itzam_ref             m_where;
    itzam_ref             m_prev_tran;
    itzam_record_header   m_record_header;
    itzam_ref             m_record_where;
}
itzam_op_header;

/* definition of shared memory used by all isnatnces of a datafile
 */
typedef struct t_itzam_datafile_shared
{
    int                       m_count;             /* header information */
    itzam_datafile_header     m_header;            /* header information */
#if defined(ITZAM_UNIX)
    pthread_mutex_t           m_mutex;             /* shared mutex */
#endif
}
itzam_datafile_shared;

/* working storage for a loaded datafile
 */
typedef struct t_itzam_datafile
{
    /* OS file information */
    ITZAM_FILE_TYPE           m_file;              /* file associated with this datafile */
    char *                    m_filename;          /* filename for this datafile */

    /* list of deleted records */
    itzam_dellist_header      m_dellist_header;    /* header for current deleted record list */
    itzam_dellist_entry *     m_dellist;           /* pointer to list of deleted records */

    /* transaction tracking file information */
    char *                    m_tran_file_name;    /* transaction filename */
    struct t_itzam_datafile * m_tran_file;         /* transaction file */

#if defined(ITZAM_WINDOWS)
    /* shared (named) mutex */
    HANDLE                    m_mutex;             /* mutex information */
#endif

    /* shared memory, used to communicate between threads and processes */
    ITZAM_SHMEM_TYPE          m_shmem;             /* system link to shared memory */
    char *                    m_shmem_name;        /* name of shared memory block */
    itzam_datafile_shared *   m_shared;            /* items shared among all instances */

    /* error handling */
    itzam_error_handler *     m_error_handler;     /* function to handle errors that occur */

    /* flags */
    itzam_bool                m_is_open;           /* is the file currently open? */
    itzam_bool                m_read_only;         /* is the file open read-only? */
    itzam_bool                m_file_locked;       /* is the file currently locked? */
    itzam_bool                m_in_transaction;    /* are we journalizing a transaction? */
    itzam_bool                m_tran_replacing;    /* set when a write replaces a record during a write */

    /* file locking */
#if defined(ITZAM_UNIX)
    struct flock              m_file_lock;         /* fcntl lock */
#endif
}
itzam_datafile;

/*-----------------------------------------------------------------------------
 * prototypes for variable-length reference data file
 */

itzam_bool itzam_datafile_exists(const char * filename);

itzam_state itzam_datafile_create(itzam_datafile * datafile,
                                  const char * filename);

itzam_state itzam_datafile_open(itzam_datafile * datafile,
                                const char * filename,
                                itzam_bool recover,
                                itzam_bool read_only);

itzam_state itzam_datafile_close(itzam_datafile * datafile);

void itzam_datafile_mutex_lock(itzam_datafile * datafile);

void itzam_datafile_mutex_unlock(itzam_datafile * datafile);

itzam_bool itzam_datafile_file_lock(itzam_datafile * datafile);

itzam_bool itzam_datafile_file_unlock(itzam_datafile * datafile);

itzam_bool itzam_datafile_is_open(itzam_datafile * datafile);

float itzam_datafile_get_version(itzam_datafile * datafile);

void itzam_datafile_set_error_handler(itzam_datafile * datafile,
                                      itzam_error_handler * error_handler);

itzam_ref itzam_datafile_tell(itzam_datafile * datafile);

itzam_state itzam_datafile_seek(itzam_datafile * datafile,
                                itzam_ref pos);

itzam_state itzam_datafile_rewind(itzam_datafile * datafile);

/* INTERNAL FUNCTION -- DO NOT USE EXPLICITLY */
itzam_ref itzam_datafile_get_next_open(itzam_datafile * datafile,
                                       itzam_int length);

itzam_ref itzam_datafile_write_flags(itzam_datafile * datafile,
                                     const void * data,
                                     itzam_int length,
                                     itzam_ref where,
                                     int32_t flags);

itzam_ref itzam_datafile_write(itzam_datafile * datafile,
                               const void * data,
                               itzam_int length,
                               itzam_ref where);

itzam_state itzam_datafile_overwrite(itzam_datafile * datafile,
                                     const void * data,
                                     itzam_int length,
                                     itzam_ref where,
                                     itzam_int offset);

itzam_state itzam_datafile_read(itzam_datafile * datafile,
                                void * data,
                                itzam_int max_length);

itzam_state itzam_datafile_read_alloc(itzam_datafile * datafile,
                                      void ** data,
                                      itzam_int * length);

itzam_state itzam_datafile_remove(itzam_datafile * datafile);

itzam_state itzam_datafile_transaction_start(itzam_datafile * datafile);

itzam_state itzam_datafile_transaction_commit(itzam_datafile * datafile);

itzam_state itzam_datafile_transaction_rollback(itzam_datafile * datafile);

/*-----------------------------------------------------------------------------
 * B-tree types data structures
 */

/* page header, prefix for pages
 */
typedef struct t_itzam_btree_page_header
{
    itzam_ref  m_where;     /* location in page file */
    itzam_ref  m_parent;    /* parent in page file */
    uint16_t   m_key_count; /* actual  # of keys */
}
itzam_btree_page_header;

/* a page in the B-tree
 */
typedef struct t_itzam_btree_page
{
    /* the actual data for this reference
     */
    itzam_byte * m_data;

    /* header information
     */
    itzam_btree_page_header * m_header;

    /* these elements are pointers into m_data
     */
    itzam_byte * m_keys;  /* array of key/data objects */
    itzam_ref  * m_links; /* links to other pages */
}
itzam_btree_page;

/* B-tree constants
 */
static const uint32_t ITZAM_BTREE_VERSION        = 0x00040000;
static const uint16_t ITZAM_BTREE_ORDER_MINIMUM  =  4;
static const uint16_t ITZAM_BTREE_ORDER_DEFAULT  = 25;

/* B-tree header
 */
typedef struct t_itzam_btree_header
{
    uint32_t   m_version;      /* version of this file structure */
    uint32_t   m_sizeof_page;  /* sizeof(itzam_btree_page) used in creating this btreefile */
    uint32_t   m_sizeof_key;   /* size of data being stored in pages */
    uint16_t   m_order;        /* number of keys per page */
    uint64_t   m_count;        /* counts number of active records */
    uint64_t   m_ticker;       /* counts the total number of new records added over the life of this btree datafile*/
    itzam_ref  m_where;        /* pointer to location of this header */
    itzam_ref  m_root_where;   /* pointer to location of root reference in file */
    itzam_ref  m_schema_ref;   /* links to a copy of the XML schema for this table (future use) */
}
itzam_btree_header;

/* working storage for a loaded B-tree
 */
typedef struct t_itzam_btree
{
    itzam_datafile *         m_datafile;          /* file associated with this btree file */
    ITZAM_SHMEM_TYPE         m_shmem_header;      /* memory map for the header */
    char *                   m_shmem_header_name; /* name associated with memory map */
    itzam_btree_header *     m_header;            /* header information */
    ITZAM_SHMEM_TYPE         m_shmem_root;        /* memory map for the header */
    char *                   m_shmem_root_name;   /* name associated with memory map */
    itzam_byte *             m_root_data;         /* actual root data */
    itzam_btree_page         m_root;              /* root structure */
    itzam_bool               m_free_datafile;     /* free the datafile object when closed */
    uint16_t                 m_links_size;        /* number of links per page; order + 1; calculated at creation time */
    uint16_t                 m_min_keys;          /* minimum # of keys; order / 2; calculated at creation time */
    uint16_t                 m_cursor_count;      /* Number of active cursors */
    itzam_key_comparator *   m_key_comparator;    /* function to compare keys */
    itzam_ref                m_saved_header;      /* temporary header saved  during transaction, for use in a rollback */
}
itzam_btree;

/*-----------------------------------------------------------------------------
 * prototypes for B-tree file
 */

itzam_state itzam_btree_create(itzam_btree * btree,
                               const char * filename,
                               uint16_t order,
                               itzam_int key_size,
                               itzam_key_comparator * key_comparator,
                               itzam_error_handler * error_handler);

itzam_state itzam_btree_open(itzam_btree * btree,
                             const char * filename,
                             itzam_key_comparator * key_comparator,
                             itzam_error_handler * error_handler,
                             itzam_bool recover,
                             itzam_bool read_only);

itzam_state itzam_btree_close(itzam_btree * btree);

uint64_t itzam_btree_count(itzam_btree * btree);

uint64_t itzam_btree_ticker(itzam_btree * btree);

void itzam_btree_mutex_lock(itzam_btree * btree);

void itzam_btree_mutex_unlock(itzam_btree * btree);

itzam_bool itzam_btree_file_lock(itzam_btree * btree);

itzam_bool itzam_btree_file_unlock(itzam_btree * btree);

itzam_bool itzam_btree_is_open(itzam_btree * btree);

void itzam_btree_set_error_handler(itzam_btree * btree,
                                   itzam_error_handler * error_handler);

itzam_state itzam_btree_insert(itzam_btree * btree, const void * key);

itzam_bool itzam_btree_find(itzam_btree * btree, const void * search_key, void * result);

itzam_state itzam_btree_remove(itzam_btree * btree, const void * key);

uint16_t itzam_btree_cursor_count(itzam_btree * btree);

itzam_state itzam_btree_transaction_start(itzam_btree * btree);

itzam_state itzam_btree_transaction_commit(itzam_btree * btree);

itzam_state itzam_btree_transaction_rollback(itzam_btree * btree);

/*-----------------------------------------------------------------------------
 * B-tree cursor structures
 */

typedef struct t_itzam_btree_cursor_memory
{
    struct t_itzam_btree_cursor_memory * m_prev;
    itzam_bool   m_done;
    size_t m_index;
}
itzam_btree_cursor_memory;

typedef struct t_itzam_btree_cursor
{
    itzam_btree      * m_btree;
    itzam_btree_page * m_page;
    size_t             m_index;
    itzam_btree_cursor_memory * m_parent_memory;
}
itzam_btree_cursor;

/* B-tree cursor functions
 */

itzam_state itzam_btree_cursor_create(itzam_btree_cursor * cursor, itzam_btree * btree);

itzam_bool itzam_btree_cursor_valid(itzam_btree_cursor * cursor);

itzam_state itzam_btree_cursor_free(itzam_btree_cursor * cursor);

itzam_bool itzam_btree_cursor_next(itzam_btree_cursor * cursor);

itzam_bool itzam_btree_cursor_reset(itzam_btree_cursor * cursor);

itzam_state itzam_btree_cursor_read(itzam_btree_cursor * cursor, void * returned_key);

#pragma pack(pop)

#if defined(__cplusplus)
}
#endif

#endif
