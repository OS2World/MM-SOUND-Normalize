/* Special definitions, to process by autoheader.
   Copyright (C) 1995, 1996 Free Software Foundation.

   Mainly stolen from Ulrich Drepper, <drepper@gnu.ai.mit.edu>
   gettext-0.10, 1995.  */

@TOP@

/* Define to the name of the distribution.  */
#undef PACKAGE

/* Define to the version of the distribution.  */
#undef VERSION

/* Define if your locale.h file contains LC_MESSAGES.  */
#undef HAVE_LC_MESSAGES

/* Define to 1 if NLS is requested.  */
#undef ENABLE_NLS

/* Define as 1 if you have catgets and don't want to use GNU gettext.  */
#undef HAVE_CATGETS

/* Define as 1 if you have gettext and don't want to use GNU gettext.  */
#undef HAVE_GETTEXT

/* Define as 1 if you have the stpcpy function.  */
#undef HAVE_STPCPY

/* These defines are needed below.  */
#undef TYPE8
#undef TYPE16
#undef TYPE32

/* Define these if not defined already.  */
#undef int8_t
#undef int16_t
#undef int32_t
#undef uint8_t
#undef uint16_t
#undef uint32_t

@BOTTOM@

#define _(String) gettext (String)
#define N_(String) (String)
#include <libintl.h>

/* end of file acconfig.h */
