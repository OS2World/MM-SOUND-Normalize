/* Copyright (C) 1999--2001 Chris Vaill
   This file is part of normalize.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifndef _RIFF_H_
#define _RIFF_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#include <stdio.h>
#include <sys/types.h>


/* RIFF data types */
typedef uint32_t fourcc_t;

typedef struct _riff_t riff_t;

typedef struct _riff_chunk_t {
  int ref;
  long offset;
  riff_t *riff_file;

  fourcc_t id;
  unsigned int size;
  fourcc_t type; /* just for RIFF and LIST chunks */

  /* in each chunk, exactly one of the following two pointers should
     be NULL. */
  unsigned char *data;
  FILE *fp;

} riff_chunk_t;

/* definitions for the id field of riff_chunk_t */
#ifdef WORDS_BIGENDIAN
# define RIFFID_RIFF ((fourcc_t)0x52494646) /* "RIFF" in big-endian */
# define RIFFID_LIST ((fourcc_t)0x4C495354) /* "LIST" in big-endian */
# define RIFFID_JUNK ((fourcc_t)0x4A554E4B) /* "JUNK" in big-endian */
#else
# define RIFFID_RIFF ((fourcc_t)0x46464952) /* "RIFF" in little-endian */
# define RIFFID_LIST ((fourcc_t)0x5453494C) /* "LIST" in little-endian */
# define RIFFID_JUNK ((fourcc_t)0x4B4E554A) /* "JUNK" in little-endian */
#endif

/* definitions for riff access permissions */
#define RIFF_RDONLY 1
#define RIFF_WRONLY 2
#define RIFF_RDWR   (RIFF_RDONLY | RIFF_WRONLY)


      riff_t *riff_new(int fd, int mode);
         void riff_unref(riff_t *riff);
         void riff_destroy(riff_t *riff);

riff_chunk_t *riff_chunk_read(riff_t *riff);
         void riff_chunk_unref(riff_chunk_t *chnk);
         void riff_chunk_destroy(riff_chunk_t *chnk);

        FILE *riff_chunk_get_stream(riff_chunk_t *chnk);
        void *riff_chunk_get_data(riff_chunk_t *chnk);
          int riff_chunk_write(riff_t *riff, fourcc_t id,
			       unsigned int size, void *data);

          int riff_list_open(riff_t *riff, fourcc_t type);
          int riff_list_close(riff_t *riff);
          int riff_list_descend(riff_t *riff, riff_chunk_t *chnk);
          int riff_list_ascend(riff_t *riff);

          int riff_fourcc_equals(fourcc_t fcc, char *s);
#define       riff_string_to_fourcc(s) (*((fourcc_t *)s))


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _RIFF_H_ */
