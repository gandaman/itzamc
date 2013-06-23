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
 *  Report an itzam error
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
 * tests
 */

#define REC_SIZE 2560 /// x 4 bytes = 10240 bytes per record

typedef struct record_t
{
    uint32_t m_key;
    uint32_t m_data[REC_SIZE];
}
record;

int compare_recs(const void * key1, const void * key2)
{
    int result = 0;

    record * k1 = (record *)key1;
    record * k2 = (record *)key2;

    if (k1->m_key < k2->m_key)
        result = -1;
    else if (k1->m_key > k2->m_key)
        result = 1;

    return result;
}

itzam_bool test_btree_insert()
{
    itzam_btree  btree;
    itzam_state  state;
    record       rec;
    int n;
    char * filename      = "insert.itz";
    itzam_int test_size  = 1000000;
    int order            = 25;

    time_t start = time(NULL);

    memset(&rec.m_data,0,REC_SIZE * sizeof(uint32_t));
    rec.m_key = 0;

    // banner for this test
    printf("\nItzam/C B-Tree Test\nStraight Insertion Performance\n");
    printf("Please wait while I insert %d records of %d bytes...\n", (int)test_size, (int)(REC_SIZE * sizeof(uint32_t)));

    state = itzam_btree_create(&btree, filename, order, sizeof(record), compare_recs, error_handler);

    if (state != ITZAM_OKAY)
    {
        not_okay(state);
        return itzam_false;
    }

    for (n = 1; n <= test_size; ++ n)
    {
        rec.m_key = n;
        rec.m_data[0] = n;
        rec.m_data[REC_SIZE - 1] = n;

        state = itzam_btree_insert(&btree,(const void *)&rec);

        if (state != ITZAM_OKAY)
        {
            not_okay(state);
            return itzam_false;
        }
    }

    time_t elapsed = time(NULL) - start;

    printf("      database  count: %d\n", (int)btree.m_header->m_count);
    printf("      database ticker: %d\n", (int)btree.m_header->m_ticker);
    printf("       total run time: %d seconds\n", (int)elapsed);
    printf("insertions per second: %f\n", ((double)test_size / (double)elapsed));

    // verify database after benchmark
    itzam_btree_cursor cursor;
    state = itzam_btree_cursor_create(&cursor, &btree);

    if (state == ITZAM_OKAY)
    {
        do
        {
            // get the key pointed to by the cursor
            state = itzam_btree_cursor_read(&cursor,(void *)&rec);

            if (state == ITZAM_OKAY)
            {
                if ((rec.m_key != rec.m_data[0]) || (rec.m_key != rec.m_data[REC_SIZE - 1]))
                {
                    printf("ERROR: record retrieved for %u does not match %u or %u\n",rec.m_key,rec.m_data[0],rec.m_data[REC_SIZE - 1]);
                    break;
                }
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
    {
        not_okay(state);
        return itzam_false;
    }

    return itzam_true;
}

int main(int argc, char* argv[])
{
    int result = EXIT_FAILURE;

    itzam_set_default_error_handler(error_handler);

    if (test_btree_insert())
        result = EXIT_SUCCESS;

    return result;
}
