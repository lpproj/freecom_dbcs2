/* $Id$
 * VER.C
 *
 *  06/30/98 (Rob Lake)
 *      rewrote ver command to accept switches, now ver alone prints
 *      copyright notice only.
 *
 * 27-Jul-1998 (John P Price <linux-guru@gcfl.net>)
 * - added config.h include
 *
 * 30-Jul-1998 (John P Price <linux-guru@gcfl.net>)
 * - added text about where to send bug reports and get updates.
 *
 * 13-Sep-1998 (John P Price <linux-guru@gcfl.net>)
 * - change /r to print info. /d shows redistribution info now.
 *
 * 30-Jan-1999 (John P Price <linux-guru@gcfl.net>)
 * - changed /r to show FreeDOS kernel version
 *
 * 1-Apr-2000 0.79 Henry Voigt <Henry.Voigt@gmx.de>
 * bugfix: ver.c : options uninitilized
 *
 * 2000/06/22 ska
 *	new version: v0.79b
 *
 * 2000/12/10 ska
 *	new minor version: v0.80
 *
 * 2001/02/16 ska
 * bugfix: VER (without option) displays too much information
 * chg: using strings (except FreeCOM's own name)
 */

#include "../config.h"

#include <stdio.h>
#include <dos.h>
#include <string.h>
#include "suppl.h"

#include "../include/command.h"
#include "../include/cmdline.h"
#include "../err_fcts.h"
#include "../strings.h"

#if defined(NEC98)
# define MACHINE " (PC98)"
#elif defined(IBMPC)
# define MACHINE " (IBMPC)"
#else
# define MACHINE ""
#endif

#include "../version.h"
const char shellver[] = FREECOM_VERSION 
#if defined(DBCS)
	"_DBCS"
#endif
	" - "
#if defined(__BORLANDC__)
	"BORLANDC"
#elif defined(__TURBOC__)
	"TURBOC"
#elif defined(__WATCOMC__)
	"WATCOMC"
#elif defined(__GNUC__)
	"GNUC"
#endif
#ifdef FEATURE_XMS_SWAP
	" - XMS_Swap"
#endif
	MACHINE
;
static const char shelldate[] = __DATE__ " " __TIME__;
const char shellname[] = "FreeCom";

#if !defined(IBMPC)
void middle_version(void)
{
  char ssvi[16];
  const char *s = strchr(shellver, ' ');
  if (s && shellver[0] != ' ' && (s - shellver) < sizeof(ssvi))
  {
    memset(ssvi, 0, sizeof(ssvi));
    memcpy(ssvi, shellver, s - shellver);
    printf("\n%s %s"
          MACHINE
          " [" __DATE__ "]\n",
          shellname, ssvi);
  }
}
#endif

#if 0
void short_version(void)
{
  printf("\n%s version %s [%s]\n", shellname, shellver, shelldate);
}
#endif

#ifdef INCLUDE_CMD_VER

static int optR, optW, optD, optC;

optScanFct(opt_ver)
{
  (void)arg;
  switch(ch) {
  case 'R': return optScanBool(optR);
  case 'W': return optScanBool(optW);
  case 'D': return optScanBool(optD);
  case 'C': return optScanBool(optC);
  }
  optErr();
  return E_Useage;
}


/*
 *  display shell version info internal command.
 *
 *
 */
int cmd_ver (char * rest) {
  char **argv;
  int argc, opts, ec = E_None;

#if 0
  short_version();
#else
# if !defined(IBMPC)
  /* note for NEC98:
     Some version of MSD.exe (bundled with EPSON DOS 6.2) requires that
     the first line of ver (without option) must be less than 52 chars.
  */
  extern int is_parent_msd;
  if (is_parent_msd)
    middle_version();
  else
# endif
  printf("\n%s ver %s [%s]\n", shellname, shellver, shelldate);
#endif

	optR = optW = optD = optC = 0;

  if((argv = scanCmdline(rest, opt_ver, 0, &argc, &opts)) == 0)
    return 1;

  /* arguments are simply ignored */

  if(optR) {                         /* version information */
        unsigned int major, minor;
        IREGS regs;
        regs.r_ax = 0x3306;
        regs.r_bx = 0;
        intrpt(0x21, &regs);
        major = regs.r_bx & 255;
        minor = regs.r_bx >> 8;
        regs.r_ax = 0x3000;
        intrpt(0x21, &regs);
        if (major == 0) {
          major = regs.r_ax & 255;
          minor = regs.r_ax >> 8;
        }
        displayString(TEXT_MSG_VER_DOS_VERSION, major, minor);

        if ((regs.r_bx >> 8) == 0xfd && (regs.r_bx & 0xFF) == 0xff)
        {
          displayString(TEXT_MSG_VER_EARLY_FREEDOS);
        }
        else
        {
           unsigned char far * pstart;
           unsigned char far * pend;
/*         displayString(TEXT_MSG_VER_LATER_FREEDOS
           , regs.r_cx >> 8, regs.r_cx & 0xFF, regs.r_bx & 0xFF);
           , 2, 0, regs.r_bx & 0xFF ); */
           regs.r_ax = 0x33FF;
           regs.r_dx = 0;
           intrpt( 0x21, &regs );
           if (regs.r_dx) {
             pstart = MK_FP( regs.r_dx, regs.r_ax );
             for (pend = pstart; *pend; ++ pend) /* empty loop body */;
             while (pend > pstart &&
               (pend[-1] == ' ' || pend[-1] == 9
                 || pend[-1] == 10 || pend[-1] == 13)) {
               -- pend;
             }
             printf( "%.*Fs\n", pend - pstart, pstart);
           }
        }
      }
      if (optW)
      {                         /* Warranty notice */
        displayString(TEXT_MSG_VER_WARRANTY);
      }
      if (optD)
      {                         /* Redistribution notice */
        displayString(TEXT_MSG_VER_REDISTRIBUTION);
      }
      if (optC)
      {                         /* Developer listing */
        displayString(TEXT_MSG_VER_DEVELOPERS);
      }

  freep(argv);
  return ec;
}

#endif
