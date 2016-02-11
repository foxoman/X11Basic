/* defs.h (c) Markus Hoffmann  */

/* This file is part of X11BASIC, the basic interpreter for Unix/X
 * ============================================================
 * X11BASIC is free software and comes with NO WARRANTY - read the file
 * COPYING for details
 */

#ifndef __X11BASICDEFS
#define __X11BASICDEFS

#include "config.h"
#include "options.h"

#ifdef SMALL
  #define SAVE_RAM 1
#endif

#ifdef SAVE_RAM
  #define MAXSTRLEN   1024   /* in Bytes */
  #define MAXLINELEN  1024   /* in Bytes */
  #define ANZFILENR     64
  #define STACKSIZE    256
  #define ANZVARS     1024
  #define ANZLABELS    256
  #define ANZPROCS     512
#else
  #define MAXSTRLEN   4096   /* in Bytes */
  #define MAXLINELEN  4096   /* in Bytes */
  #define ANZFILENR    100
  #define STACKSIZE    512
  #define ANZVARS     4096
  #define ANZLABELS   1024
  #define ANZPROCS    1024
#endif
#define DEFAULTWINDOW 1

#define INTSIZE sizeof(double)

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#define min(a,b) ((a<b)?a:b)
#define max(a,b) ((a>b)?a:b)
#define sgn(x)   ((x<0)?-1:1)
#define rad(x)   (PI*x/180)
#define deg(x)   (180*x/PI)
#ifndef LOBYTE
  #define LOBYTE(x) ((unsigned char) ((x) & 0xff))
#endif
#ifndef HIBYTE
  #define HIBYTE(x) ((unsigned char) ((x) >> 8 & 0xff))
#endif
#ifdef WINDOWS
  #define bzero(p, l) memset(p, 0, l)
#endif
#endif