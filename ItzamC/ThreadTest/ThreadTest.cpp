/**
    Itzam/Galitt is an embedded database engine written in
    Standard C for Galitt. 

    Copyright 2011 Scott Robert Ladd. All rights reserved.

    The author can be contacted at:

          scott.ladd@coyotegulch.com
          scott.ladd@gmail.com
          http:www.coyotegulch.com
*/

#include "itzam.h"

#include <stdlib.h>
#include <iostream>
#include <iomanip>

using namespace std;

#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

/**
 *----------------------------------------------------------
 * embedded random number generator; ala Park and Miller
 */
static int32_t seed = 1325;

void init_test_prng(int32_t s)
{
	seed = s;
}

int32_t random_int32(int32_t limit)
{
    static const int32_t IA   = 16807;
    static const int32_t IM   = 2147483647;
    static const int32_t IQ   = 127773;
    static const int32_t IR   = 2836;
    static const int32_t MASK = 123459876;

    int32_t k;
    int32_t result;

    seed ^= MASK;
    k = seed / IQ;
    seed = IA * (seed - k * IQ) - IR * k;

    if (seed < 0L)
        seed += IM;

    result = (seed % limit);
    seed ^= MASK;

    return result;
}

/**
 *----------------------------------------------------------
 *  Reports an itzam error
 */
static const char * ERROR_STRINGS [] =
{
    "invalid datafile signature",
    "invalid version",
    "can not open 64-bit datafile on 32-bit operating system",
    "write failed",
    "open failed",
    "read failed",
    "close failed",
    "seek failed",
    "tell failed",
    "duplicate remove",
    "flush failed",
    "rewrite record too small",
    "page not found",
    "lost key",
    "key not written",
    "key seek failed",
    "unable to remove key record",
    "record seek failed",
    "unable to remove data record",
    "list of deleted records could not be read",
    "list of deleted records could not be written",
    "iterator record count differs from database internal count",
    "rewrite over deleted record",
    "invalid column index",
    "invalid row index",
    "invalid hash value",
    "memory allocation failed",
    "attempt reading deleted record",
    "invalid record signature found",
    "invalid file locking mode",
    "unable to lock datafile",
    "unable to unlock datafile",
    "size mismatch when reading record",
    "attempt to start new transaction while one is already active",
    "no transaction active",
    "attempt to free a B-tree cursor when cursors were active",
    "invalid datafile object",
    "size_t is incompatible with Itzam",
    "could not create datafile",
    "global shared memory requires Administrator or user with SeCreateGlobalPrivilege",
    "cannot create global shared memory",
    "another process or thread has already created shared objects for this datafile",
    "invalid operation for read only file"
};

static const char * STATE_MESSAGES [] =
{
    "okay",
    "operation failed",
    "version mismatch in files",
    "iterator at end",
    "iterator at beginning",
    "key not found",
    "duplicate key",
    "exceeded maximum file size on 32-bit system",
    "unable to write data record for index",
    "sizeof(size_t) smaller than required for file references; possibly 64-bit DB on 32-bit platform",
    "invalid operation for read only file"
};

void not_okay(itzam_state state)
{
    cerr << "\nItzam problem: " << STATE_MESSAGES[state] << endl;
    exit(EXIT_FAILURE);
}

void error_handler(const char * function_name, itzam_error error)
{
    cerr << "Itzam error in " << function_name << ": " << ERROR_STRINGS[error] << endl;
    exit(EXIT_FAILURE);
}

itzam_bool test_btree_thread(char * filename, int maxkey, int test_size)
{
    itzam_state state;
    itzam_btree btree;
    int32_t     key;
    int i;

    state = itzam_btree_open(&btree, filename, itzam_comparator_int32, error_handler, false, false);

    for (i = 1; i <= test_size; ++i)
    {
        key = (int32_t)random_int32((size_t)maxkey);

        if (ITZAM_OKAY != itzam_btree_insert(&btree,(const void *)&key))
            itzam_btree_remove(&btree,(const void *)(&key));
    }

    itzam_btree_close(&btree);

    return itzam_true;
}

struct threadArgs
{
    char * filename;
    int maxkey;
    int test_size;
};

DWORD WINAPI threadProc(LPVOID ARGS)
{
    struct threadArgs * args = ((struct threadArgs *)ARGS);
    test_btree_thread(args->filename, args->maxkey, args->test_size);
    return 0;
}

#define NTHREADS 5

static itzam_bool test_threaded()
{
    int n;
    const uint16_t order = 25;
    int32_t rec;
    char * filename = "threaded.itz";
    time_t start;
    int maxkey = 1000;
    int test_size = 200000;

    DWORD id[NTHREADS];
    HANDLE thread[NTHREADS];
    struct threadArgs args[NTHREADS];

    itzam_btree btree;
    itzam_btree_cursor cursor;
    itzam_state state;

    cout << "\nTesting Itzam/C b-tree indexes with multiple threads" << endl;

    /* create an empty database file
     */
    if (!itzam_datafile_exists(filename))
    {
        state = itzam_btree_create(&btree, filename, order, sizeof(int32_t), itzam_comparator_int32, error_handler);

        if (state != ITZAM_OKAY)
        {
            cout << "uable to create index file" << endl;
            return itzam_false;
        }

        itzam_btree_close(&btree);
    }

    /* create threads and test simultaneous access tot he database
     */
    cout << "Please wait while " << NTHREADS << " threads are created and executed" << endl;

    start = time(NULL);

    for (n = 0; n < NTHREADS; ++n)
    {
        args[n].filename = filename;
        args[n].maxkey = maxkey;
        args[n].test_size = test_size;
        thread[n] = CreateThread(NULL, 0, threadProc, &args[n], 0, &id[n]);
    }

    for (n = 0; n < NTHREADS; ++n)
        WaitForSingleObject(thread[n],INFINITE);

    /* stats
     */
    time_t elapsed = time(NULL) - start;

    cout << "       total run time: " << elapsed << endl;
    cout << "operations per second: " << ((double)NTHREADS * (double)test_size / double(elapsed)) << endl << endl;

    return itzam_true;
}

int main(int argc, char* argv[])
{
    int result = EXIT_FAILURE;

    itzam_set_default_error_handler(error_handler);

    init_test_prng((long)time(NULL));

    if (test_threaded())
        result = EXIT_SUCCESS;

    return result;
}

