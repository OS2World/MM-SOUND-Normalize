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

#define _POSIX_C_SOURCE 2

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef STDC_HEADERS
# include <string.h>
#else
# ifdef HAVE_STRERROR
char *strerror();
# else
#  define strerror(x) "Unknown error"
# endif
#endif
#ifdef HAVE_SYS_MMAN_H
# include <sys/mman.h>
#endif
#if HAVE_BYTESWAP_H
# include <byteswap.h>
#else
# define bswap_16(x) \
    ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))
# define bswap_32(x) \
    ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |       \
     (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#endif /* HAVE_BYTESWAP_H */

#include "riff.h"


#define MIN(a,b) ((a)<(b)?(a):(b))

/* we use a stack to keep track of the nestedness of list chunks */
struct _riff_chunk_stack_t {
  long start_off;
  long end_off;
  struct _riff_chunk_stack_t *next;
};

typedef struct _riff_chunk_stack_t *riff_chunk_stack_t;

struct _riff_t {
  int ref;
  FILE *fp; /* needed for stream writing */
  long file_off; /* current file offset */
  int mode;

  riff_chunk_stack_t stack;

  /* stream to which messages are written */
  FILE *msg_stream;
};

/*
 * converts the integer x to a 4-byte little-endian array
 */
static void
_int_to_buffer_lendian(unsigned char *buf, unsigned int x)
{
  buf[0] = x & 0xFF;
  buf[1] = (x >> 8) & 0xFF;
  buf[2] = (x >> 16) & 0xFF;
  buf[3] = (x >> 24) & 0xFF;  
}

#if 0
static void
_stack_print(riff_chunk_stack_t stack)
{
  fprintf(stderr, "STACK:");
  while(stack) {
    fprintf(stderr, " (%lX,%lX)", stack->start_off, stack->end_off);
    stack = stack->next;
  }
  fprintf(stderr, "\n");
}
#endif

static int
_stack_push(riff_chunk_stack_t *stack, long start, long end)
{
  riff_chunk_stack_t newelt;

  newelt = (riff_chunk_stack_t)malloc(sizeof(struct _riff_chunk_stack_t));
  if(newelt == NULL) {
    fprintf(stderr, "riff: unable to malloc!\n");
    return -1;
  }
  newelt->start_off = start;
  newelt->end_off = end;
  newelt->next = *stack;
  *stack = newelt;

  return 0;
}

static void
_stack_pop(riff_chunk_stack_t *stack)
{
  riff_chunk_stack_t tmp;
  if(*stack != NULL) {
    tmp = *stack;
    *stack = (*stack)->next;
    free(tmp);
  }
}

riff_t *
riff_new(int fd, int mode)
{
  riff_t *riff;
  char *fmode;
  int dup_fd;
  long file_len;
  struct stat st;

  riff = (riff_t *)malloc(sizeof(riff_t));
  if(riff == NULL) {
    errno = ENOMEM;
    return NULL;
  }

  riff->mode = mode;
  riff->msg_stream = stderr;
  riff->file_off = lseek(fd, 0, SEEK_CUR);


  /* Open file stream */
  switch(riff->mode) {
  case RIFF_RDONLY: fmode = "rb";  break;
  case RIFF_WRONLY: fmode = "wb";  break;
  case RIFF_RDWR:   fmode = "r+b"; break;
  default:
    if(riff->msg_stream)
      fprintf(riff->msg_stream, "riff: bad riff mode %d\n", riff->mode);
    free(riff);
    errno = EINVAL;
    return NULL;
  }
  dup_fd = dup(fd);
  riff->fp = fdopen(dup_fd, fmode);
  if(riff->fp == NULL) {
    close(dup_fd);
    free(riff);
    return NULL;
  }


  if(riff->mode == RIFF_RDONLY || riff->mode == RIFF_RDWR) {

    /* Initialize the chunk stack */
    riff->stack = NULL;
    if(fstat(fd, &st) == -1) {
      free(riff);
      return NULL;
    }
    file_len = st.st_size;
    if(_stack_push(&riff->stack, riff->file_off, file_len) == -1) {
      free(riff);
      return NULL;
    }

  } else if(riff->mode == RIFF_WRONLY) {

    /* Initialize the chunk stack */
    riff->stack = NULL;
    /* don't know length yet, just push 0 for length */
    if(_stack_push(&riff->stack, riff->file_off, 0) == -1) {
      free(riff);
      return NULL;
    }

  }

  /* set reference count to 1 */
  riff->ref = 1;

  return riff;
}

void
riff_unref(riff_t *riff)
{
  riff->ref--;
  if(riff->ref == 0)
    riff_destroy(riff);
}

void
riff_destroy(riff_t *riff)
{
  while(riff->stack != NULL)
    _stack_pop(&riff->stack);
  fclose(riff->fp);
  free(riff);
}


/*
 * Get the next chunk in the file
 */
riff_chunk_t *
riff_chunk_read(riff_t *riff)
{
  riff_chunk_t *new_chnk;
  long remaining;
  fourcc_t chnk_id;
  unsigned int chnk_size;

  remaining = riff->stack->end_off - riff->file_off;
  if(remaining < 8) {
    if(remaining > 0)
      if(riff->msg_stream)
	fprintf(riff->msg_stream,
		"riff: trailing junk at offset 0x%08lX ignored\n",
		riff->file_off);
    errno = 0; /* no error, there's just no more chunks */
    return NULL;
  }

  /* allocate chunk */
  new_chnk = (riff_chunk_t *)malloc(sizeof(riff_chunk_t));
  if(new_chnk == NULL) {
    if(riff->msg_stream)
      fprintf(riff->msg_stream, "riff: unable to malloc!\n");
    return NULL;
  }
  new_chnk->ref = 1;
  new_chnk->offset = riff->file_off;
  new_chnk->riff_file = riff; riff->ref++;
  new_chnk->fp = NULL;
  new_chnk->data = NULL;

  if(fseek(riff->fp, new_chnk->offset, SEEK_SET) == -1) {
    riff_chunk_destroy(new_chnk);
    if(riff->msg_stream)
      fprintf(riff->msg_stream, "riff: unable to fseek: %s\n",
	      strerror(errno));
    return NULL;
  }

  /* read ID and size */
  fread(&chnk_id, 4, 1, riff->fp);
  fread(&chnk_size, 4, 1, riff->fp);

#ifdef WORDS_BIGENDIAN
  /* reverse bytes on big endian machines */
  chnk_size = bswap_32(chnk_size);
#endif

  new_chnk->id = chnk_id;
  new_chnk->size = chnk_size;

  /* if it's a RIFF or LIST, read the type */
  if(chnk_id == RIFFID_RIFF || chnk_id == RIFFID_LIST) {

    if(remaining < 12) {
      if(riff->msg_stream)
	fprintf(riff->msg_stream,
		"riff: bad RIFF or LIST chunk at offset 0x%08lX\n",
		riff->file_off);
      riff_chunk_destroy(new_chnk);
      errno = EINVAL;
      return NULL;
    }

    fread(&new_chnk->type, 4, 1, riff->fp);

  } else { /* it's not a RIFF or LIST */

    /* if we're at the top level (which we can tell by the stack),
       this should be a RIFF chunk */
    if(riff->stack->next == NULL) {
      if(riff->msg_stream)
	fprintf(riff->msg_stream,
		"riff: bad top-level chunk id at offset 0x%08lX\n",
		riff->file_off);
      riff_chunk_destroy(new_chnk);
      errno = EINVAL;
      return NULL;
    }
  }

  /* check size against size of containing chunk */
  if(remaining - 8 < chnk_size) {
    if(riff->msg_stream)
      fprintf(riff->msg_stream, "riff: chunk at offset 0x%08lX has bad size\n",
	      riff->file_off);
    chnk_size = remaining - 8;
  }

  /* seek to beginning of next chunk */
  if(chnk_size & 1)
    chnk_size++;
  riff->file_off += chnk_size + 8;

  return new_chnk;
}


/*
 * Get the stream for reading/writing data to the chunk
 */
FILE *
riff_chunk_get_stream(riff_chunk_t *chnk)
{
  int dup_fd;
  char *fmode;
  riff_t *riff = chnk->riff_file;

  if(chnk->fp != NULL)
    return chnk->fp;

  /* Open file stream */
  switch(riff->mode) {
  case RIFF_RDONLY: fmode = "rb";  break;
  case RIFF_WRONLY: fmode = "wb";  break;
  case RIFF_RDWR:   fmode = "r+b"; break;
  default:
    if(riff->msg_stream)
      fprintf(riff->msg_stream, "riff: bad riff mode %d\n", riff->mode);
    fmode = "rb";
  }

  /* dup the file desc before opening a stream so we can close the
     stream later without pulling the original file desc out from
     under any other chunks */
  dup_fd = dup(fileno(riff->fp));
  chnk->fp = fdopen(dup_fd, fmode);
  if(chnk->fp == NULL) {
    if(riff->msg_stream)
      fprintf(riff->msg_stream, "riff: unable to fdopen: %s\n",
	      strerror(errno));
    return NULL;
  }

  /* seek to beginning of chunk data (skip id & size) */
  if(fseek(chnk->fp, chnk->offset + 8, SEEK_SET) == -1) {
    fclose(chnk->fp);
    chnk->fp = NULL;
    if(riff->msg_stream)
      fprintf(riff->msg_stream, "riff: unable to fseek: %s\n",
	      strerror(errno));
    return NULL;
  }

  return chnk->fp;
}


/*
 * Get the data for this chunk as a (mmap'ed) buffer
 */
void *
riff_chunk_get_data(riff_chunk_t *chnk)
{
  if(chnk->data == NULL) {
    ; /* TODO: do the mmap */
  }
  return chnk->data;
}


/*
 * Right now, this can only append the chunk on the end of the riff
 */
int
riff_chunk_write(riff_t *riff, fourcc_t id, unsigned int size, void *data)
{
  unsigned char buf[8];

  if(fseek(riff->fp, riff->file_off, SEEK_SET) == -1)
    return -1;
  memcpy(buf, &id, 4);
  _int_to_buffer_lendian(buf + 4, size);
  if(fwrite(buf, 1, 8, riff->fp) == -1)
    return -1;

  if(fwrite(data, 1, size, riff->fp) != size)
    return -1;

  riff->file_off += size + 8;
  return 0;
}

void
riff_chunk_unref(riff_chunk_t *chnk)
{
  chnk->ref--;
  if(chnk->ref == 0)
    riff_chunk_destroy(chnk);
}

void
riff_chunk_destroy(riff_chunk_t *chnk)
{
  riff_unref(chnk->riff_file);
  if(chnk->fp)
    fclose(chnk->fp);
  free(chnk);
}

/*
 * start a new list (for writing)
 */
int
riff_list_open(riff_t *riff, fourcc_t type)
{
  fourcc_t id;
  char buf[12] = "    \0\0\0\0";

  /* if this is a top-level list, the ID is "RIFF", otherwise it's "LIST" */
  id = (riff->stack->next == NULL) ? RIFFID_RIFF : RIFFID_LIST;

  if(_stack_push(&riff->stack, riff->file_off, 0) == -1)
    return -1;
  memcpy(buf, &id, 4);       /* chunk id */
  ;                          /* chunk length (already zeroed) */
  memcpy(buf + 8, &type, 4); /* list type */
  if(fseek(riff->fp, riff->file_off, SEEK_SET) == -1) {
    _stack_pop(&riff->stack);
    return -1;
  }
  if(fwrite(buf, 1, 12, riff->fp) == -1) {
    _stack_pop(&riff->stack);
    return -1;
  }
  riff->file_off += 12;

  return 0;
}

int
riff_list_close(riff_t *riff)
{
  unsigned char buf[4];
  unsigned int len;

  if(fseek(riff->fp, riff->stack->start_off + 4, SEEK_SET) == -1) {
    if(riff->msg_stream)
      fprintf(riff->msg_stream, "riff: unable to seek\n");
    return -1;
  }
  len = riff->file_off - (riff->stack->start_off + 8);
  _int_to_buffer_lendian(buf, len);
  if(fwrite(buf, 4, 1, riff->fp) == -1) {
    if(riff->msg_stream)
      fprintf(riff->msg_stream, "riff: unable to write to stream\n");
    return -1;
  }
  _stack_pop(&riff->stack);
  return 0;
}

/*
 * "step into" the given list (for reading)
 */
int
riff_list_descend(riff_t *riff, riff_chunk_t *chnk)
{
  if(_stack_push(&riff->stack, chnk->offset, chnk->offset + 8 + chnk->size)
     == -1) {
    return -1;
  }
  riff->file_off = chnk->offset + 12;
  return 0;
}

/*
 * "step out" of the current list (for reading)
 */
int
riff_list_ascend(riff_t *riff)
{
  if(riff->stack->next == NULL) {
    /* we're already at the highest level */
    return -1;
  }

  riff->file_off = riff->stack->end_off;
  _stack_pop(&riff->stack);
  return 0;
}

int
riff_fourcc_equals(fourcc_t fcc, char *s)
{
  fourcc_t other = *((fourcc_t *)s);
  return (fcc == other);
}
