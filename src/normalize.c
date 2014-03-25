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
#include <errno.h>
#include <time.h>

#if STDC_HEADERS
# include <stdlib.h>
# include <unistd.h>
# include <string.h>
# include <ctype.h>
# include <math.h>
#else
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif
char *strchr(); char *strrchr();
# ifndef HAVE_MEMCPY
#  define memcpy(d,s,n) bcopy((s),(d),(n))
#  define memmove(d,s,n) bcopy((s),(d),(n))
# endif
#endif

#if HAVE_FCNTL_H
# include <fcntl.h>
#endif
#if HAVE_SYS_STAT_H
# include <sys/stat.h>
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

#if HAVE_GETHOSTNAME
extern int gethostname();
#else
# define gethostname(a, b) strcpy((a), "?")
#endif

#if HAVE_LOCALE_H
# include <locale.h>
#endif

#if ENABLE_NLS
# include <libintl.h>
# define _(s) gettext (s)
#else
# define _(s) (s)
#endif

#include "getopt.h"

#include "riff.h"

#define USE_TEMPFILE 0
#define USE_LOOKUPTABLE 1

#define AMPTODBFS(x) (20 * log10(x))
#define FRACTODB(x) (20 * log10(x))
#define DBFSTOAMP(x) pow(10,(x)/20.0)
#define DBTOFRAC(x) pow(10,(x)/20.0)

#ifndef MIN
# define MIN(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef MAX
# define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#ifndef FALSE
# define FALSE  (0)
#endif
#ifndef TRUE
# define TRUE   (!FALSE)
#endif

/* anything less than EPSILON is considered zero */
#ifndef EPSILON
# define EPSILON 0.00000000001
#endif

/* warn about clipping if we clip more than this fraction of the samples */
#define CLIPPING_WARN_THRESH 0.001

struct wavfmt {
  uint16_t format_tag;              /* Format category */
  uint16_t channels;                /* Number of channels */
  uint32_t samples_per_sec;         /* Sampling rate */
  uint32_t avg_bytes_per_sec;       /* For buffer estimation */
  uint16_t block_align;             /* Data block size */

  uint16_t bits_per_sample;         /* Sample size */
};

struct signal_info {
  double level;      /* maximum sustained RMS amplitude */
  double peak;       /* peak amplitude */
  long max_sample;   /* maximum sample value */
  long min_sample;   /* minimum sample value */
  struct wavfmt fmt; /* WAV format info */
};

struct progress_struct {
  time_t file_start;   /* what time we started processing the file */
  time_t batch_start;  /* what time we started processing the batch */
  off_t *file_sizes;   /* sizes of each file, in kb */
  off_t batch_size;    /* sum of all file sizes, in kb */
  off_t finished_size; /* sum of sizes of all completed files, in kb */
  int on_file;         /* the index of the file we're working on */
} progress_info;

void compute_levels(struct signal_info *sis, char **fnames, int nfiles);
double average_levels(struct signal_info *sis, int nfiles, double threshold);
double signal_max_power(int fd, char *filename, struct signal_info *psi);
double signal_max_power_stream(FILE *in, char *filename,
			       struct signal_info *psi);
int apply_gain(int read_fd, int write_fd, char *filename, double gain,
	       struct signal_info *psi);
#if 0
double amp_to_dBFS(double x);
double frac_to_dB(double x);
double dBFS_to_amp(double x);
double dB_to_frac(double x);
#endif
#if USE_TEMPFILE
int xmkstemp(char *template);
int xrename(const char *oldpath, const char *newpath);
#endif
int strncaseeq(const char *s1, const char *s2, size_t n);
void *xmalloc(size_t size);

extern char version[];
char *progname;


void
usage()
{
  fprintf(stderr, _("\
Usage: %s [OPTION]... [FILE]...\n\
Normalize volume of multiple WAV files\n\
\n\
  -a, --amplitude=AMP  normalize the RMS volume to the target amplitude\n\
                         AMP [default 0.25 or -12dBFS]\n\
  -n, --no-adjust      compute and output the volume adjustment, but\n\
                         don't apply it to any of the files\n\
  -g, --gain=ADJ       don't compute the volume adjustment, just apply\n\
                         adjustment ADJ to the files.  Use the suffix \"dB\"\n\
                         to indicate a gain in decibels.\n\
  -b, --batch          batch mode: average the levels of all files given\n\
                         on the command line, and use one adjustment, based\n\
                         on the average level, for all files\n\
  -m, --mix            mix mode: average the levels of all files given\n\
                         on the command line, and normalize volume of\n\
                         each file to the average level\n\
  -t, --threshold=THR  in batch mode, throw out any level values more\n\
                         than THR decibels different from the average.\n\
                         By default, use twice the standard deviation of\n\
                         all the power levels.\n\
  -c, --compression    do dynamic range compression, instead of clipping.\n\
      --peak           adjust using peak levels instead of RMS levels.\n\
                         Each file will be adjusted so that its maximum\n\
                         sample is at full scale.\n\
  -v, --verbose        increase verbosity\n\
  -q, --quiet          quiet (decrease verbosity to zero)\n\
  -V, --version        display version information and exit\n\
  -h, --help           display this help and exit\n\
\n\
Report bugs to <cvaill@cs.columbia.edu>.\n"), progname);
}

enum verbose_t {
  VERBOSE_QUIET    = 0,
  VERBOSE_PROGRESS = 1,
  VERBOSE_INFO     = 2,
  VERBOSE_DEBUG    = 3,
};

enum {
  OPT_CLIPPING     = 1,
  OPT_PEAK         = 2,
  OPT_FRACTIONS    = 3,
};

/* arguments */
int verbose = VERBOSE_PROGRESS;
int do_print_only = FALSE;
int do_apply_gain = TRUE;
double target = 0.25;
double threshold = -1.0; /* in decibels */
int do_compute_levels = TRUE;
int gain_in_decibels = FALSE;
int batch_mode = FALSE;
int mix_mode = FALSE;
int do_compression = FALSE;
int use_peak = FALSE;
int use_fractions = FALSE;

int
main(int argc, char *argv[])
{
  int fd, fd2, c, i, nfiles;
  struct signal_info *sis, *psi;
  double level, gain = 1.0, dBdiff;
  char **fnames, *p;
  struct stat st;
  int files_changed = FALSE;

  struct option longopts[] = {
    {"help", 0, NULL, 'h'},
    {"version", 0, NULL, 'V'},
    {"no-adjust", 0, NULL, 'n'},
    {"quiet", 0, NULL, 'q'},
    {"verbose", 0, NULL, 'v'},
    {"batch", 0, NULL, 'b'},
    {"amplitude", 1, NULL, 'a'},
    {"threshold", 1, NULL, 't'},
    {"gain", 1, NULL, 'g'},
    {"mix", 0, NULL, 'm'},
    {"compression", 0, NULL, 'c'},
    {"clipping", 0, NULL, OPT_CLIPPING},
    {"peak", 0, NULL, OPT_PEAK},
    {"fractions", 0, NULL, OPT_FRACTIONS},
    {NULL, 0, NULL, 0}
  };

#ifdef __EMX__
  /* This gives wildcard expansion on Non-POSIX shells with OS/2 */
  _wildcard(&argc, &argv);
#endif

  /* get program name */
  if ((progname = strrchr(argv[0], '/')) == NULL)
    progname = argv[0];
  else
    progname++;
  if (strlen(progname) > 16)
    progname[16] = '\0';

#if ENABLE_NLS
  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);
#endif

  /* get args */
  while ((c = getopt_long(argc, argv, "hVnvqbmcg:a:t:", longopts, NULL)) != EOF) {
    switch(c) {
    case 'a':
      target = strtod(optarg, &p);

      /* check if "dB" or "dBFS" is given after number */
      while(isspace(*p))
	p++;
      if (strncaseeq(p, "db", 2)) {

	/* amplitude given as dBFS */
	if (target > 0) {
	  target = -target;
	  fprintf(stderr, _("%s: Warning: assuming you meant -%s...\n"),
		  progname, optarg);
	}
	/* translate to fraction */
	target = DBFSTOAMP(target);

      } else {

	/* amplitude given as fraction */
	if (target < 0 || target > 1.0) {
	  usage();
	  exit(1);
	}
      }
      break;
    case 't':
      /* a negative threshold means don't use threshold (use 2*stddev) */
      threshold = strtod(optarg, NULL);
      break;
    case 'g':
      gain = strtod(optarg, &p);

      /* check if "dB" is given after number */
      while(isspace(*p))
	p++;
      if (strncaseeq(p, "db", 2)) {
	dBdiff = gain;
	gain = DBTOFRAC(dBdiff);
	gain_in_decibels = TRUE;
      }

      do_compute_levels = FALSE;
      batch_mode = TRUE;
      if (gain < 0) {
	usage();
	exit(1);
      }
      break;
    case 'n':
      do_print_only = TRUE;
      do_apply_gain = FALSE;
      break;
    case 'b':
      batch_mode = TRUE;
      break;
    case 'm':
      mix_mode = TRUE;
      break;
    case 'c':
      do_compression = TRUE;
      break;
    case OPT_CLIPPING:
      /* just for compatibility with 0.5 */
      do_compression = FALSE;
      break;
    case OPT_PEAK:
      use_peak = TRUE;
      do_compression = FALSE;
      break;
    case OPT_FRACTIONS:
      use_fractions = TRUE;
      break;
    case 'v':
      verbose++;
      break;
    case 'q':
      verbose = VERBOSE_QUIET;
      break;
    case 'V':
      fprintf(stderr, "normalize %s\n", version);
      exit(0);
    case 'h':
      usage();
      exit(0);
    default:
      usage();
      exit(1);
    }
  }
  if (mix_mode && batch_mode) {
    fprintf(stderr,
	    _("%s: error: the -m and -b options are mutually exclusive\n"),
	    progname);
    exit(1);
  }
  if (use_peak && (mix_mode || batch_mode)) {
    fprintf(stderr,
	    _("%s: error: -m and -b can't be used with the --peak option\n"),
	    progname);
    exit(1);
  }
  if (optind >= argc) {
    usage();
    exit(1);
  }


  /*
   * get sizes of all files, for progress calculation
   */
  nfiles = 0;
  progress_info.batch_size = 0;
  fnames = (char **)xmalloc((argc - optind) * sizeof(char *));
  progress_info.file_sizes = (off_t *)xmalloc((argc - optind) * sizeof(off_t));
  for (i = optind; i < argc; i++) {
    if (strcmp(argv[i], "-") == 0) {
      if (do_apply_gain) {
	fprintf(stderr, _("%s: Warning: stdin specified on command line, not adjusting files\n"), progname);
	do_apply_gain = FALSE;
      }
      fnames[nfiles++] = argv[i];
    } else if (stat(argv[i], &st) == -1) {
      fprintf(stderr, _("%s: file %s: %s\n"),
	      progname, argv[i], strerror(errno));
    } else {
      /* we want the size of the data chunk in kilobytes, so subtract
         the size of the wav header and divide by size of kb */
      progress_info.file_sizes[nfiles] = (st.st_size - 36) / 1024;
      /* add the size of the file, in kb */
      progress_info.batch_size += progress_info.file_sizes[nfiles];
      fnames[nfiles] = argv[i];
      nfiles++;
    }
  }
  if (nfiles == 0) {
    fprintf(stderr, _("%s: no files!\n"), progname);
    return 1;
  }

  /* allocate space to store levels and peaks */
  sis = (struct signal_info *)xmalloc(nfiles * sizeof(struct signal_info));


  /*
   * Compute the levels
   */
  if (do_compute_levels) {
    compute_levels(sis, fnames, nfiles);

    /* anything that came back with a level of -1 was bad, so remove it */
    for (i = 0; i < nfiles; i++) {
      if (sis[i].level < 0) {
	nfiles--;
	memmove(sis + i, sis + i + 1,
		(nfiles - i) * sizeof(struct signal_info));
	memmove(fnames + i, fnames + i + 1,
		(nfiles - i) * sizeof(char *));
	memmove(progress_info.file_sizes + i, progress_info.file_sizes + i + 1,
		(nfiles - i) * sizeof(off_t));
      }
    }

    if (batch_mode || mix_mode) {
      level = average_levels(sis, nfiles, threshold);

      /* For mix mode, we set the target to the average level */
      if (mix_mode)
	target = level;

      /* For batch mode, we use one gain for all files */
      if (batch_mode)
	gain = target / level;

      if (do_print_only) {
	if (use_fractions)
	  printf(_("%-12.6f average level\n"), level);
	else
	  printf(_("%-8.4fdBFS average level\n"), AMPTODBFS(level));
      } else if (verbose >= VERBOSE_INFO) {
	if (use_fractions)
	  fprintf(stderr, _("Average level: %0.4f\n"), level);
	else
	  fprintf(stderr, _("Average level: %0.4fdBFS\n"), AMPTODBFS(level));
      }
    }

  } /* end of if (do_compute_levels) */


  /*
   * Check if we need to apply the gain --
   *
   *   Volume differences of less than 1 dB are not audible.  We
   *   therefore allow amplitudes that are +/-0.25 dB from the target
   *   to pass without adjustment (mainly so that normalizing files
   *   for the second time has no effect).
   *
   *   Why 0.25 dB?  If we allow amplitudes that are 0.25 dB above and
   *   below the target, the total possible range is 0.5 dB, which is
   *   well under the 1 dB hearing threshold.
   */
  if (do_apply_gain && batch_mode) {
    if (!gain_in_decibels)
      dBdiff = FRACTODB(gain);

    /* the do_compute_levels check makes sure we always apply gain if
       it was specified with -g */
    if (do_compute_levels && fabs(dBdiff) < 0.25) {
      if (verbose >= VERBOSE_PROGRESS)
	fprintf(stderr,
		_("Files are already normalized, not adjusting...\n"));
      do_apply_gain = FALSE;
    } else if (verbose >= VERBOSE_PROGRESS) {
      if (!do_compute_levels) { /* if -g */
	if (gain_in_decibels)
	  printf(_("Applying adjustment of %fdB...\n"), dBdiff);
	else
	  printf(_("Applying adjustment of %f...\n"), gain);
      } else {
	printf(_("Applying adjustment of %0.2fdB...\n"), dBdiff);
      }
    }
  }


  /*
   * Apply the gain
   */
  if (do_apply_gain) {
#if USE_TEMPFILE
    struct stat stbuf;
    char *tmpfile, *p;
#endif

    progress_info.batch_start = time(NULL);
    progress_info.finished_size = 0;

    for (i = 0; i < nfiles; i++) {

      fd = open(fnames[i], O_RDONLY);
      if (fd == -1) {
	fprintf(stderr, _("%s: error opening %s: %s\n"), progname, fnames[i],
		strerror(errno));
	continue;
      }

      if (!batch_mode) {
	if (use_peak)
	  gain = 1.0 / sis[i].peak;
	else
	  gain = target / sis[i].level;
	dBdiff = FRACTODB(gain);

	/* don't bother applying very small adjustments -- see above */
	if (fabs(dBdiff) < 0.25) {
	  if (verbose >= VERBOSE_PROGRESS)
	    fprintf(stderr, _("%s already normalized, not adjusting...\n"),
		    fnames[i]);
	  continue;
	}

	if (verbose >= VERBOSE_PROGRESS)
	  fprintf(stderr, _("Applying adjustment of %0.2fdB to %s...\n"),
		  dBdiff, fnames[i]);
      }

#if USE_TEMPFILE
      /* Create temporary file name, and open it for writing.  We want
       * it to be in the same directory (and therefore, in the same
       * filesystem, for a fast rename). */
      tmpfile = (char *)xmalloc(strlen(fnames[i]) + 16);
      strcpy(tmpfile, fnames[i]);
      if ((p = strrchr(tmpfile, '/')) == NULL)
	p = tmpfile;
      else
	p++;
      strcpy(p, "_normXXXXXX");
      fd2 = xmkstemp(tmpfile);
      if (fd2 == -1) {
	fprintf(stderr, _("%s: error opening temp file: %s\n"), progname,
		strerror(errno));
	close(fd);
	free(tmpfile);
	continue;
      }

      /* preserve original permissions */
      fstat(fd, &stbuf);
      fchmod(fd2, stbuf.st_mode);
#else
      fd2 = open(fnames[i], O_WRONLY);
      if (fd2 == -1) {
	fprintf(stderr, _("%s: error opening %s: %s\n"), progname, fnames[i],
		strerror(errno));
	close(fd);
	continue;
      }
#endif

      progress_info.file_start = time(NULL);
      progress_info.on_file = i;

      psi = do_compute_levels ? &sis[i] : NULL;
      if (apply_gain(fd, fd2, fnames[i], gain, psi) == -1) {
	fprintf(stderr, _("%s: error applying adjustment to %s: %s\n"),
		progname, fnames[i], strerror(errno));
      }
      files_changed = TRUE;

      progress_info.finished_size += progress_info.file_sizes[i];

      close(fd);
      close(fd2);

#if USE_TEMPFILE
      /* move the temporary file back to the original file */
      if (xrename(tmpfile, fnames[i]) == -1) {
	fprintf(stderr, _("%s: error moving %s to %s: %s\n"), progname,
		tmpfile, fnames[i], strerror(errno));
	exit(1);
      }
      free(tmpfile);
#endif

      if (verbose >= VERBOSE_PROGRESS && !batch_mode)
	fprintf(stderr, "\n");
    }

    /* we're done with the second progress meter, so go to next line */
    if (verbose >= VERBOSE_PROGRESS && batch_mode)
      fputc('\n', stderr);

  } else if (batch_mode && do_print_only) {

    /* if we're not applying the gain, just print it out, and we're done */
    if (use_fractions) {
      printf(_("%-12f volume adjustment\n"), gain);
    } else {
      char cbuf[32];
      sprintf(cbuf, "%fdB", FRACTODB(gain));
      printf(_("%-12s volume adjustment\n"), cbuf);
    }

  } /* end of if (do_apply_gain) */

  free(sis);
  free(progress_info.file_sizes);
  free(fnames);

  /* if the -n option was given but we didn't adjust any files, return
   * exit status 2 to let scripts know nothing was changed */
  if (!files_changed && !do_print_only)
    return 2;

  return 0;
}

/*
 * Compute the RMS levels of the files.
 */
void
compute_levels(struct signal_info *sis, char **fnames, int nfiles)
{
  double power;
  int i, fd;
  char cbuf[32];
  struct wavfmt fmt = { 1, 2, 44100, 176400, 0, 16 };

  if (verbose >= VERBOSE_PROGRESS) {
    fprintf(stderr, _("Computing levels...\n"));

    if (do_print_only) {
      if (batch_mode)
	fprintf(stderr, _("  level        peak\n"));
      else
	fprintf(stderr, _("  level        peak         gain\n"));
    }
  }

  progress_info.batch_start = time(NULL);
  progress_info.finished_size = 0;

  for (i = 0; i < nfiles; i++) {

    sis[i].level = 0;

    if (strcmp(fnames[i], "-") == 0) {
      progress_info.file_start = time(NULL);
      progress_info.on_file = i;
      errno = 0;

      /* for a stream, format info is passed through sis[i].fmt */
      memcpy(&sis[i].fmt, &fmt, sizeof(struct wavfmt));
      power = signal_max_power_stream(stdin, NULL, &sis[i]);
      fnames[i] = "STDIN";

    } else {

      fd = open(fnames[i], O_RDONLY);
      if (fd == -1) {
	fprintf(stderr, _("%s: error opening %s: %s\n"), progname, fnames[i],
		strerror(errno));
	sis[i].level = -1;
	goto error_update_progress;
      }

      progress_info.file_start = time(NULL);
      progress_info.on_file = i;
      errno = 0;

      power = signal_max_power(fd, fnames[i], &sis[i]);
    }

    if (power < 0) {
      fprintf(stderr, _("%s: error reading %s: %s\n"), progname, fnames[i],
	      strerror(errno));
      sis[i].level = -1;
      goto error_close_fd;
    }
    if (power < EPSILON) {
      if (verbose >= VERBOSE_PROGRESS) {
	fprintf(stderr,
		"\r                                     "
		"                                     \r");
	fprintf(stderr,
		_("File %s has zero power, ignoring...\n"), fnames[i]);
      }
      sis[i].level = -1;
      goto error_close_fd;
    }

    if (do_print_only) {

      /* clear the progress meter first */
      if (verbose >= VERBOSE_PROGRESS)
	fprintf(stderr,
		"\r                                     "
		"                                     \r");

      if (use_fractions)
	sprintf(cbuf, "%0.6f", sis[i].level);
      else
	sprintf(cbuf, "%0.4fdBFS", AMPTODBFS(sis[i].level));
      printf("%-12s ", cbuf);
      if (use_fractions)
	sprintf(cbuf, "%0.6f", sis[i].peak);
      else
	sprintf(cbuf, "%0.4fdBFS", AMPTODBFS(sis[i].peak));
      printf("%-12s ", cbuf);
      if (!batch_mode) {
	if (use_fractions)
	  sprintf(cbuf, "%0.6f", target / sis[i].level);
	else
	  sprintf(cbuf, "%0.4fdB", AMPTODBFS(target / sis[i].level));
	printf("%-10s ", cbuf);
      }
      printf("%s\n", fnames[i]);

    } else if (verbose >= VERBOSE_INFO) {
      fprintf(stderr,
	      "\r                                     "
	      "                                     \r");
      if (use_fractions)
	fprintf(stderr, _("Level for %s: %0.4f (%0.4f peak)\n"),
		fnames[i], sis[i].level, sis[i].peak);
      else
	fprintf(stderr, _("Level for %s: %0.4fdBFS (%0.4fdBFS peak)\n"),
		fnames[i], AMPTODBFS(sis[i].level), AMPTODBFS(sis[i].peak));
    }

  error_close_fd:
    if (strcmp(fnames[i], "STDIN") != 0)
      close(fd);

  error_update_progress:
    progress_info.finished_size += progress_info.file_sizes[i];
  }

  /* we're done with the level calculation progress meter, so go to
     next line */
  if (verbose == VERBOSE_PROGRESS && !do_print_only)
    fputc('\n', stderr);
}

/*
 * For batch mode, we take the levels for all the input files, throw
 * out any that appear to be statistical aberrations, and average the
 * rest together to get one level and one gain for the whole batch.
 */
double
average_levels(struct signal_info *sis, int nlevels, double threshold)
{
  int i, files_to_avg;
  double sum, level_difference, std_dev, variance;
  double level, mean_level;
  char *badlevels;

  /* badlevels is a boolean array marking the level values to be thrown out */
  badlevels = (char *)xmalloc(nlevels * sizeof(char));
  memset(badlevels, 0, nlevels * sizeof(char));

  /* get mean level */
  sum = 0;
  for (i = 0; i < nlevels; i++)
    sum += sis[i].level;
  mean_level = sum / nlevels;

  /* if no threshold is specified, use 2 * standard dev */
  if (threshold < 0.0) {

    /*
     * We want the standard dev of the levels, but we need it in decibels.
     * Therefore, if u is the mean, the variance is
     *                  (1/N)summation((20*log10(x/u))^2)
     *       instead of (1/N)summation((x-u)^2),
     * which it would be if we needed straight variance "by the numbers".
     */

    /* get variance */
    sum = 0;
    for (i = 0; i < nlevels; i++) {
      double tmp = FRACTODB(sis[i].level / mean_level);
      sum += tmp * tmp;
    }
    variance = sum / nlevels;

    /* get standard deviation */
    if (variance < EPSILON)
      std_dev = 0.0;
    else
      std_dev = sqrt(variance);
    if (verbose >= VERBOSE_INFO)
      fprintf(stderr, _("Standard deviation is %0.2f dB\n"), std_dev);

    threshold = 2 * std_dev;
  }

  /*
   * Throw out level values that seem to be aberrations
   * (so that one "quiet song" doesn't throw off the average)
   * We define an aberration as a level that is > 2*stddev dB from the mean.
   */
  if (threshold > EPSILON && nlevels > 1) {
    for (i = 0; i < nlevels; i++) {

      /* Find how different from average the i'th file's level is.
       * The "level" here is actually the signal's maximum amplitude,
       * from which we can compute the difference in decibels. */
      level_difference = fabs(FRACTODB(mean_level / sis[i].level));

      /* mark as bad any level that is > threshold different than the mean */
      if (level_difference > threshold) {
	if (verbose >= VERBOSE_INFO) {
	  if (use_fractions) {
	    fprintf(stderr,
		_("Throwing out level of %0.4f (different by %0.2fdB)\n"),
		    sis[i].level, level_difference);
	  } else {
	    fprintf(stderr,
		_("Throwing out level of %0.4fdBFS (different by %0.2fdB)\n"),
		    AMPTODBFS(sis[i].level), level_difference);
	  }
	}
	badlevels[i] = TRUE;
      }
    }
  }

  /* throw out the levels marked as bad */
  files_to_avg = 0;
  sum = 0;
  for (i = 0; i < nlevels; i++)
    if (!badlevels[i]) {
      sum += sis[i].level;
      files_to_avg++;
    }

  if (files_to_avg == 0) {
    fprintf(stderr, _("%s: all files ignored, try using -t 100\n"), progname);
    exit(1);
  }

  free(badlevels);

  level = sum / files_to_avg;

  return level;
}

#define LINE_LENGTH 79
void
progress_callback(char *prefix, float fraction_completed)
{
  char buf[LINE_LENGTH + 32]; /* need +1, but +32 in case of huge ETA's */
  time_t now, time_spent;
  unsigned int file_eta_hr, file_eta_min, file_eta_sec;
  off_t kb_done;
  float batch_fraction;
  unsigned int batch_eta_hr, batch_eta_min, batch_eta_sec;

  if (fraction_completed <= 0.0) {
    if (progress_info.batch_size == 0) {
      fprintf(stderr,
        _(" %s  --%% done, ETA --:--:-- (batch  --%% done, ETA --:--:--)"),
	      prefix);
    } else {
      batch_fraction = (progress_info.finished_size
			/ (float)progress_info.batch_size);
      fprintf(stderr,
        _(" %s  --%% done, ETA --:--:-- (batch %3.0f%% done, ETA --:--:--)"),
	      prefix, batch_fraction * 100);
    }
    fprintf(stderr, "\r");
    return;
  }

  now = time(NULL);
  if (fraction_completed > 1.0)
    fraction_completed = 1.0;

  /* figure out the ETA for this file */
  file_eta_hr = file_eta_sec = file_eta_min = 0;
  time_spent = now - progress_info.file_start;
  if (fraction_completed == 0.0)
    file_eta_sec = 0;
  else
    file_eta_sec = floor((float)time_spent / fraction_completed
			 - (float)time_spent + 0.5);
  file_eta_min = file_eta_sec / 60;
  file_eta_sec = file_eta_sec % 60;
  file_eta_hr = file_eta_min / 60;
  file_eta_min = file_eta_min % 60;
  if (file_eta_hr > 99)
    file_eta_hr = 99;

  /* figure out the ETA for the whole batch */
  kb_done = progress_info.finished_size
    + fraction_completed * progress_info.file_sizes[progress_info.on_file];
  batch_fraction = (float)kb_done / (float)progress_info.batch_size;
  batch_eta_hr = batch_eta_min = batch_eta_sec = 0;
  time_spent = now - progress_info.batch_start;
  batch_eta_sec = floor((float)time_spent / batch_fraction
			- (float)time_spent + 0.5);

  batch_eta_min = batch_eta_sec / 60;
  batch_eta_sec = batch_eta_sec % 60;
  batch_eta_hr = batch_eta_min / 60;
  batch_eta_min = batch_eta_min % 60;
  if (batch_eta_hr > 99)
    batch_eta_hr = 99;

  sprintf(buf, _(" %s %3.0f%% done, ETA %02d:%02d:%02d (batch %3.0f%% done, ETA %02d:%02d:%02d)"),
	  prefix, fraction_completed * 100,
	  file_eta_hr, file_eta_min, file_eta_sec,
	  batch_fraction * 100,
	  batch_eta_hr, batch_eta_min, batch_eta_sec);

  fprintf(stderr, "%s\r", buf);
}


static __inline__ long
get_sample(unsigned char *pdata, int bytes_per_sample)
{
  long sample;

  switch(bytes_per_sample) {
  case 1:
    sample = *pdata - 128;
    break;
  case 2:
#ifdef WORDS_BIGENDIAN
    sample = *((int8_t *)pdata + 1) << 8;
    sample |= *((int8_t *)pdata) & 0xFF;
#else
    sample = *((int16_t *)pdata);
#endif
    break;
  case 3:
    sample = *((int8_t *)pdata + 2) << 16;
    sample |= (*((int8_t *)pdata + 1) << 8) & 0xFF00;
    sample |= *((int8_t *)pdata) & 0xFF;
    break;
  case 4:
    sample = *((int32_t *)pdata);
#ifdef WORDS_BIGENDIAN
    sample = bswap_32(sample);
#endif
    break;
  default:
    /* shouldn't happen */
    fprintf(stderr,
	    _("%s: I don't know what to do with %d bytes per sample\n"),
	    progname, bytes_per_sample);
    sample = 0;
  }

  return sample;
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
    *pdata = (unsigned char)sample;
    *(pdata + 1) = (unsigned char)(sample >> 8);
    *(pdata + 2) = (unsigned char)(sample >> 16);
    break;
  case 4:
#ifdef WORDS_BIGENDIAN
    sample = bswap_32(sample);
#endif
    *((int32_t *)pdata) = (int32_t)sample;
    break;
  default:
    /* shouldn't happen */
    fprintf(stderr,
	    _("%s: I don't know what to do with %d bytes per sample\n"),
	    progname, bytes_per_sample);
  }
}


static riff_chunk_t *
get_wav_data(riff_t *riff, struct wavfmt *fmt)
{
  riff_chunk_t *chnk;

  chnk = riff_chunk_read(riff);
  if (chnk == NULL) {
    fprintf(stderr, _("%s: error reading riff chunk\n"), progname);
    goto error2;
  }
  if (!riff_fourcc_equals(chnk->id, "RIFF")) {
    fprintf(stderr, _("%s: not a RIFF WAV file\n"), progname);
    goto error3;
  }
  riff_list_descend(riff, chnk);
  riff_chunk_unref(chnk);

  /* read format header */
  chnk = riff_chunk_read(riff);
  if (chnk == NULL) {
    fprintf(stderr, _("%s: error reading riff chunk\n"), progname);
    goto error2;
  }
  if (!riff_fourcc_equals(chnk->id, "fmt ")) {
    fprintf(stderr, _("%s: no format chunk found\n"), progname);
    goto error3;
  }
  fread(fmt, sizeof(struct wavfmt), 1, riff_chunk_get_stream(chnk));
  riff_chunk_unref(chnk);
#ifdef WORDS_BIGENDIAN
  fmt->format_tag        = bswap_16(fmt->format_tag);
  fmt->channels          = bswap_16(fmt->channels);
  fmt->samples_per_sec   = bswap_32(fmt->samples_per_sec);
  fmt->avg_bytes_per_sec = bswap_32(fmt->avg_bytes_per_sec);
  fmt->block_align       = bswap_16(fmt->block_align);
  fmt->bits_per_sample   = bswap_16(fmt->bits_per_sample);
#endif

  /*
   * Make sure we can handle this type of wav
   */
  if (fmt->format_tag != 1) {
    fprintf(stderr, _("%s: this is a non-PCM WAV file\n"), progname);
    errno = EINVAL;
    goto error3;
  }
  if (fmt->bits_per_sample > 32) {
    fprintf(stderr, _("%s: more than 32 bits per sample not implemented\n"),
	    progname);
    errno = EINVAL;
    goto error3;
  }

  /* read until data chunk */
  chnk = NULL;
  do {

    if (chnk) {
      riff_chunk_unref(chnk);
      chnk = NULL;
    }

    errno = 0;
    chnk = riff_chunk_read(riff);
    if (chnk == NULL) {
      fprintf(stderr, _("%s: no data chunk found\n"), progname);
      if (errno == 0)
	errno = EINVAL;
      goto error2;
    }

  } while (!riff_fourcc_equals(chnk->id, "data"));

  return chnk;

  /* error handling stuff */
 error3:
  riff_chunk_unref(chnk);
 error2:
  return NULL;
}

typedef struct {
  double *buf;
  int buflen;  /* elements allocated to buffer */
  int start;   /* index of first element in buffer */
  int n;       /* num of elements in buffer */
} datasmooth_t;

/*
 * Takes a full smoothing window, and returns the value of the center
 * element, smoothed.  Currently, just does a mean filter, but we could
 * do a median or gaussian filter here instead.
 */
static __inline__ double
get_smoothed_data(datasmooth_t *s)
{
  int i;
  /*int center = (s->n + 1) / 2;*/
  double smoothed;

  smoothed = 0;
  for (i = 0; i < s->n; i++)
    smoothed += s->buf[i];
  smoothed = smoothed / s->n;

  return smoothed;
}


/*
 * Get the maximum power level of the wav file
 * (and the peak sample, if ppeak is not NULL)
 */
double
signal_max_power(int fd, char *filename, struct signal_info *psi)
{
  riff_t *riff;
  riff_chunk_t *chnk;
  struct wavfmt *fmt;
  unsigned int nsamples;

  int bytes_per_sample;
  int last_window;
  unsigned int windowsz;
  unsigned int win_start, old_start, win_end, old_end;

  int i, c;
  long sample, samplemax, samplemin;
  double *sums;
  double pow, maxpow;
  datasmooth_t *powsmooth;

  float progress, last_progress = 0.0;
  char prefix_buf[18];

  FILE *in;
  unsigned char *data_buf = NULL;
  int filled_sz;


  riff = riff_new(fd, RIFF_RDONLY);
  if (riff == NULL) {
    fprintf(stderr, _("%s: error making riff object\n"), progname);
    goto error1;
  }

  /* WAV format info will be passed back */
  fmt = &psi->fmt;

  chnk = get_wav_data(riff, fmt);
  if (chnk == NULL) {
    fprintf(stderr, _("%s: error getting wav data\n"), progname);
    goto error2;
  }
#if DEBUG
  if (verbose >= VERBOSE_DEBUG) {
    fprintf(stderr,
	    "fmt chunk for %s:\n"
	    "  format_tag:        %u\n"
	    "  channels:          %u\n"
	    "  samples_per_sec:   %u\n"
	    "  avg_bytes_per_sec: %u\n"
	    "  block_align:       %u\n"
	    "  bits_per_sample:   %u\n",
	    filename, fmt->format_tag, fmt->channels, fmt->samples_per_sec,
	    fmt->avg_bytes_per_sec, fmt->block_align, fmt->bits_per_sample);
  }
#endif
  windowsz = (unsigned int)(fmt->samples_per_sec / 100);

  bytes_per_sample = (fmt->bits_per_sample - 1) / 8 + 1;
  samplemax = (1 << (bytes_per_sample * 8 - 1)) - 1;
  samplemin = -samplemax - 1;
  nsamples = chnk->size / bytes_per_sample / fmt->channels;
  /* initialize peaks to effectively -inf and +inf */
  psi->max_sample = samplemin;
  psi->min_sample = samplemax;
#if DEBUG
  if (verbose >= VERBOSE_DEBUG) {
    fprintf(stderr,
	    "bytes_per_sample: %d nsamples: %d\n"
	    "samplemax: %ld samplemin: %ld\n",
	    bytes_per_sample, nsamples, samplemax, samplemin);
  }
#endif

  sums = (double *)xmalloc(fmt->channels * sizeof(double));
  for (c = 0; c < fmt->channels; c++)
    sums[c] = 0;

  data_buf = (unsigned char *)xmalloc(windowsz
				      * fmt->channels * bytes_per_sample);

  /* set up smoothing window buffer */
  powsmooth = (datasmooth_t *)xmalloc(fmt->channels * sizeof(datasmooth_t));
  for (c = 0; c < fmt->channels; c++) {
    powsmooth[c].buflen = 100; /* use a 100-element (1 second) window */
    powsmooth[c].buf = (double *)xmalloc(powsmooth[c].buflen * sizeof(double));
    powsmooth[c].start = powsmooth[c].n = 0;
  }

  /* initialize progress meter */
  if (verbose >= VERBOSE_PROGRESS) {
    if (strrchr(filename, '/') != NULL) {
      filename = strrchr(filename, '/');
      filename++;
    }
    strncpy(prefix_buf, filename, 17);
    prefix_buf[17] = 0;
    progress_callback(prefix_buf, 0.0);
    last_progress = 0.0;
  }

  in = fdopen(fd, "r");
  if (in == NULL) {
    fprintf(stderr, _("%s: failed fdopen: %s\n"),
	    progname, strerror(errno));
    goto error7;
  }
  fseek(in, chnk->offset + 8, SEEK_SET);


  /*
   * win_start, win_end, old_start, windowsz, interval, and i are in
   * units of samples.  c is in units of channels.
   *
   * The actual window extends from win_start to win_end - 1, inclusive.
   */
  old_start = win_start = 0;
  win_end = 0;
  last_window = FALSE;
  maxpow = 0.0;

  do {

    /* set up the window end */
    old_end = win_end;
    win_end = win_start + windowsz;
    if (win_end >= nsamples) {
      win_end = nsamples;
      last_window = TRUE;
    }

    /* read a windowsz sized chunk */
    filled_sz = fread(data_buf, bytes_per_sample,
		      windowsz * fmt->channels, in);
    for (c = 0; c < fmt->channels; c++) {
      sums[c] = 0;
      for (i = 0; i < (win_end - win_start); i++) {
	sample = get_sample(data_buf + (i * fmt->channels * bytes_per_sample)
			    + (c * bytes_per_sample), bytes_per_sample);
	sums[c] += sample * (double)sample;
	/* track peak */
	if (sample > psi->max_sample)
	  psi->max_sample = sample;
	if (sample < psi->min_sample)
	  psi->min_sample = sample;
      }
    }

    /* compute power for each channel */
    for (c = 0; c < fmt->channels; c++) {
      int end;
      pow = sums[c] / (double)(win_end - win_start);

      end = (powsmooth[c].start + powsmooth[c].n) % powsmooth[c].buflen;
      powsmooth[c].buf[end] = pow;
      if (powsmooth[c].n == powsmooth[c].buflen) {
	powsmooth[c].start = (powsmooth[c].start + 1) % powsmooth[c].buflen;
	pow = get_smoothed_data(&powsmooth[c]);
	if (pow > maxpow)
	  maxpow = pow;
      } else {
	powsmooth[c].n++;
      }
    }

    /* update progress meter */
    if (verbose >= VERBOSE_PROGRESS) {
      if (nsamples - windowsz == 0)
	progress = 0;
      else
	progress = (win_end - windowsz) / (float)(nsamples - windowsz);
      /*progress = win_end / ((nsamples - windowsz) / (float)windowsz);*/
      if (progress >= last_progress + 0.01) {
	/*printf(_("progress: %f       \n"), progress);*/
	progress_callback(prefix_buf, progress);
	last_progress += 0.01;
      }
    }

    /* slide the window ahead */
    old_start = win_start;
    win_start += windowsz;

  } while (!last_window);

  if (maxpow < EPSILON) {
    /*
     * Either this whole file has zero power, or was too short to ever
     * fill the smoothing buffer.  In the latter case, we need to just
     * get maxpow from whatever data we did collect.
     */
    for (c = 0; c < fmt->channels; c++) {
      pow = get_smoothed_data(&powsmooth[c]);
      if (pow > maxpow)
	maxpow = pow;
    }
  }

  for (c = 0; c < fmt->channels; c++)
    free(powsmooth[c].buf);
  free(powsmooth);
  free(data_buf);
  free(sums);
  riff_chunk_unref(chnk);
  riff_unref(riff);

  /* scale the pow value to be in the range 0.0 -- 1.0 */
  maxpow = maxpow / (samplemin * (double)samplemin);

  /* fill in the signal_info struct */
  psi->level = sqrt(maxpow);
  if (-psi->min_sample > psi->max_sample)
    psi->peak = psi->min_sample / (double)samplemin;
  else
    psi->peak = psi->max_sample / (double)samplemax;

  return maxpow;

  /* error handling stuff */
 error7:
  for (c = 0; c < fmt->channels; c++)
    free(powsmooth[c].buf);
  /*error6:*/
  free(powsmooth);
  /*error5:*/
  free(data_buf);
  /*error4:*/
  free(sums);
  /*error3:*/
  riff_chunk_unref(chnk);
 error2:
  riff_unref(riff);
 error1:
  return -1.0;
}


/*
 * Get the maximum power level of the data read from a stream
 * (and the peak sample, if ppeak is not NULL)
 */
double
signal_max_power_stream(FILE *in, char *filename, struct signal_info *psi)

{
  struct wavfmt *fmt;
  int bytes_per_sample;
  int last_window;
  unsigned int windowsz;
  unsigned int win_start, old_start, win_end, old_end;

  int i, c;
  long sample, samplemax, samplemin;
  double *sums;
  double pow, maxpow;
  datasmooth_t *powsmooth;

  char prefix_buf[18];

  unsigned char *data_buf = NULL;
  int filled_sz;

  if (filename == NULL || strcmp(filename, "-") == 0)
    filename = "STDIN";

  /* WAV format info must be passed to us in psi->fmt */
  fmt = &psi->fmt;

  windowsz = (unsigned int)(fmt->samples_per_sec / 100);

  bytes_per_sample = (fmt->bits_per_sample - 1) / 8 + 1;
  samplemax = (1 << (bytes_per_sample * 8 - 1)) - 1;
  samplemin = -samplemax - 1;
  /* initialize peaks to effectively -inf and +inf */
  psi->max_sample = samplemin;
  psi->min_sample = samplemax;

  sums = (double *)xmalloc(fmt->channels * sizeof(double));
  for (c = 0; c < fmt->channels; c++)
    sums[c] = 0;

  data_buf = (unsigned char *)xmalloc(windowsz
				      * fmt->channels * bytes_per_sample);

  /* set up smoothing window buffer */
  powsmooth = (datasmooth_t *)xmalloc(fmt->channels * sizeof(datasmooth_t));
  for (c = 0; c < fmt->channels; c++) {
    powsmooth[c].buflen = 100; /* use a 100-element (1 second) window */
    powsmooth[c].buf = (double *)xmalloc(powsmooth[c].buflen * sizeof(double));
    powsmooth[c].start = powsmooth[c].n = 0;
  }

  /* initialize progress meter */
  if (verbose >= VERBOSE_PROGRESS) {
    if (strrchr(filename, '/') != NULL) {
      filename = strrchr(filename, '/');
      filename++;
    }
    strncpy(prefix_buf, filename, 17);
    prefix_buf[17] = 0;
    progress_callback(prefix_buf, 0.0);
  }


  /*
   * win_start, win_end, old_start, windowsz, interval, and i are in
   * units of samples.  c is in units of channels.
   *
   * The actual window extends from win_start to win_end - 1, inclusive.
   */
  old_start = win_start = 0;
  win_end = 0;
  last_window = FALSE;
  maxpow = 0.0;

  do {

    /* set up the window end */
    old_end = win_end;
    win_end = win_start + windowsz;

    /* read a windowsz sized chunk */
    filled_sz = fread(data_buf, bytes_per_sample,
		      windowsz * fmt->channels, in);

    /* if we couldn't read a complete chunk, then this is the last chunk */
    if (filled_sz < windowsz * fmt->channels) {
      win_end = win_start + (filled_sz / fmt->channels);
      last_window = TRUE;
    }

    for (c = 0; c < fmt->channels; c++) {
      sums[c] = 0;
      for (i = 0; i < (win_end - win_start); i++) {
	sample = get_sample(data_buf + (i * fmt->channels * bytes_per_sample)
			    + (c * bytes_per_sample), bytes_per_sample);
	sums[c] += sample * (double)sample;
	/* track peak */
	if (sample > psi->max_sample)
	  psi->max_sample = sample;
	if (sample < psi->min_sample)
	  psi->min_sample = sample;
      }
    }

    /* compute power for each channel */
    for (c = 0; c < fmt->channels; c++) {
      int end;
      pow = sums[c] / (double)(win_end - win_start);

      end = (powsmooth[c].start + powsmooth[c].n) % powsmooth[c].buflen;
      powsmooth[c].buf[end] = pow;
      if (powsmooth[c].n == powsmooth[c].buflen) {
	powsmooth[c].start = (powsmooth[c].start + 1) % powsmooth[c].buflen;
	pow = get_smoothed_data(&powsmooth[c]);
	if (pow > maxpow)
	  maxpow = pow;
      } else {
	powsmooth[c].n++;
      }
    }

    /* slide the window ahead */
    old_start = win_start;
    win_start += windowsz;

  } while (!last_window);

  if (maxpow < EPSILON) {
    /*
     * Either this whole file has zero power, or was too short to ever
     * fill the smoothing buffer.  In the latter case, we need to just
     * get maxpow from whatever data we did collect.
     */
    for (c = 0; c < fmt->channels; c++) {
      pow = get_smoothed_data(&powsmooth[c]);
      if (pow > maxpow)
	maxpow = pow;
    }
  }

  for (c = 0; c < fmt->channels; c++)
    free(powsmooth[c].buf);
  free(powsmooth);
  free(data_buf);
  free(sums);

  /* scale the pow value to be in the range 0.0 -- 1.0 */
  maxpow = maxpow / (samplemin * (double)samplemin);

  /* fill in the signal_info struct */
  psi->level = sqrt(maxpow);
  if (-psi->min_sample > psi->max_sample)
    psi->peak = psi->min_sample / (double)samplemin;
  else
    psi->peak = psi->max_sample / (double)samplemax;

  return maxpow;
}


/*
 * input is read from read_fd and output is written to write_fd:
 * filename is used only for messages.
 *
 * The psi pointer gives the peaks so we know if compression is needed
 * or not.  It may be specified as NULL if this information is not
 * available.
 */
int
apply_gain(int read_fd, int write_fd, char *filename, double gain,
	   struct signal_info *psi)
{
  riff_t *riff;
  riff_chunk_t *chnk;
  struct wavfmt fmt;
  unsigned int nsamples, samples_done, nclippings;
  int bytes_per_sample, i;
  long sample, samplemax, samplemin;
  float clip_loss;
  FILE *rd_stream, *wr_stream;

  float last_progress = 0, progress;
  char prefix_buf[18];

  unsigned char *data_buf = NULL;
  int samples_in_buf, samples_recvd;
  int do_compression_this_file;
#if USE_LOOKUPTABLE
  int min_pos_clipped; /* the minimum positive sample that gets clipped */
  int max_neg_clipped; /* the maximum negative sample that gets clipped */
  int16_t *lut = NULL;
#endif

  riff = riff_new(read_fd, RIFF_RDONLY);
  if (riff == NULL) {
    fprintf(stderr, _("%s: error making riff object\n"), progname);
    goto error1;
  }

  chnk = get_wav_data(riff, &fmt);
  if (chnk == NULL) {
    fprintf(stderr, _("%s: error getting wav data\n"), progname);
    goto error2;
  }

  bytes_per_sample = (fmt.bits_per_sample - 1) / 8 + 1;
  samplemax = (1 << (bytes_per_sample * 8 - 1)) - 1;
  samplemin = -samplemax - 1;

  /* ignore different channels, apply gain to all samples */
  nsamples = chnk->size / bytes_per_sample;

  /* set up sample buffer to hold 1/100 of a second worth of samples */
  /* (make sure it can hold at least the wav header, though) */
  samples_in_buf = (fmt.samples_per_sec / 100) * fmt.channels;
  if (chnk->offset + 8 > samples_in_buf * bytes_per_sample)
    data_buf = (unsigned char *)xmalloc(chnk->offset + 8);
  else
    data_buf = (unsigned char *)xmalloc(samples_in_buf * bytes_per_sample);

  /* open streams for reading and writing */
  rd_stream = fdopen(read_fd, "rb");
  wr_stream = fdopen(write_fd, "wb");
  if (rd_stream == NULL || wr_stream == NULL) {
    fprintf(stderr, _("%s: failed fdopen: %s\n"), progname, strerror(errno));
    goto error4;
  }
  /* copy the wav header */
  rewind(rd_stream);
  rewind(wr_stream);
  if (fread(data_buf, chnk->offset + 8, 1, rd_stream) < 1) {
    fprintf(stderr, _("%s: read failed: %s\n"), progname, strerror(errno));
    goto error4;
  }
  if (fwrite(data_buf, chnk->offset + 8, 1, wr_stream) < 1) {
    fprintf(stderr, _("%s: write failed: %s\n"), progname, strerror(errno));
    goto error4;
  }

  /*
   * Check if we actually need to do compression on this file:
   * we don't if gain <= 1 or if the peaks wouldn't clip anyway.
   */
  do_compression_this_file = do_compression && gain > 1.0;
  if (do_compression_this_file && psi) {
    if (psi->max_sample * gain <= samplemax
	&& psi->min_sample * gain >= samplemin)
      do_compression_this_file = FALSE;
  }

#if USE_LOOKUPTABLE
  /*
   * If samples are 16 bits or less, build a lookup table for fast
   * adjustment.  This table is 128k, look out!
   */
  if (bytes_per_sample <= 2) {
    lut = (int16_t *)xmalloc((samplemax - samplemin + 1) * sizeof(int16_t));
    lut -= samplemin; /* so indices don't have to be offset */
    min_pos_clipped = samplemax + 1;
    max_neg_clipped = samplemin - 1;
    if (gain > 1.0) {
      if (do_compression_this_file) {
	/* apply gain, and do tanh compression to avoid clipping */
	/* Thanks to Ted Wright for this idea and fix */
	for (i = samplemin; i < 0; i++)
	  lut[i] = samplemin * tanh(i * gain / (double)samplemin);
	for (i = 0; i <= samplemax; i++)
	  lut[i] = samplemax * tanh(i * gain / (double)samplemax);
      } else {
	/* apply gain, and do clipping */
	for (i = samplemin; i <= samplemax; i++) {
	  sample = i * gain;
	  if (sample > samplemax) {
	    sample = samplemax;
	    if (i < min_pos_clipped)
	      min_pos_clipped = i;
	  } else if (sample < samplemin) {
	    sample = samplemin;
	    if (i > max_neg_clipped)
	      max_neg_clipped = i;
	  }
	  lut[i] = sample; /* negative indices are okay, see above */
	}
      }
    } else {
      /* just apply gain if it's less than 1 */
      for (i = samplemin; i <= samplemax; i++)
	lut[i] = i * gain;
    }
  }
#endif

  /* initialize progress meter */
  if (verbose >= VERBOSE_PROGRESS) {
    if (strrchr(filename, '/') != NULL) {
      filename = strrchr(filename, '/');
      filename++;
    }
    strncpy(prefix_buf, filename, 17);
    prefix_buf[17] = 0;
    progress_callback(prefix_buf, 0.0);
    last_progress = 0.0;
  }

  /* read, apply gain, and write, one chunk at time */
  nclippings = samples_done = 0;
  while ((samples_recvd = fread(data_buf, bytes_per_sample,
				samples_in_buf, rd_stream)) > 0) {

    for (i = 0; i < samples_recvd; i++) {
      sample = get_sample(data_buf + (i * bytes_per_sample), bytes_per_sample);

      if (lut) {

	/* use the lookup table if we built one */
	if (sample >= min_pos_clipped || sample <= max_neg_clipped)
	  nclippings++;
	sample = lut[sample];

      } else {

	/* apply the gain to the sample */
	sample *= gain;

	if (gain > 1.0) {
	  if (do_compression_this_file) {
	    /* do tanh compression instead of clipping */
	    sample = samplemax * tanh(sample / (double)samplemax);
	  } else {
	    /* perform clipping */
	    if (sample > samplemax) {
	      sample = samplemax;
	      nclippings++;
	    } else if (sample < samplemin) {
	      sample = samplemin;
	      nclippings++;
	    }
	  }
	}
      }

      put_sample(sample, data_buf + (i * bytes_per_sample), bytes_per_sample);
    }

    if (fwrite(data_buf, bytes_per_sample,
	       samples_recvd, wr_stream) == 0) {
      fprintf(stderr, _("%s: failed fwrite: %s\n"), progname, strerror(errno));
    }

    samples_done += samples_recvd;

    /* update progress meter */
    if (verbose >= VERBOSE_PROGRESS) {
      progress = samples_done / (float)nsamples;
      if (progress >= last_progress + 0.01) {
	progress_callback(prefix_buf, progress);
	last_progress += 0.01;
      }
    }
  }

  /* make sure progress meter is finished */
  if (verbose >= VERBOSE_PROGRESS)
    progress_callback(prefix_buf, 1.0);

  if (fflush(rd_stream) == -1) {
    fprintf(stderr, _("%s: failed fflush: %s\n"), progname, strerror(errno));
  }
  if (fflush(wr_stream) == -1) {
    fprintf(stderr, _("%s: failed fflush: %s\n"), progname, strerror(errno));
  }

  if (!do_compression_this_file) {
    clip_loss = (float)nclippings / (float)nsamples;

    if (verbose >= VERBOSE_INFO) {
      if (nclippings) {
	fprintf(stderr, "\n");
	fprintf(stderr, _("%s: %d clippings performed, %.4f%% loss\n"),
		progname, nclippings, clip_loss * 100);
      }
    } else if (verbose >= VERBOSE_PROGRESS) {
      if (clip_loss > CLIPPING_WARN_THRESH)
	fprintf(stderr,
        _("%s: Warning: lost %0.2f%% of data due to clipping              \n"),
		progname, clip_loss * 100);
    }
  }

  if (lut) {
    /* readjust the pointer to the beginning of the array */
    lut += samplemin;
    free(lut);
  }
  free(data_buf);
  riff_chunk_unref(chnk);
  riff_unref(riff);
  return 0;


  /* error handling stuff */
 error4:
  free(data_buf);
  /*error3:*/
  riff_chunk_unref(chnk);
 error2:
  riff_unref(riff);
 error1:
  return -1;
}

#if 0
/*
 * decibel conversion routines
 */
__inline__ double
amp_to_dBFS(double x)
{
  return 20 * log10(x);
}

__inline__ double
frac_to_dB(double x)
{
  return 20 * log10(x);
}

__inline__ double
dBFS_to_amp(double x)
{
  return pow(10, x / 20.0);
}

__inline__ double
dB_to_frac(double x)
{
  return pow(10, x / 20.0);
}
#endif

#if USE_TEMPFILE
/*
 * This works like the BSD mkstemp, except that we don't unlink the
 * file, since we end up renaming it to something else.
 */
int
xmkstemp(char *template)
{
  static char sfx[7] = "AAAAAA";
  char *p;
  int fd, i, done;

  p = template + strlen(template) - 6;
  if (strcmp(p, "XXXXXX") != 0) {
    errno = EINVAL;
    return -1;
  }

  do {
    strcpy(p, sfx);

    /* increment the suffix */
    done = 0; i = 5;
    while (!done && i >= 0) {
      sfx[i]++;
      if (sfx[i] > 'Z') {
	sfx[i] = 'A';
	i--;
      } else {
	done = 1;
      }
    }
    if (!done) {
      errno = EEXIST;
      return -1;
    }

    /* attempt to open the file */
    fd = open(template, O_RDWR | O_CREAT | O_EXCL, 0600);

  } while (fd == -1 && errno == EEXIST);

  return fd;
}


/*
 * Move the file "oldpath" to "newpath", or copy and delete if they
 * are on different filesystems.
 */
int
xrename(const char *oldpath, const char *newpath)
{
  if (rename(oldpath, newpath) == -1) {
    if (errno == EXDEV) {
      /* files are on different filesystems, so we have to copy */
      FILE *in, *out;
      char buf[4096];
      size_t sz;

      in = fopen(oldpath, "rb");
      if (in == NULL)
	return -1;
      out = fopen(newpath, "wb");
      if (out == NULL) {
	fclose(in);
	return -1;
      }

      while ((sz = fread(buf, 1, 4096, in)) > 0)
	fwrite(buf, 1, sz, out);

      if (ferror(in) || ferror(out)) {
	fclose(in);
	fclose(out);
	return -1;
      }
      if (fclose(in) == EOF) {
	fclose(out);
	return -1;
      }
      if (fclose(out) == EOF)
	return -1;
      if (unlink(oldpath) == -1)
	return -1;
    } else {
      return -1;
    }
  }

  return 0;
}
#endif

/*
 * Return nonzero if the two strings are equal, ignoring case, up to
 * the first n characters
 */
int
strncaseeq(const char *s1, const char *s2, size_t n)
{
  for ( ; n > 0; n--) {
    if (tolower(*s1++) != tolower(*s2++))
      return 0;
  }

  return 1;
}

void *
xmalloc(size_t size)
{
  void *ptr = malloc(size);
  if (ptr == NULL) {
    fprintf(stderr, _("%s: unable to malloc\n"), progname);
    exit(1);
  }
  return ptr;
}
