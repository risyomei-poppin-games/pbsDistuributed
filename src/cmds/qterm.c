/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/
/*
 * qterm
 *  The qterm command terminates the batch server.
 *
 * Synopsis:
 *  qterm -t type [server ...]
 *
 * Options:
 *  -t  delay   Jobs are (1) checkpointed if possible; otherwise, (2) jobs are
 *  rerun (requeued) if possible; otherwise, (3) jobs are left to
 *              run.
 *
 *      immediate
 *              Jobs are (1) checkpointed if possible; otherwise, (2) jobs are
 *  rerun if possible; otherwise, (3) jobs are aborted.
 *
 * quick
 *  The server will save state and exit leaving running jobs
 *  still running.  Good for shutting down when you wish to
 *  quickly restart the server.
 *
 * Arguments:
 *  server ...
 *      A list of servers to terminate.
 *
 * Written by:
 *  Bruce Kelly
 *  National Energy Research Supercomputer Center
 *  Livermore, CA
 *  May, 1993
 */

#include "cmds.h"
#include <pbs_config.h>   /* the master config generated by configure */

int exitstatus = 0; /* Exit Status */

static void execute(int, char *);

int main(

  int    argc,  /* I */
  char **argv)  /* I */

  {
  /*
   * This routine sends a Server Shutdown request to the batch server.  If the
   * batch request is accepted, and the type is IMMEDIATE, then no more jobs
   * are accepted and all jobs are checkpointed or killed.  If the type is
   * DELAY, then only privileged users can submit jobs, and jobs will be
   * checkpointed if available.
   */

  static char opts[] = "t:";  /* See man getopt */
  int s;                  /* The execute line option */
  static char usage[] = "Usage: qterm [-t immediate|delay|quick] [server ...]\n";
  char *type = NULL;      /* Pointer to the type of termination */
  int manner;             /* The type of termination */
  int errflg = 0;         /* Error flag */

  /* Command line options */

  while ((s = getopt(argc, argv, opts)) != EOF)
    {
    switch (s)
      {

      case 't':

        type = optarg;

        break;

      case '?':

      default:

        errflg++;

        break;
      }
    }  /* END while() */

  if (errflg)
    {
      fprintf(stderr, "%s",
            usage);

    exit(1);
    }

  if (type == NULL)
    {
    /* use 'quick' as default */

    manner = SHUT_QUICK;
    }
  else
    {
    if (!strcasecmp(type, "delay"))
      {
      manner = SHUT_DELAY;
      }
    else if (!strcasecmp(type, "immediate"))
      {
      manner = SHUT_IMMEDIATE;
      }
    else if (!strcasecmp(type, "quick"))
      {
      manner = SHUT_QUICK;
      }
    else
      {
      /* invalid type specified */

	fprintf(stderr, "%s",
              usage);

      exit(1);
      }
    }

  if (optind < argc)
    {
    /* shutdown each specified server */

    for (;optind < argc;optind++)
      {
      execute(manner, argv[optind]);
      }
    }
  else
    {
    /* shutdown default server */

    execute(manner, "");
    }

  exit(exitstatus);

  /*NOTREACHED*/

  return(0);
  }  /* END main() */





/*
 * void execute(int manner,char *server)
 *
 * manner   The manner in which to terminate the server.
 * server   The name of the server to terminate.
 *
 * Returns:
 *  None
 *
 * File Variables:
 *  exitstatus  Set to two if an error occurs.
 */

static void execute(

  int   manner,  /* I */
  char *server)  /* I */

  {
  int ct;         /* Connection to the server */
  int err;        /* Error return from pbs_terminate */
  char *errmsg;   /* Error message from pbs_terminate */

  if ((ct = cnt2server(server)) > 0)
    {
    err = pbs_terminate(ct, manner, NULL);

    if (err != 0)
      {
      errmsg = pbs_geterrmsg(ct);

      if (errmsg != NULL)
        {
        fprintf(stderr, "qterm: %s",
                errmsg);
        }
      else
        {
        fprintf(stderr, "qterm: Error (%d - %s) terminating server ",
                pbs_errno, pbs_strerror(pbs_errno));
        }

      fprintf(stderr, "%s\n",

              server);

      exitstatus = 2;
      }

    pbs_disconnect(ct);
    }
  else
    {
    /* FAILURE */

    fprintf(stderr, "qterm: could not connect to server '%s' (%d) %s\n",
            server,
            pbs_errno,
            pbs_strerror(pbs_errno));

    exitstatus = 2;
    }

  return;
  }  /* END execute() */


