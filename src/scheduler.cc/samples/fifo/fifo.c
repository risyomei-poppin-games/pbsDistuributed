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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "pbs_error.h"
#include "pbs_ifl.h"
#include "sched_cmds.h"
#include <time.h>
#include "log.h"
#include <string.h>
#include "queue_info.h"
#include "server_info.h"
#include "check.h"
#include "constant.h"
#include "job_info.h"
#include "node_info.h"
#include "misc.h"
#include "fifo.h"
#include "config.h"
#include "sort.h"
#include "parse.h"
#include "globals.h"
#include "prev_job_info.h"
#include "fairshare.h"
#include "prime.h"
#include "dedtime.h"
#include "token_acct.h"
#include "gfarm/gfarm.h"
//#include "gfm_client.h"

extern float rgAlphaValue;
int alphaParam; 
/* a list of running jobs from the last scheduling cycle */
static prev_job_info *last_running = NULL;
static int last_running_size = 0;
static time_t last_decay;
static time_t last_sync;


#define HASH_TABLE_LENGTH 300 


/*
 *
 * schedinit - initialize conf struct and parse conf files
 *
 *   argc - passed in from main
 *   argv - passed in from main
 *
 * Returns Success/Failure
 *
 *
 */
int schedinit(int argc, char *argv[])
  {
char logbuf[MAX_LOG_SIZE]; 
  init_config();

	alphaParam = (int)rgAlphaValue;
	sprintf(logbuf,"alphaParam %f", rgAlphaValue);
	sched_log(	PBSEVENT_SCHED, 
			PBS_EVENTCLASS_NODE, 
			"alphaParam", 
			logbuf);

  parse_config(CONFIG_FILE);
  parse_holidays(HOLIDAYS_FILE);

  time(&(cstat.current_time));

  if (is_prime_time())
    init_prime_time();
  else
    init_non_prime_time();

  parse_ded_file(DEDTIME_FILE);

  /* preload the static members to the fairshare tree */
  preload_tree();

  parse_group(RESGROUP_FILE);

  calc_fair_share_perc(conf.group_root -> child, UNSPECIFIED);

  if (conf.prime_fs || conf.non_prime_fs)
    {
    read_usage();

    /*
     * initialize the last_decay to the current time, since we do not know when
     * the last decay happened
     */
    last_sync = last_decay = cstat.current_time;
    }

  token_acct_open((char *)0);

  return 0;
  }

/*
 *
 * update_cycle_status - update global status structure which holds
 *         status information used by the scheduler which
 *         can change from cycle to cycle
 *
 * returns nothing
 *
 */
void
update_cycle_status(void)
  {
  char dedtime;    /* boolean: is it dedtime? */
  enum prime_time prime;  /* current prime time status */

  time(&(cstat.current_time));
  dedtime = is_ded_time();

  /* it was dedtime last scheduling cycle, and it is not dedtime now */

  if (cstat.is_ded_time && !dedtime)
    {
    /* since the current dedtime is now passed, set it to zero and sort it to
     * the end of the dedtime array so the next dedtime is at the front
     */
    conf.ded_time[0].from = 0;
    conf.ded_time[0].to = 0;
    qsort(conf.ded_time, MAX_DEDTIME_SIZE, sizeof(struct timegap), cmp_ded_time);
    }

  cstat.is_ded_time = dedtime;


  /* if we have changed from prime to nonprime or nonprime to prime
   * init the status respectivly
   */
  prime = is_prime_time();

  if (prime == PRIME && !cstat.is_prime)
    init_prime_time();
  else if (prime == NON_PRIME && cstat.is_prime)
    init_non_prime_time();
  }

/*
 *
 * init_scheduling_cycle - run things that need to be set up every
 *    scheduling cycle
 *
 * returns 1 on success 0 on failure
 *
 * NOTE: failure of this function will cause schedule() to return
 */

int init_scheduling_cycle(server_info *sinfo)
  {
  group_info *user; /* the user for the running jobs of the last cycle */
  queue_info *qinfo; /* user to cycle through the queues to sort the jobs */
  char decayed = 0; /* boolean: have we decayed usage? */
  time_t t;  /* used in decaying fair share */
  int i, j;

  if (cstat.fair_share)
    {
    if (last_running != NULL)
      {
      /* add the usage which was accumulated between the last cycle and this
       * one and calculate a new value
       */

      for (i = 0; i < last_running_size ; i++)
        {
        job_info** jobs;
        user = last_running[i].ginfo;
#if HIGH_PRECISION_FAIRSHARE
        jobs = sinfo -> jobs; /* check all jobs (exiting, completed, running) */
#else
        jobs = sinfo -> running_jobs; /* check only running */
#endif

        for (j = 0; jobs[j] != NULL; j++)
          {
            if (jobs[j] -> is_completed || jobs[j] -> is_exiting ||
              jobs[j] -> is_running)
              if (!strcmp(last_running[i].name, jobs[j] -> name))
                break;
          }

        if (jobs[j] != NULL)
          {
          user -> usage +=
            calculate_usage_value(jobs[j] -> resused) -
            calculate_usage_value(last_running[i].resused);
          }
        }

      /* assign usage into temp usage since temp usage is used for usage
       * calculations.  Temp usage starts at usage and can be modified later.
       */
      for (i = 0; i < last_running_size; i++)
        last_running[i].ginfo -> temp_usage = last_running[i].ginfo -> usage;
      }

    /* The half life for the fair share tree might have passed since the last
     * scheduling cycle.  For that matter, several half lives could have
     * passed.  If this is the case, perform as many decays as necessary
     */

    t = cstat.current_time;

    while (t - last_decay > conf.half_life)
      {
      sched_log(PBSEVENT_DEBUG2, PBS_EVENTCLASS_SERVER, "", "Decaying Fairshare Tree");
      decay_fairshare_tree(conf.group_root);
      t -= conf.half_life;
      decayed = 1;
      }

    if (decayed)
      {
      /* set the time to the acuall the half-life should have occured */
      last_decay = cstat.current_time -
                   (cstat.current_time - last_decay) % conf.half_life;
      }

    if (cstat.current_time - last_sync > conf.sync_time)
      {
      write_usage();
      last_sync = cstat.current_time;
      sched_log(PBSEVENT_DEBUG2, PBS_EVENTCLASS_SERVER, "", "Usage Sync");
      }
    }

  if (cstat.help_starving_jobs)
    cstat.starving_job = update_starvation(sinfo -> jobs);

  /* sort queues by priority if requested */

  if (cstat.sort_queues)
    qsort(sinfo -> queues, sinfo -> num_queues, sizeof(queue_info *),
          cmp_queue_prio_dsc);


  if (cstat.sort_by[0].sort != NO_SORT)
    {
    if (cstat.by_queue || cstat.round_robin)
      {
      for (i = 0; i < sinfo -> num_queues; i++)
        {
        qinfo = sinfo -> queues[i];
        qsort(qinfo -> jobs, qinfo -> sc.total, sizeof(job_info *), cmp_sort);
        }
      }
    else
      qsort(sinfo -> jobs, sinfo -> sc.total, sizeof(job_info *), cmp_sort);
    }

  next_job(sinfo, INITIALIZE);

  return 1;  /* SUCCESS */
  }





/*
 *
 * schedule - this function gets called to start each scheduling cycle
 *     It will handle the difference cases that caused a
 *     scheduling cycle
 *
 *   cmd - reason for scheduling cycle
 *   SCH_ERROR  error
 *  SCH_SCHEDULE_NULL NULL command
 *  SCH_SCHEDULE_NEW A new job was submited
 *  SCH_SCHEDULE_TERM A job terminated
 *  SCH_SCHEDULE_TIME The scheduling interval expired
 *  SCH_SCHEDULE_RECYC A scheduling recycle(see admin guide)
 *  SCH_SCHEDULE_CMD The server scheduling variabe was set
 *     or reset to true
 *  SCH_SCHEDULE_FIRST the first cycle after the server starts
 *  SCH_CONFIGURE  perform scheduling configuration
 *  SCH_QUIT  have the scheduler quit
 *  SCH_RULESET  reread the scheduler ruleset
 *
 *   sd - connection desctiptor to the pbs server
 *
 *
 * returns 1: continue calling scheduling cycles, 0: exit scheduler
 */

int schedule(



  int cmd,
  int sd)

  
  {


	//    sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_SERVER, "WTF", "fifo.schedule is called"); 
  switch (cmd)
    {

    case SCH_ERROR:

    case SCH_SCHEDULE_NULL:

    case SCH_RULESET:

    case SCH_SCHEDULE_RECYC:

      /* ignore and end cycle */

      break;

    case SCH_SCHEDULE_NEW:
		//replicaGen(sd);

    case SCH_SCHEDULE_TERM:

    case SCH_SCHEDULE_FIRST:

    case SCH_SCHEDULE_CMD:

    case SCH_SCHEDULE_TIME:
	  replicaGen(sd);
      return(scheduling_cycle(sd));

      /*NOTREACHED*/

      break;

    case SCH_CONFIGURE:

      if (conf.prime_fs || conf.non_prime_fs)
        write_usage();

      reinit_config();

      parse_config(CONFIG_FILE);

      parse_holidays(HOLIDAYS_FILE);

      parse_ded_file(DEDTIME_FILE);

      preload_tree();

      parse_group(RESGROUP_FILE);

      calc_fair_share_perc(conf.group_root -> child, UNSPECIFIED);

      if (conf.prime_fs || conf.non_prime_fs)
        read_usage();

      break;

    case SCH_QUIT:
      if (conf.prime_fs || conf.non_prime_fs)
        write_usage();

      return 1;  /* have the scheduler exit nicely */

    default:
      return 0;
    }

  return(0);
  }  /* END schedule() */


struct gfarm_host_sched_info {
	char *host;
	gfarm_uint32_t port;
	gfarm_uint32_t ncpu;    /* XXX should have whole gfarm_host_info? */

	/* if GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL */
	float loadavg;
	gfarm_uint64_t cache_time;
	gfarm_uint64_t disk_used;
	gfarm_uint64_t disk_avail;

	/* if GFM_PROTO_SCHED_FLAG_RTT_AVAIL */
	gfarm_uint64_t rtt_cache_time;
	gfarm_uint32_t rtt_usec;

	gfarm_uint32_t flags;           /* GFM_PROTO_SCHED_FLAG_* */
};

unsigned int getHashCode(char* val)
{
	int offset = 0;

	int off = offset;
	int len = strlen(val);

	unsigned int h = 0;
	int i;
	for (i = 0; i < len; i++) {
		h = 31*h + val[off++];
	}

	return h%HASH_TABLE_LENGTH;
}





/*
 *
 *
 *
 *
 *
 * */
#define MAXTRACKSIZE 60
int replicaGen(int sd)
{
	server_info *sinfo;  /* ptr to the server/queue/job/node info */
	job_info *jinfo;  /* ptr to the job to see if it can run */
	int ret = SUCCESS;  /* return code from is_ok_to_run_job() */
	char logbuf[MAX_LOG_SIZE]; /* used to log an message about job */
//	char comment[MAX_COMMENT_SIZE]; /* used to update comment of job */

	sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_REQUEST, "", "in replicaGen");

//	update_cycle_status();
//
	unsigned int fileHash[HASH_TABLE_LENGTH] = {0};
	char *fileHashName[HASH_TABLE_LENGTH]={0};

	/* create the server / queue / job / node structures */

	if ((sinfo = query_server(sd)) == NULL)
	{
		fprintf(stderr, "Problem with creating server data strucutre\n");

		return(0);
	}

	if (init_scheduling_cycle(sinfo) == 0)
	{
		sched_log(
				PBSEVENT_SCHED,
				PBS_EVENTCLASS_SERVER,
				sinfo -> name,
				"init_scheduling_cycle failed.");

		free_server(sinfo, 1);

		return(0);
	}
	
	if(  gfarm_initialize(NULL,NULL) !=GFARM_ERR_NO_ERROR )
	{
		sched_log(	PBSEVENT_SCHED, 
					PBS_EVENTCLASS_NODE, 
					"getReplicaInformation", 
					"gfarm Initalize failed, used default value for data-aware value");
		goto cleanUp;
	}

	
	int count = 0;
	while ((jinfo = next_job(sinfo, 0)))
	{
		if(count++>MAXTRACKSIZE)
			break;

		if( jinfo->fileusedChecked == 1 ) //checked
		{	
		}
		else if(jinfo->name!=NULL && jinfo->fileused!=NULL)//not checked
		{
			
			jinfo -> fileusedChecked = 1;

			int hashVar = getHashCode(jinfo->fileused);	
			fileHash[ hashVar] ++;

			if(fileHashName[hashVar]==NULL)
				fileHashName[hashVar]=strdup(jinfo->fileused);
		
		}
		else
		{

		}
	}


	int currentMax=0;
	int index = -1;
	int i;
	for(i=0;i<HASH_TABLE_LENGTH;i++)
	{
		if(fileHash[i]>currentMax)
		{
			currentMax = fileHash[i];
			index = i;
		}
	}
	int replicaCount =0;
	struct gfs_replica_info* ri=NULL;
	if( index >= 0 && fileHashName[index]!=NULL)
	{

		if(gfs_replica_info_by_name(fileHashName[index], 0, &ri)==GFARM_ERR_NO_ERROR)
		{
		
			replicaCount = gfs_replica_info_number(ri);
		}	
	}
	
	//static int threadPID;

	if(index<0)
		index = 0;
	sprintf(logbuf,"replicaCount %d, fileUsed %d",replicaCount, fileHash[index]);


	sched_log(	PBSEVENT_SCHED, 
			PBS_EVENTCLASS_NODE, 
			"decision", 
			logbuf);



cleanUp:
	gfarm_terminate();
	free_server(sinfo, 1); /* free server and queues and jobs */
//	sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_REQUEST, "", "Leaving making replica\n");





	//ReplicaGeneration
	static int threadCount = 0;
	if(	threadCount <= 0 &&
 		replicaCount >0 &&
		replicaCount*alphaParam < fileHash[index])
	{		
		int numberToBeCreated = fileHash[index]/(replicaCount*alphaParam);
		char* fileToBeCreated = fileHashName[index];
		int k;
 		
		
		gfarm_initialize(NULL,NULL); 
		int hostNumber;
		struct gfarm_host_sched_info *infosp;
		gfarm_error_t e;
		struct gfm_connection *gfm_server;
		if ((e = gfm_client_connection_and_process_acquire_by_path(fileToBeCreated, &gfm_server)) != GFARM_ERR_NO_ERROR)
		{
			goto cleanUp;
		}
		e = gfm_client_schedule_host_domain(gfm_server, "", &hostNumber, &infosp);
		gfm_client_connection_free(gfm_server);
		gfarm_terminate();





		int count = hostNumber>numberToBeCreated?numberToBeCreated:hostNumber;	
		int i;
		threadCount+=count;
		for(i=0;i<count;i++)
		{
			pid_t pid = 0;
			pid = fork();
			if(pid == 0)//Sub thread
			{
				//Replicating

				if(  gfarm_initialize(NULL,NULL) ==GFARM_ERR_NO_ERROR )
				{
					gfs_replicate_to(fileToBeCreated, infosp[i].host, infosp[i].port);
				}	
				
				//thread Ended
				sched_log(PBSEVENT_SCHED,PBS_EVENTCLASS_REQUEST, "thread", "new thread end1");
				gfarm_terminate();
				sched_log(PBSEVENT_SCHED,PBS_EVENTCLASS_REQUEST, "thread", "new thread end2");
				_exit((int)e);
			}
		}


		int status;
		pid_t wait_pid; 

		do
		{
			wait_pid = 	waitpid(-1,&status,WNOHANG);
			if(wait_pid>0)
			{
				threadCount--;
			}
			else if(wait_pid==-1)
			{
				threadCount=0;
			}
		}while(wait_pid!=-1);
	}

	return 0;
}




/*
 *
 * scheduling_cycle - the controling function of the scheduling cycle
 *
 *   sd  - connection descriptor to the pbs server
 *
 * returns success/failure
 *
 */

int scheduling_cycle(

		int sd)

{
	//
	server_info *sinfo;  /* ptr to the server/queue/job/node info */
	job_info *jinfo;  /* ptr to the job to see if it can run */
	int ret = SUCCESS;  /* return code from is_ok_to_run_job() */
	char log_msg[MAX_LOG_SIZE]; /* used to log an message about job */
	char comment[MAX_COMMENT_SIZE]; /* used to update comment of job */

	sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_REQUEST, "", "Entering Schedule");

	update_cycle_status();

	/* create the server / queue / job / node structures */

	if ((sinfo = query_server(sd)) == NULL)
	{
		fprintf(stderr, "Problem with creating server data strucutre\n");

		return(0);
	}

	if (init_scheduling_cycle(sinfo) == 0)
	{
		sched_log(
				PBSEVENT_DEBUG,
				PBS_EVENTCLASS_SERVER,
				sinfo -> name,
				"init_scheduling_cycle failed.");

		free_server(sinfo, 1);

		return(0);
	}

	/* main scheduling loop */





	while ((jinfo = next_job(sinfo, 0)))
	{	
		
		sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_REQUEST, "debug1", jinfo->name);


		sched_log(
				PBSEVENT_DEBUG2,
				PBS_EVENTCLASS_JOB,
				jinfo->name,
				"Considering job to run");

		if ((ret = is_ok_to_run_job(sd, sinfo, jinfo->queue, jinfo)) == SUCCESS)
		{
			//int freeNodesLeft;
			run_update_job(sd, sinfo, jinfo->queue, jinfo);

		}
		else
		{
			if (jinfo->can_never_run)
			{
				sched_log(
						PBSEVENT_JOB,
						PBS_EVENTCLASS_JOB,
						jinfo->name,
						"Job Deleted because it would never run");

				pbs_deljob(sd, jinfo->name, "Job could never run");
			}

			jinfo->can_not_run = 1;

			if (translate_job_fail_code(ret, comment, log_msg))
			{
				/* if the comment doesn't get changed, its because it hasn't changed.
				 * if the reason for the job has not changed, we do not need to log it
				 */

				if (update_job_comment(sd, jinfo, comment) == 0)
				{
					sched_log(
							PBSEVENT_SCHED,
							PBS_EVENTCLASS_JOB,
							jinfo->name,
							log_msg);
				}
			}

			if ((ret != NOT_QUEUED) && cstat.strict_fifo)
			{
				update_jobs_cant_run(
						sd,
						jinfo->queue->jobs,
						jinfo,
						COMMENT_STRICT_FIFO,
						START_AFTER_JOB);
			}
		}
	}

	if (cstat.fair_share)
		update_last_running(sinfo);

cleanUpInSchedule:

	free_server(sinfo, 1); /* free server and queues and jobs */

	sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_REQUEST, "", "Leaving schedule\n");

	return 0;
}




/*
 *
 * update_last_running - update the last_running job array
 *         keep the currently running jobs till next
 *         scheduling cycle
 *
 *   sinfo - the server of current jobs
 *
 * Returns success/failure
 *
 */
int update_last_running(server_info *sinfo)
  {
  free_pjobs(last_running, last_running_size);

  last_running = create_prev_job_info(sinfo -> running_jobs,
                                      sinfo -> sc.running);
  last_running_size = sinfo -> sc.running;

  if (last_running == NULL)
    return 0;

  return 1;
  }

/*
 *
 * update_starvation - update starvation information in array of jobs
 *
 *   jobs - job array to update
 *
 * returns the most starving job
 *
 */
job_info *update_starvation(job_info **jobs)
  {
  job_info *jinfo = NULL; /* used as a ptr to the job to update */
  int i;

  if (jobs != NULL)
    {
    for (i = 0; jobs[i] != NULL; i++)
      {
      if (jobs[i] -> qtime + conf.max_starve < cstat.current_time &&
          jobs[i] -> is_queued && !jobs[i] -> queue -> dedtime_queue)
        {
        jobs[i] -> is_starving = 1;
        jobs[i] -> sch_priority =
          cstat.current_time - jobs[i] -> qtime - conf.max_starve;

        if (jinfo == NULL || jobs[i] -> sch_priority > jinfo -> sch_priority)
          jinfo = jobs[i];
        }

      /* job not starving yet
      else
        ;
      */
      }
    }

  return jinfo;
  }







gfarm_error_t getRepInfo(
		char *fileUsed,
		int *replicaCount,
		int *fileSize,
		char ***nodes_filelocated)
{

	int noErr = 1;
	gfarm_error_t  e;
	char logbuf[256];   /* buffer for log messages */

	sched_log(	PBSEVENT_SCHED, 
			 	PBS_EVENTCLASS_NODE,
				"getReplicaInformation",
				fileUsed);

	//Get prepared for getting the Information about the file used;
	if( (e=  gfarm_initialize(NULL,NULL) )!=GFARM_ERR_NO_ERROR )
	{
		sched_log(	PBSEVENT_SCHED, 
					PBS_EVENTCLASS_NODE, 
					"getReplicaInformation", 
					"gfarm Initalize failed, used default value for data-aware value");
		
		*replicaCount=0;
		*nodes_filelocated=NULL;
		*fileSize=0;
		return e;	
	}


	struct gfs_replica_info* ri=NULL;
	if(	(e=gfs_replica_info_by_name(fileUsed, 0, &ri))!=GFARM_ERR_NO_ERROR)
	{
		sprintf(logbuf, "get_replic_info failed:error = %d",e );
		sched_log(PBSEVENT_SCHED, 
				PBS_EVENTCLASS_NODE,
				fileUsed,
				logbuf);
		*replicaCount=0;
		*nodes_filelocated=NULL;
		*fileSize=0;
		return e;
	}

	*replicaCount = gfs_replica_info_number(ri);
	sprintf(logbuf, "ReplicaCount %d",*replicaCount );
	sched_log(PBSEVENT_SCHED, 
			PBS_EVENTCLASS_NODE, 
			fileUsed,
			logbuf);

	if(*replicaCount<=0)
	{
		*replicaCount=0;
		*nodes_filelocated = NULL; 
		*fileSize=0;
		return -1;
	}


	*nodes_filelocated = malloc(sizeof(char*)*(*replicaCount));
	int z;
	for(z=0;z<*replicaCount;z++)
	{

		const char *tempHostName =gfs_replica_info_nth_host(ri, z);

		

		

		int x;
		for(x=0;x<strlen(tempHostName);x++)
			if(tempHostName[x]=='.')
				break;

		char job_name_prefix[LOG_BUF_SIZE];
		strncpy(job_name_prefix,tempHostName,x);

		(*nodes_filelocated)[z]=strdup(job_name_prefix);
		sprintf(logbuf, " %s",(*nodes_filelocated)[z]); 
	}



	struct gfs_stat st;

	e=gfs_lstat(fileUsed, &st);
	if(e!=GFARM_ERR_NO_ERROR)	
	{

		sprintf(logbuf, "gfs_lstat  failed:error = %d",e );
		sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_NODE, fileUsed,logbuf);\
		*replicaCount=0;
		free(*nodes_filelocated);
		*nodes_filelocated = NULL; 
		*fileSize=0;
		return e;

	}

	sprintf(logbuf, "user %s, group %s, size %ld",st.st_user,st.st_group,st.st_size);
	*fileSize = st.st_size;
	sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_NODE,"wtf" ,logbuf );



	//Gfarm termination
	gfarm_terminate();
	return GFARM_ERR_NO_ERROR;

}
void relRepInfo(int *replicaCount,char ***nodes_filelocated)
{
	char logbuf[256];   /* buffer for log messages */

//	sprintf(logbuf,"Replica Count %d",*replicaCount);
//	sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_NODE, "relRepInfo", logbuf);
	
	if(*replicaCount==0||*nodes_filelocated==NULL)
		return;

	int lp = 0 ;
	for(lp=0;lp<*replicaCount;lp++)
	{
		
		free((*nodes_filelocated)[lp]);
//		sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_NODE, "in", "memory  freed");
	}

	free(*nodes_filelocated);	
//	sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_NODE, "out", "memory freed");

}

node_info* dataAwareDispatch(job_info *jinfo)
{

//	sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_NODE, "NONE", "d-a called");
	node_info *possible_node = NULL; /* node which under max node not ideal*/
	node_info *good_node = NULL;  /* node which is under ideal load */

	int i = 0, c;   /* index & loop vars */
	int nodes_number;    /* index of the last node in ninfo_arr*/
//	float good_node_la = 1.0e10;  /* the best load ave found yet */
	float proj_la;   /* the projected load average */
	char logbuf[256];   /* buffer for log messages */

	node_info **ninfo_arr = jinfo -> queue -> server -> nodes;
	nodes_number = jinfo -> queue -> server -> num_nodes;
	
	
	char  **nodes_filelocated=NULL;
	int replicaCount = 0;
	int fileSize=0;
	int dataAwareSwitch=0;

	//No information? fuck yourself
	if (jinfo == NULL||ninfo_arr == NULL)
		return NULL;

	//Initialize loop valiable and the maxhostvalue


	if(jinfo->fileused)
	{
		gfarm_error_t e= getRepInfo(jinfo->fileused,&replicaCount,&fileSize,&nodes_filelocated);
		if(replicaCount>0)
			dataAwareSwitch = 1;
	}


	float currentScore = 2;	
	unsigned lastFree = 0;	
	float scoreThreshould=0.7;
	float BETA = 0.9;
	

	
	for (i = 0,c=0; c< nodes_number; c++)//loop for the host
	{

			long fileMatch = 0;
			long fileMismatch = 0;
			float fileFactor = 0;

			if(dataAwareSwitch==1)
			{
				int lp2=0;
				for(lp2=0;lp2<replicaCount;lp2++)
				{
					if(!strcmp(ninfo_arr[i]->name,nodes_filelocated[lp2]))
					{
						fileMatch += fileSize;
					}
				
				}
				if(fileMatch==0)
				{
					fileMismatch +=fileSize;
				}

			}
			fileFactor = ( ( -fileMatch + fileMismatch ) / (float)fileSize  + 1 ) / 2;

			float CPUloadAve = ( ninfo_arr[i] -> loadave) / 16.0 ;
			float score = fileFactor * BETA + CPUloadAve*(1-BETA);
			//proj_la = ninfo_arr[i] -> loadave;

			sprintf(logbuf, "CPU: %6.2f score: %6.2f last: %6.2f Score: %u,%u",
							CPUloadAve, score, currentScore,lastFree,ninfo_arr[i]->is_free);
			sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_NODE, ninfo_arr[i]->name,logbuf);
			if( /*proj_la  <= ninfo_arr[i]->ideal_load&&*/ score < scoreThreshould)
			{



				if(lastFree==0&&ninfo_arr[i]->is_free)
				{
					good_node = ninfo_arr[i];
					currentScore = score;
					lastFree=1;
					sprintf(logbuf,"not free to free:useThis" );
				}
				else if(lastFree==1&&!ninfo_arr[i]->is_free)
				{

					sprintf(logbuf,"free to not free: skipped" );
					goto endofloop;	
				}

				else if( score < currentScore  )
				{
			//		sprintf(logbuf, "lowload load: %6.2f Score: %6.2f last Score``: %6.2f",
				//			proj_la, score,currentScore);

					sprintf(logbuf,"score < currentScore: useThis" );
					good_node = ninfo_arr[i];
					currentScore = score;
					lastFree = ninfo_arr[i]->is_free;
				}
				else
				{

					sprintf(logbuf,"score >= currentScore: skipped" );
		//			sprintf(logbuf, "lowload Rej, higher: Score: %6.2f last Score: %6.2f", 
			//				score, currentScore);
				}
			}
			else
				sprintf(logbuf, "thresholud not met", 
						proj_la, ninfo_arr[i] -> max_load);

	//	}
	//	else
	//	{
	//		sprintf(logbuf, "Node Rejected, node does not fit job requirements.");
	//	}



endofloop:	sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_NODE, ninfo_arr[i]->name,logbuf);
		i++;
		if (ninfo_arr[i] == NULL)
			i = 0;
	}

	//release the memory used
	relRepInfo(&replicaCount,&nodes_filelocated);


	if(good_node == NULL && possible_node == NULL)
	{
		sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_NODE, "NONE", "no nodes available now");
		return NULL;	

	}
	else if (good_node == NULL)
	{
		sprintf(logbuf, "%s on possible  %s", jinfo->name,possible_node -> name);
		sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_NODE,"Job Run", logbuf);
		return possible_node;
	}
	else
	{
		sprintf(logbuf, "%s on good  %s", jinfo->name,good_node -> name);
		sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_NODE,"Job Run", logbuf);	
		return good_node;
	}
	return NULL;  /* should never get here */
}





/*
 *
 * run_update_job - run the job and update the job information
 *
 *   pbs_sd - connection to pbs_server
 *   sinfo  - server job is on
 *   qinfo  - queue job resides in
 *   jinfo  - the job to run
 *
 * returns success/failure - see pbs_errno for more info
 *
 */

#define RUJ_BUFSIZ 1024

int run_update_job(int pbs_sd, server_info *sinfo, queue_info *qinfo,
		job_info *jinfo)
{
	int ret;    /* return code from pbs_runjob() */
	node_info *best_node = NULL;  /* best node to run job on */
	char *best_node_name = NULL;  /* name of best node */
  char buf[RUJ_BUFSIZ] = {'\0'};  /* generic buffer - comments & logging*/
  char timebuf[128];   /* buffer to hold the time and date */
  resource_req *res;   /* ptr to the resource of ncpus */
  int ncpus;    /* numeric amount of resource ncpus */
  char *errmsg;    /* used for pbs_geterrmsg() */

  strftime(timebuf, 128, "started on %a %b %d at %H:%M", localtime(&cstat.current_time));
  best_node = dataAwareDispatch(jinfo);
  if(best_node)
  	best_node_name = best_node -> name;

  char logbuf[100]= {0}	;
  sprintf(logbuf,"submission, *%s* on *%s*",jinfo->name,best_node_name); 
  sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, "submission", logbuf);
  if (best_node == NULL)
    snprintf(buf, RUJ_BUFSIZ, "Job %s", timebuf);

  update_job_comment(pbs_sd, jinfo, buf);

  buf[0] = '\0';

  ret = pbs_runjob(pbs_sd, jinfo -> name, best_node_name, NULL);

  if (ret == 0)
    {
    /* If a job is 100% efficent, it will raise the load average by 1 per
     * cpu is uses.  Temporarly inflate load average by that value
     */
 //   if (cstat.load_balancing && best_node != NULL)
   //   {

		sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, "submission", "load_balancing now");
		if ((res = find_resource_req(jinfo -> resreq, "ncpus")) == NULL)
			ncpus = 1;
		else
			ncpus = res -> amount;

		if(best_node)
			best_node -> loadave += ncpus;
  //    }

    if (cstat.help_starving_jobs && jinfo == cstat.starving_job)
      jinfo -> sch_priority = 0;

    sched_log(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, jinfo -> name, "Job Run");

    update_server_on_run(sinfo, qinfo, jinfo);

    update_queue_on_run(qinfo, jinfo);

    update_job_on_run(pbs_sd, jinfo);

    if (cstat.fair_share)
      update_usage_on_run(jinfo);

    free(sinfo -> running_jobs);

    sinfo -> running_jobs = job_filter(sinfo -> jobs, sinfo -> sc.total,
                                       check_run_job, NULL);

    free(qinfo -> running_jobs);

    qinfo -> running_jobs = job_filter(qinfo -> jobs, qinfo -> sc.total,
                                       check_run_job, NULL);
    }
  else
    {
    errmsg = pbs_geterrmsg(pbs_sd);
    snprintf(buf, RUJ_BUFSIZ, "Not Running - PBS Error: %s", errmsg);
    update_job_comment(pbs_sd, jinfo, buf);
    }

  return ret;
  }

/*
 *
 * next_job - find the next job to be run by the scheduler
 *
 *   sinfo - the server the jobs are on
 *   init  - whether or not to initialize
 *
 * returns
 *     - the next job to run
 *     - NULL on error or if there are no more jobs to run
 *
 */
job_info *next_job(server_info *sinfo, int init)
{
	/* cjobs is an array of the queue's job_info arrays.  It is used to cycle
	 * through the jobs returning 1 job from each queue
	 */
	static job_info ***cjobs = NULL;

	/* last_queue is the index into a queue array of the last time
	 * the function was called
	 */
	static int last_queue;

	/* last_job is the index into a job array of the last time
	 * the function was called
	 */
	static int last_job;

	job_info *rjob = NULL;  /* the job to return */
	int i;

	if (cstat.round_robin)
	{
		if (init == INITIALIZE)
		{
			if (cjobs != NULL)
			{
				free(cjobs);
				cjobs = NULL;
			}

			/*
			 * cjobs is an array of pointers which will point to each of the job
			 * arrays in each queue
			 */
			if (cjobs == NULL)
			{
				if ((cjobs = (job_info ***) malloc(sizeof(job_info**) * sinfo -> num_queues)) == NULL)
				{
					perror("Memory Allocation Error");
					return NULL;
				}

				last_queue = -1;

				last_job = 0;

				for (i = 0; i < sinfo -> num_queues; i++)
					cjobs[i] = sinfo -> queues[i] -> jobs;
			}
		} /* end initalization */
		else
		{
			/* find the next queue to run a job out of */
			for (i = 0; i < sinfo -> num_queues && rjob == NULL; i++)
			{
				/* we have reached the end of the array, cycle back through */
				if (last_queue == (sinfo -> num_queues - 1))
				{
					last_queue = 0;
					last_job++;
				}
				else /* still more queues to go */
					last_queue++;

				if (cjobs[last_queue] != NULL)
				{
					if (cjobs[last_queue][last_job] == NULL)
						cjobs[last_queue] = NULL;
					else
					{
						if (cstat.fair_share)
							rjob = extract_fairshare(cjobs[last_queue]);
						else if (cjobs[last_queue][last_job] -> can_not_run == 0)
							rjob = cjobs[last_queue][last_job];
					}
				}
			}
		}
	}
	else if (cstat.by_queue)
	{

		if (init == INITIALIZE)
		{
			last_job = -1;
			last_queue = 0;
		}
		else
		{
			if (init == INITIALIZE)
			{
				last_job = -1;
				last_queue = 0;
			}
			else
			{
				last_job++;

				/* check if we have reached the end of a queue and need to find another */

				while (last_queue < sinfo -> num_queues &&
						(sinfo -> queues[last_queue] -> jobs == NULL ||
						 sinfo -> queues[last_queue] -> jobs[last_job] == NULL ||
						 sinfo -> queues[last_queue] -> jobs[last_job] -> can_not_run))
				{
					if (sinfo -> queues[last_queue] -> jobs == NULL ||
							sinfo -> queues[last_queue] -> jobs[last_job] == NULL)
					{
						last_queue++;
						last_job = 0;
					}
					else if (sinfo -> queues[last_queue] -> jobs[last_job] -> can_not_run)
						last_job++;
				}

				if (last_queue == sinfo -> num_queues ||
						sinfo -> queues[last_queue] == NULL)
					rjob = NULL;
				else
				{
					if (cstat.fair_share)
						rjob = extract_fairshare(sinfo -> queues[last_queue] -> jobs);
					else
					{
						if (last_job < sinfo -> queues[last_queue] -> sc.total)
							rjob = sinfo -> queues[last_queue] -> jobs[last_job];
					}
				}
			}
		}
	}
	else /* treat the entire system as one large queue */
	{
		if (init == INITIALIZE)
		{
			last_job = -1;
		}
		else
		{
			if (cstat.fair_share)
				rjob = extract_fairshare(sinfo -> jobs);
			else
			{
				last_job++;

				while (sinfo -> jobs[last_job] != NULL &&
						sinfo -> jobs[last_job] -> can_not_run)
				{
					last_job++;
				}

				rjob = sinfo -> jobs[last_job];
			}
		}
	}

	return rjob;
}
