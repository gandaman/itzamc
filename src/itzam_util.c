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
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(ITZAM_UNIX)
#include <sys/mman.h>
#endif

/*-----------------------------------------------------------------------------
 * default error handler, for errors outside the scope of valid datafile objects
 */
void default_default_error_handler(const char * function_name, itzam_error error)
{
    /* default error handler displays a simple message and exits program
     */
    fprintf(stderr,"Itzam internal error in %s: %d\n",function_name,error);
    exit(1);
}

itzam_error_handler * default_error_handler = default_default_error_handler;

void itzam_set_default_error_handler(itzam_error_handler * handler)
{
    if (handler != NULL)
        default_error_handler = handler;
}

/*-----------------------------------------------------------------------------
 * shared memory
 */

ITZAM_SHMEM_TYPE itzam_shmem_obtain(const char * name, size_t len, itzam_bool * creator)
{
#if defined(ITZAM_UNIX)
    int result;

    result = shm_open(name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);

    if (result < 0)
    {
        if (errno == EEXIST)
        {
            *creator = itzam_false;

            result = shm_open(name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

            if (result < 0)
                default_error_handler("itzam_shmem_obtain",ITZAM_ERROR_SHMEM);
        }
    }
    else
    {
        *creator = itzam_true;

        if (ftruncate(result, len))
            default_error_handler("itzam_shmem_obtain",ITZAM_ERROR_SHMEM);
    }

    return result;
#else
    HANDLE result;

    result = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, (LPCSTR)name);

    if ((result == NULL) && (len > 0))
    {
        *created = itzam_true;
        result = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)len, (LPCTSTR)name);
    }
    else
        *created = itzam_false;

    if (result == NULL)
    {
        if (ERROR_ACCESS_DENIED == GetLastError())
            default_error_handler("itzam_shmem_obtain",ITZAM_ERROR_SHMEM_PRIVILEGE);
        else
            default_error_handler("itzam_shmem_obtain",ITZAM_ERROR_SHMEM);
    }

    return result;
#endif
}

void itzam_shmem_close(ITZAM_SHMEM_TYPE shmem, const char * name)
{
#if defined(ITZAM_UNIX)
    close(shmem);
    shm_unlink(name);
#else
    if (shmem != NULL)
        CloseHandle(shmem);
#endif
}

void * itzam_shmem_getptr(ITZAM_SHMEM_TYPE shmem, size_t len)
{
#if defined(ITZAM_UNIX)
    void * result = mmap(NULL, len, PROT_READ | PROT_WRITE,  MAP_SHARED, shmem, 0);

    if (result == MAP_FAILED)
        default_error_handler("itzam_shmem_obtain",ITZAM_ERROR_SHMEM);

    return result;
#else
    void * result = NULL;

    result = (void *)MapViewOfFile(shmem, FILE_MAP_ALL_ACCESS, 0, 0, len);

    if (result == NULL)
    {
        if (ERROR_ACCESS_DENIED == GetLastError())
            default_error_handler("itzam_shmem_obtain",ITZAM_ERROR_SHMEM_PRIVILEGE);
        else
            default_error_handler("itzam_shmem_obtain",ITZAM_ERROR_SHMEM);
    }

    return result;
#endif
}

itzam_bool itzam_shmem_freeptr(void * shmem_ptr, size_t len)
{
#if defined(ITZAM_UNIX)
    return (itzam_bool)(0 == munmap(shmem_ptr,len));
#else
    return (itzam_bool)UnmapViewOfFile(shmem_ptr);
#endif
}

/*-----------------------------------------------------------------------------
 * file I/O functions
 */

ITZAM_FILE_TYPE itzam_file_create(const char * filename)
{
#if defined(ITZAM_UNIX)
    return open(filename, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
#else
    HANDLE result = NULL;

    do
    {
        result = CreateFile((LPCSTR)filename,
                            GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL,
                            CREATE_ALWAYS,
                            FILE_FLAG_RANDOM_ACCESS, // | FILE_FLAG_WRITE_THROUGH,
                            NULL);

        if ((result != NULL) && (result != INVALID_HANDLE_VALUE))
            break;

        if ((result == INVALID_HANDLE_VALUE) && (GetLastError() == ERROR_ACCESS_DENIED))
            Sleep(250);
        else
            break;
    }
    while (1);

    return result;

#endif
}

ITZAM_FILE_TYPE itzam_file_open(const char * filename)
{
#if defined(ITZAM_UNIX)
    return open(filename, O_RDWR);
#else
    return CreateFile((LPCSTR)filename,
                      GENERIC_READ | GENERIC_WRITE,
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                      NULL,
                      OPEN_EXISTING,
                      FILE_FLAG_RANDOM_ACCESS, // | FILE_FLAG_WRITE_THROUGH,
                      NULL);

#endif
}

itzam_bool itzam_file_close(ITZAM_FILE_TYPE file)
{
#if defined(ITZAM_UNIX)
    return (itzam_bool)(0 == close(file));
#else
    return (itzam_bool)CloseHandle(file);
#endif
}

itzam_bool itzam_file_read(ITZAM_FILE_TYPE file, void * data, size_t len)
{
#if defined(ITZAM_UNIX)
    return (itzam_bool)(len == read(file,data,len));
#else
    DWORD count;
    return (itzam_bool)ReadFile(file, (LPVOID)data, (DWORD)len, &count, NULL);
#endif
}

itzam_bool itzam_file_write(ITZAM_FILE_TYPE file, const void * data, size_t len)
{
#if defined(ITZAM_UNIX)
    return (itzam_bool)(len == write(file,data,len));
#else
    DWORD count;
    return (itzam_bool)WriteFile(file,(LPVOID)data, (DWORD)len, &count, NULL);
#endif
}

itzam_ref itzam_file_seek(ITZAM_FILE_TYPE file, itzam_ref pos, int mode)
{
#if defined(ITZAM_UNIX)
    return (itzam_ref)lseek(file,pos,mode);
#else
    return (itzam_ref)SetFilePointer(file, (LONG)pos, NULL, mode);
#endif
}

itzam_ref itzam_file_tell(ITZAM_FILE_TYPE file)
{
#if defined(ITZAM_UNIX)
    return (itzam_ref)lseek(file,0,SEEK_CUR);
#else
    return (itzam_ref)SetFilePointer(file, 0, NULL, FILE_CURRENT);
#endif
}

itzam_bool itzam_file_commit(ITZAM_FILE_TYPE file)
{
#if defined(ITZAM_UNIX)
    return true; // (itzam_bool)commit(file);
#else
    return (itzam_bool)FlushFileBuffers(file);
#endif
}

itzam_bool itzam_file_remove(const char * filename)
{
#if defined(ITZAM_UNIX)
    return (itzam_bool)remove(filename);
#else
    return (itzam_bool)DeleteFile((LPCSTR)filename);
#endif
}
