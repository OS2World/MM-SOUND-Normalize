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
#if STDC_HEADERS
# include <stdlib.h>
# include <unistd.h>
# include <string.h>
# include <math.h>
#else
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif
char *strchr(); char *strrchr();
#endif
#if HAVE_FCNTL_H
# include <fcntl.h>
#endif
/*#include <stdint.h>*/

#if HAVE_BYTESWAP_H
# include <byteswap.h>
#else
# define bswap_16(x) \
    ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))
# define bswap_32(x) \
    ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |       \
     (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#endif /* HAVE_BYTESWAP_H */

#ifndef M_PI
# define M_PI 3.14159265358979323846  /* pi */
#endif

/* sqrt(2)/2 is the RMS amplitude of a 1-amplitude sine wave */
#define SQRT2_2 0.70710678118654752440 /* sqrt(2) / 2*/

#include "getopt.h"

#include "riff.h"

struct wavfmt
{
  uint16_t wFormatTag;              /* Format category */
  uint16_t wChannels;               /* Number of channels */
  uint32_t dwSamplesPerSec;         /* Sampling rate */
  uint32_t dwAvgBytesPerSec;        /* For buffer estimation */
  uint16_t wBlockAlign;             /* Data block size */

  uint16_t wBitsPerSample;          /* Sample size */
};

extern char version[];
char *progname;

void
usage()
{
  fprintf(stderr, "\
Usage: %s [OPTION]... [FILE]...\n\
Create a WAV file containing a sine wave of a given\n\
amplitude and frequency\n\
\n\
  -a, --amplitude=AMP       make sine wave with RMS amplitude AMP\n\
                              [default 0.25]\n\
  -b, --bytes-per-sample=B  write WAV with B bytes per sample [default 2]\n\
  -c, --channels=C          write WAV with C channels [default 1]\n\
  -f, --frequency=F         make a sine wave with frequency F [default 1000]\n\
  -o, --output=FILE         output to filename FILE [default test.wav]\n\
  -r, --sample-rate=R       write WAV with sample rate R [default 44100]\n\
  -s, --samples=S           write S samples [default 1 second worth]\n\
  -V, --version             display version information and exit\n\
  -h, --help                display this help and exit\n\
\n\
Report bugs to <cvaill@cs.columbia.edu>.\n", progname);
}

static __inline__ void
put_sample(long sample, unsigned char *pdata, int bytes_per_sample)
{
  switch(bytes_per_sample) {
  case 1:
    *pdata = sample + 128;
    break;
  case 2:
#ifdef WORDS_BIGENDIAN
    sample = bswap_16(sample);
#endif
    *((int16_t *)pdata) = (int16_t)sample;
    break;
  case 3:
    *pdata = (unsigned char)(sample & 0xFF);
    *(pdata + 1) = (unsigned char)((sample >> 8) & 0xFF);
    *(pdata + 2) = (unsigned char)((sample >> 16) & 0xFF);
    break;
  case 4:
#ifdef WORDS_BIGENDIAN
    sample = bswap_32(sample);
#endif
    *((int32_t *)pdata) = (int32_t)sample;
    break;
  default:
    /* shouldn't happen */
    fprintf(stderr, "%s: I don't know what to do with %d bytes per sample\n",
	    progname, bytes_per_sample);
  }
}

int
main(int argc, char *argv[])
{
  riff_t *riff;
  int fd, c;
  long sample, i;
  struct wavfmt wf;
  unsigned char *sbuf;
  double mconst, gconst, f_samp;
  char *outfile = "test.wav";
  double amp = 0.25;
  long s = 0, freq = 1000;
  int rate = 44100, channels = 1, bytes_per_samp = 2;

  struct option longopts[] = {
    {"help", 0, NULL, 'h'},
    {"version", 0, NULL, 'V'},
    {"amplitude", 1, NULL, 'a'},
    {"bytes-per-sample", 1, NULL, 'b'},
    {"channels", 1, NULL, 'c'},
    {"frequency", 1, NULL, 'f'},
    {"output", 1, NULL, 'o'},
    {"sample-rate", 1, NULL, 'r'},
    {"samples", 1, NULL, 's'},
    {NULL, 0, NULL, 0}
  };

  /* get program name */
  if ((progname = strrchr(argv[0], '/')) == NULL)
    progname = argv[0];
  else
    progname++;

  while ((c = getopt_long(argc, argv, "hVa:b:c:f:o:r:s:",longopts,NULL)) != EOF) {
    switch(c) {
    case 'a':
      amp = strtod(optarg, NULL);
      break;
    case 'b':
      bytes_per_samp = strtol(optarg, NULL, 0);
      break;
    case 'c':
      channels = strtol(optarg, NULL, 0);
      break;
    case 'f':
      freq = strtol(optarg, NULL, 0);
      break;
    case 'o':
      outfile = optarg;
      break;
    case 'r':
      rate = strtol(optarg, NULL, 0);
      break;
    case 's':
      s = strtol(optarg, NULL, 0);
      break;
    case 'V':
      fprintf(stderr, "mktestwav, from normalize %s\n", version);
      exit(0);
    case 'h':
      usage();
      exit(0);
    default:
      usage();
      exit(1);
    }
  }

  /*if (bytes_per_samp != 1 && bytes_per_samp != 2 && bytes_per_samp != 4) {*/
  if (bytes_per_samp < 1 || bytes_per_samp > 4) {
    fprintf(stderr, "%s: %d bytes per sample not supported\n",
	    progname, bytes_per_samp);
    exit(1);
  }
  if (channels < 1) {
    fprintf(stderr, "%s: bad number of channels\n", progname);
    exit(1);
  }

  if (s == 0)
    s = rate;

  sbuf = (unsigned char *)malloc(s * bytes_per_samp * channels);
  if (sbuf == NULL) {
    fprintf(stderr, "%s: unable to malloc!\n", progname);
    exit(1);
  }

  fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);

  if(fd == -1 || (riff = riff_new(fd, RIFF_WRONLY)) == NULL) {
    fprintf(stderr, "%s: error opening output file\n", progname);
    exit(1);
  }

  riff_list_open(riff, riff_string_to_fourcc("WAVE"));

  wf.wFormatTag = 1; /* for PCM wav format */
  wf.wChannels = channels;
  wf.dwSamplesPerSec = rate;
  wf.dwAvgBytesPerSec = rate * bytes_per_samp;
  wf.wBlockAlign = 4;
  wf.wBitsPerSample = bytes_per_samp * 8;
#ifdef WORDS_BIGENDIAN
  wf.wFormatTag       = bswap_16(wf.wFormatTag);
  wf.wChannels        = bswap_16(wf.wChannels);
  wf.dwSamplesPerSec  = bswap_32(wf.dwSamplesPerSec);
  wf.dwAvgBytesPerSec = bswap_32(wf.dwAvgBytesPerSec);
  wf.wBlockAlign      = bswap_16(wf.wBlockAlign);
  wf.wBitsPerSample   = bswap_16(wf.wBitsPerSample);
#endif

  /* Write format header chunk */
  riff_chunk_write(riff, riff_string_to_fourcc("fmt "),
		   sizeof(wf), &wf);

  mconst = freq * 2.0 * M_PI / rate;
  gconst = amp / SQRT2_2;
  for (i = 0; i < s; i++) {
    f_samp = sin(mconst * i) * gconst;
    if (f_samp > 1.0)
      f_samp = 1.0;
    if (f_samp < -1.0)
      f_samp = -1.0;
    sample = f_samp * (0x7FFFFFFF >> (8 * (4 - bytes_per_samp)));
    /*printf("%ld %ld\n", i, sample);*/
    for (c = 0; c < channels; c++) {
      put_sample(sample,
		 sbuf + (i * channels + c) * bytes_per_samp,
		 bytes_per_samp);
    }
  }

  /* Write data chunk */
  riff_chunk_write(riff, riff_string_to_fourcc("data"),
		   s * bytes_per_samp * channels, sbuf);

  riff_list_close(riff);
  riff_unref(riff);
  close(fd);
  free(sbuf);

  return 0;
}
