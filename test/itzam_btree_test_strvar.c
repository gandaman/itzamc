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

#include "../src/itzam.h"
#include "itzam_errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

/*----------------------------------------------------------
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

/*----------------------------------------------------------
 *  Reports an itzam error
 */
void not_okay(itzam_state state)
{
    fprintf(stderr, "\nItzam problem: %s\n", STATE_MESSAGES[state]);
    exit(EXIT_FAILURE);
}

void error_handler(const char * function_name, itzam_error error)
{
    fprintf(stderr, "Itzam error in %s: %s\n", function_name, ERROR_STRINGS[error]);
    exit(EXIT_FAILURE);
}

/*----------------------------------------------------------
 * Key record
 */
#define MAX_KEY_LEN  24
#define REC_LEN_BASE 10
#define REC_LEN_EXT  90

typedef struct t_key_type
{
    char      m_key[MAX_KEY_LEN];
    itzam_int m_rec_len;
    itzam_ref m_rec_ref;
}
key_type;

// could be shorter, but want to be clear
int compare_keys(const void * key1, const void * key2)
{
    const key_type * k1 = (const key_type *)key1;
    const key_type * k2 = (const key_type *)key2;

    return strcmp(k1->m_key,k2->m_key);
}

/*----------------------------------------------------------
 * tests
 */
itzam_bool test_btree_strvar()
{
    itzam_btree  btree;
    itzam_btree_cursor cursor;
    itzam_state  state;
    int          n, i, klen;
    char *       filename  = "strvar.itz";
    itzam_int    test_size = 100;
    int          order = 25;
    key_type     key;
    char *       record;
    itzam_int    len_read;

    // banner for this test
    printf("\nItzam/C B-Tree Test\nString Keys & Variable-Length Records\n");

    state = itzam_btree_create(&btree, filename, order, sizeof(key_type), compare_keys, error_handler);

    if (state != ITZAM_OKAY)
        not_okay(state);

    for (n = 1; n <= test_size; ++ n)
    {
        /* generate a random alphabetic key */
        memset(key.m_key,0,MAX_KEY_LEN);

        klen = random_int32((int32_t)(MAX_KEY_LEN - 9)) + 9;

        for (i = 0; i < klen; ++i)
            key.m_key[i] = 'A' + random_int32(26);

        /* generate the record */
        key.m_rec_len = REC_LEN_BASE + (itzam_int)random_int32((int32_t)REC_LEN_EXT);

        record = (char *)malloc(key.m_rec_len);

        memset(record, key.m_key[0], key.m_rec_len);

        /* write record directly to the datafile */
        key.m_rec_ref = itzam_datafile_write(btree.m_datafile, (void *)record, key.m_rec_len, ITZAM_NULL_REF);

        /* if it worked, write the key */
        if (key.m_rec_ref != ITZAM_NULL_REF)
        {
            state = itzam_btree_insert(&btree,(const void *)&key);

            if (ITZAM_OKAY != state)
                not_okay(state);
        }
        else
        {
            fprintf(stderr,"Unable to write database record\n");
            exit(EXIT_FAILURE);
        }

        printf("wrote record at key %s\n",key.m_key);

        free(record);
    }

    // now use a cursor to show the records stored, in key order
    state = itzam_btree_cursor_create(&cursor, &btree);

    if (state == ITZAM_OKAY)
    {
        do
        {
            // get the key pointed to by the cursor
            state = itzam_btree_cursor_read(&cursor,(void *)&key);

            if (state == ITZAM_OKAY)
            {
                // seek to record
                state = itzam_datafile_seek(btree.m_datafile, key.m_rec_ref);

                if (state == ITZAM_OKAY)
                {
                    // read record
                    state = itzam_datafile_read_alloc(btree.m_datafile,(void **)&record,&len_read);

                    if (state == ITZAM_OKAY)
                    {
                        // verify that record matches key
                        for (i = 0; i < key.m_rec_len; ++i)
                        {
                            if (key.m_key[0] != record[i])
                            {
                                printf("ERROR: record retrieved for %s does not match\n",key.m_key);
                                break;
                            }
                        }

                        printf("record retrieved for %s matches\n",key.m_key);

                        free(record); // was allocated by the datafile read;
                    }
                    else
                        not_okay(state);
                }
                else
                    not_okay(state);
            }
            else
                not_okay(state);
        }
        while (itzam_btree_cursor_next(&cursor));

        state = itzam_btree_cursor_free(&cursor);

        if (state != ITZAM_OKAY)
            return itzam_false;
    }

    state = itzam_btree_close(&btree);

    if (state != ITZAM_OKAY)
        return itzam_false;

    return itzam_true;
}

int main(int argc, char* argv[])
{
    int result = EXIT_FAILURE;

    itzam_set_default_error_handler(error_handler);

    init_test_prng((long)time(NULL));

    if (test_btree_strvar())
        result = EXIT_SUCCESS;

    return result;
}

