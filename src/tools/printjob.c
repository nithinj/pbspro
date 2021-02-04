/*
 * Copyright (C) 1994-2021 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of both the OpenPBS software ("OpenPBS")
 * and the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * OpenPBS is free software. You can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * OpenPBS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * PBS Pro is commercially licensed software that shares a common core with
 * the OpenPBS software.  For a copy of the commercial license terms and
 * conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
 * Altair Legal Department.
 *
 * Altair's dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of OpenPBS and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair's trademarks, including but not limited to "PBS™",
 * "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
 * subject to Altair's trademark licensing policies.
 */

/**
 * @file printjob.c
 *
 * @brief
 *		printjob.c - This file contains the functions related to the print job task.
 *
 * Functions included are:
 * 	print_usage()
 * 	prt_job_struct()
 * 	prt_task_struct()
 * 	read_attr()
 * 	print_db_job()
 * 	main()
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>

#define	PBS_MOM 1	/* this is so we can use the task struct */

#include "cmds.h"
#include "pbs_version.h"
#include "portability.h"
#include "list_link.h"
#include "attribute.h"
#include "server_limits.h"
#include "job.h"
#ifdef PRINTJOBSVR
#include "pbs_db.h"
void *conn = NULL;
#endif

#ifdef PRINTJOBSVR
/* just to make jattr_get_set.c happy */
attribute_def job_attr_def[1] = {{0}};
#endif

#define BUF_SIZE 512
int display_script = 0;	 /* to track if job-script is required or not */
/**
 * @brief
 *		Print usage text to stderr and exit.
 *
 * @return	void
 *
 */
void
print_usage()
{
	fprintf(stderr, "Usage: %s [-a] (jobid|file)\n", "printjob");
	fprintf(stderr, "       %s -s jobid\n", "printjob");
	fprintf(stderr, "       %s --version\n", "printjob");
}
/**
 * @brief
 *		print the job struct.
 *
 * @param[in]	pjob	-	pointer to the job struct.
 */
static void
prt_job_struct(job *pjob, char *state, char *substate)
{
	unsigned int ss_num;
	unsigned int s_num;
	char *endp = NULL;

	ss_num = strtol(substate, &endp, 10);
	s_num = state_char2int(state[0]);

	printf("---------------------------------------------------\n");
	printf("jobid:\t%s\n", pjob->ji_qs.ji_jobid);
	printf("---------------------------------------------------\n");
	printf("state:\t\t0x%x\n", s_num);
	printf("substate:\t0x%x (%d)\n", ss_num, ss_num);
	printf("svrflgs:\t0x%x (%d)\n", pjob->ji_qs.ji_svrflags,
		pjob->ji_qs.ji_svrflags);
	printf("stime:\t\t%ld\n", (long)pjob->ji_qs.ji_stime);
	printf("file base:\t%s\n", pjob->ji_qs.ji_fileprefix);
	printf("queue:\t\t%s\n", pjob->ji_qs.ji_queue);
	switch (pjob->ji_qs.ji_un_type) {
		case JOB_UNION_TYPE_NEW:
			printf("union type new:\n");
			printf("\tsocket\t%d\n", pjob->ji_qs.ji_un.ji_newt.ji_fromsock);
			printf("\taddr\t%lu\n", pjob->ji_qs.ji_un.ji_newt.ji_fromaddr);
			printf("\tscript\t%d\n", pjob->ji_qs.ji_un.ji_newt.ji_scriptsz);
			break;
		case JOB_UNION_TYPE_EXEC:
			printf("union type exec:\n");
			printf("\texits\t%d\n",
				pjob->ji_qs.ji_un.ji_exect.ji_exitstat);
			break;
		case JOB_UNION_TYPE_ROUTE:
			printf("union type route:\n");
			printf("\tquetime\t%ld\n",
				(long)pjob->ji_qs.ji_un.ji_routet.ji_quetime);
			printf("\tretry\t%ld\n",
				(long)pjob->ji_qs.ji_un.ji_routet.ji_rteretry);
			break;
		case JOB_UNION_TYPE_MOM:
			printf("union type mom:\n");
			printf("\tsvraddr\t%lu\n",
				pjob->ji_qs.ji_un.ji_momt.ji_svraddr);
			printf("\texitst\t%d\n", pjob->ji_qs.ji_un.ji_momt.ji_exitstat);
			printf("\tuid\t%d\n", pjob->ji_qs.ji_un.ji_momt.ji_exuid);
			printf("\tgid\t%d\n", pjob->ji_qs.ji_un.ji_momt.ji_exgid);
			break;
		default:
			printf("--bad union type %d\n", pjob->ji_qs.ji_un_type);
	}
}
/**
 * @brief
 *		print the pbs_task struct.
 *
 * @param[in]	ptask	-	pointer to the task struct.
 */
void
prt_task_struct(pbs_task *ptask)
{
	printf("\n");
	printf("\tparentjobid:\t%s\n", ptask->ti_qs.ti_parentjobid);
	printf("\tparentnode:\t%d\n", ptask->ti_qs.ti_parentnode);
	printf("\tmyvnode:\t%d\n", ptask->ti_qs.ti_myvnode);
	printf("\tparenttask:\t%d\n", ptask->ti_qs.ti_parenttask);
	printf("\ttask:\t\t%d\n", ptask->ti_qs.ti_task);
	printf("\tstatus:\t\t%d\t", ptask->ti_qs.ti_status);
	switch (ptask->ti_qs.ti_status) {

		case TI_STATE_EMBRYO:
			printf("TI_STATE_EMBRYO\n");
			break;

		case TI_STATE_RUNNING:
			printf("TI_STATE_RUNNING\n");
			break;

		case TI_STATE_EXITED:
			printf("TI_STATE_EXITED\n");
			break;

		case TI_STATE_DEAD:
			printf("TI_STATE_DEAD\n");
			break;

		default:
			printf("unknown value\n");
			break;
	}

	printf("\tsid:\t\t%d\n", ptask->ti_qs.ti_sid);
	printf("\texitstat:\t%d\n", ptask->ti_qs.ti_exitstat);
}

#define ENDATTRIBUTES -711

/**
 * @brief	Print an attribute
 *
 * @param[in]	pal  - pointer to attribute
 *
 * @return	void
 */
static void
print_attr(svrattrl *pal)
{
	printf("%s", pal->al_name);
	if (pal->al_resc)
		printf(".%s", pal->al_resc);
	printf(" = ");
	if (pal->al_value)
		printf("%s", show_nonprint_chars(pal->al_value));
	printf("\n");
}

/**
 * @brief
 * 		read attributes from file descriptor.
 *
 * @param[in]	fd	-	file descriptor.
 *
 * @return	int
 * @return	1	: success
 * @return	0	: failure
 */
static svrattrl *
read_attr(int fd)
{
	int amt;
	int i;
	svrattrl *pal;
	svrattrl tempal;

	i = read(fd, (char *)&tempal, sizeof(tempal));
	if (i != sizeof(tempal)) {
		fprintf(stderr, "bad read of attribute\n");
		return NULL;
	}
	if (tempal.al_tsize == ENDATTRIBUTES)
		return NULL;

	pal = (svrattrl *)malloc(tempal.al_tsize);
	if (pal == NULL) {
		fprintf(stderr, "malloc failed\n");
		exit(1);
	}
	*pal = tempal;

	/* read in the actual attribute data */

	amt = pal->al_tsize - sizeof(svrattrl);
	i = read(fd, (char *)pal + sizeof(svrattrl), amt);
	if (i != amt) {
		fprintf(stderr, "short read of attribute\n");
		exit(2);
	}
	pal->al_name = (char *)pal + sizeof(svrattrl);
	if (pal->al_rescln)
		pal->al_resc = pal->al_name + pal->al_nameln;
	else
		pal->al_resc = NULL;
	if (pal->al_valln)
		pal->al_value = pal->al_name + pal->al_nameln + pal->al_rescln;
	else
		pal->al_value = NULL;

	return pal;
}

/**
 * @brief	Read all job attribute values
 *
 * @param[in]	fd - fd of job file
 * @param[out]	state - return pointer to state value
 * @param[out]	substate - return pointer for substate value
 *
 * @return	void
 */
static svrattrl *
read_all_attrs(int fd, char **state, char **substate)
{
	svrattrl *pal = NULL;
	svrattrl *pali = NULL;

	while ((pali = read_attr(fd)) != NULL) {
		if (pal == NULL) {
			pal = pali;
			(&pal->al_link)->ll_struct = (void *)(&pal->al_link);
			(&pal->al_link)->ll_next = NULL;
			(&pal->al_link)->ll_prior = NULL;
		} else {
			pbs_list_link *head = &pal->al_link;
			pbs_list_link *newp = &pali->al_link;
			newp->ll_prior = NULL;
			newp->ll_next  = head;
			newp->ll_struct = pali;
			pal = pali;
		}
		/* Check if the attribute read is state/substate and store it separately */
		if (strcmp(pali->al_name, ATTR_state) == 0)
			*state = pali->al_value;
		else if (strcmp(pali->al_name, ATTR_substate) == 0)
			*substate = pali->al_value;
	}

	return pal;
}

/**
 * @brief
 * 		save the db info into job structure
 *
 * @param[out]	pjob	-	pointer to job struct
 * @param[in]	pdjob	-	pbs DB job info.
 */
#ifdef PRINTJOBSVR
static void
db_2_job(job *pjob,  pbs_db_job_info_t *pdjob)
{
	char statec;

	strcpy(pjob->ji_qs.ji_jobid, pdjob->ji_jobid);
	statec = state_int2char(pdjob->ji_state);
	if (statec != '0')
		set_job_state(pjob, statec);

	set_job_substate(pjob, pdjob->ji_substate);
	pjob->ji_qs.ji_svrflags = pdjob->ji_svrflags;
	pjob->ji_qs.ji_stime = pdjob->ji_stime;
	pjob->ji_qs.ji_fileprefix[0] = 0;
	strcpy(pjob->ji_qs.ji_queue, pdjob->ji_queue);
	strcpy(pjob->ji_qs.ji_destin, pdjob->ji_destin);
	pjob->ji_qs.ji_un_type = pdjob->ji_un_type;
	if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_NEW) {
		pjob->ji_qs.ji_un.ji_newt.ji_fromsock = pdjob->ji_fromsock;
		pjob->ji_qs.ji_un.ji_newt.ji_fromaddr = pdjob->ji_fromaddr;
	} else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_EXEC)
		pjob->ji_qs.ji_un.ji_exect.ji_exitstat = pdjob->ji_exitstat;
	else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_ROUTE) {
		pjob->ji_qs.ji_un.ji_routet.ji_quetime = pdjob->ji_quetime;
		pjob->ji_qs.ji_un.ji_routet.ji_rteretry = pdjob->ji_rteretry;
	}

	/* extended portion */
	strcpy(pjob->ji_extended.ji_ext.ji_jid, pdjob->ji_jid);
	pjob->ji_extended.ji_ext.ji_credtype = pdjob->ji_credtype;
}

/**
 * @brief
 * 		enable db lookup only for server version of printjob
 *
 * @param[in]	id	-	Job Id.
 * @param[in]	no_attributes	-	if set means no need to set attr_info
 *
 * @return	int
 */
int
print_db_job(char *id, int no_attributes)
{
	pbs_db_obj_info_t obj;
	pbs_db_job_info_t dbjob;
	pbs_db_jobscr_info_t jobscr;
	job xjob;
	char *db_errmsg = NULL;
	int failcode;

	if (conn == NULL) {

		/* connect to database */
#ifdef NAS /* localmod 111 */
		if (pbs_conf.pbs_data_service_host) {
			failcode = pbs_db_connect(&conn, pbs_conf.pbs_data_service_host, pbs_conf.pbs_data_service_port, PBS_DB_CNT_TIMEOUT_NORMAL);
		}
		else
#endif /* localmod 111 */
		failcode = pbs_db_connect(&conn, pbs_conf.pbs_server_name, pbs_conf.pbs_data_service_port, PBS_DB_CNT_TIMEOUT_NORMAL);
		if (!conn && pbs_conf.pbs_secondary != NULL) {
			failcode = pbs_db_connect(&conn, pbs_conf.pbs_secondary, pbs_conf.pbs_data_service_port, PBS_DB_CNT_TIMEOUT_NORMAL);
			if (!conn) {
				pbs_db_get_errmsg(failcode, &db_errmsg);
				fprintf(stderr, "%s\n", db_errmsg);
				free(db_errmsg);
				return -1;
			}
		}
	}

	/*
	 * On a server machine, if display_script is set,
	 * retrieve the job-script from database.
	 */
	if (display_script) {
		obj.pbs_db_obj_type = PBS_DB_JOBSCR;
		obj.pbs_db_un.pbs_db_jobscr = &jobscr;
		strcpy(jobscr.ji_jobid, id);
		if (strchr(id, '.') == 0) {
			strcat(jobscr.ji_jobid, ".");
			strcat(jobscr.ji_jobid, pbs_conf.pbs_server_name);
		}

		if (pbs_db_load_obj(conn, &obj) != 0) {
			fprintf(stderr, "Job %s not found\n", jobscr.ji_jobid);
			return (1);
		}
		else {
			printf("---------------------------------------------------\n");
			printf("Jobscript for jobid:%s\n", jobscr.ji_jobid);
			printf("---------------------------------------------------\n");

			printf("%s \n", jobscr.script);
		}
	}

	/*
	 * On a server machine, if display_script is not set,
	 * retrieve the job info from database.
	 */
	else {
		char state[2];
		char substate[4];
		obj.pbs_db_obj_type = PBS_DB_JOB;
		obj.pbs_db_un.pbs_db_job = &dbjob;
		strcpy(dbjob.ji_jobid, id);
		if (strchr(id, '.') == 0) {
			strcat(dbjob.ji_jobid, ".");
			strcat(dbjob.ji_jobid, pbs_conf.pbs_server_name);
		}

		if (pbs_db_load_obj(conn, &obj) !=0) {
			fprintf(stderr, "Job %s not found\n", dbjob.ji_jobid);
			return (1);
		}
		db_2_job(&xjob, &dbjob);
		snprintf(state, sizeof(state), "%c", get_job_state(&xjob));
		snprintf(substate, sizeof(substate), "%ld", get_job_substate(&xjob));
		prt_job_struct(&xjob, state, substate);

		if (no_attributes == 0) {
			svrattrl *pal;
			printf("--attributes--\n");
			for (pal = (svrattrl *)GET_NEXT(dbjob.db_attr_list.attrs); pal != NULL; pal = (svrattrl *)GET_NEXT(pal->al_link)) {
				printf("%s", pal->al_atopl.name);
				if (pal->al_atopl.resource && pal->al_atopl.resource[0] != 0)
					printf(".%s", pal->al_atopl.resource);
				printf(" = ");
				if (pal->al_atopl.value)
					printf("%s", pal->al_atopl.value);
				printf("\n");
			}

		}
		printf("\n");
	}

	return 0;
}
#endif
/**
 * @brief
 *      This is main function of printjob.
 *
 * @return	int
 * @retval	0	: success
 * @retval	1	: failure
 */
int
main(argc, argv)
int argc;
char *argv[];
{
	int amt;
	int err = 0;
	int f;
	int fp;
	int no_attributes = 0;
	job xjob;
	pbs_task xtask;
	extern int optopt;
	extern int opterr;
	extern int optind;
	char *job_id = NULL;
	char job_script[BUF_SIZE];

	/*
	 * Check for the user. If the user is not root/administrator,
	 * display appropriate error message and exit. Else, continue.
	 */

#ifdef WIN32
	if (!isAdminPrivilege(getlogin())) {
		fprintf(stderr, "printjob must be run by Admin\n");
		exit(1);
	}

#else
	if ((getuid() != 0) || (geteuid() != 0)) {
		fprintf(stderr, "printjob must be run by root\n");
		exit(1);
	}
#endif

	if (pbs_loadconf(0) == 0) {
		fprintf(stderr, "%s\n", "couldnot load conf file");
		exit(1);
	}

	/*the real deal or output pbs_version and exit?*/
	PRINT_VERSION_AND_EXIT(argc, argv);
	opterr = 0;
	while ((f = getopt(argc, argv, "as")) != EOF) {
		switch (f) {
			case 'a':
				if (display_script) {
					print_usage();
					exit(1);
				}
				no_attributes = 1;
				break;

			case 's':
				/* set display_script if job-script is required */
				if (no_attributes) {
					print_usage();
					exit(1);
				}
				display_script = 1;
				break;

			default:
				err = 1;
				fprintf(stderr, "printjob: invalid option -- %c\n", optopt);

		}
	}
	if (err || (argc - optind < 1)) {
		print_usage();
		return 1;
	}

	for (f=optind; f<argc; ++f) {
		char	*jobfile = argv[f];
		int	len;
		char	*dirname;
		DIR	*dirp;
		FILE *fp_script = NULL;
		struct dirent	*dp;

		fp = open(jobfile, O_RDONLY, 0);

		if (display_script) {

			/*
			 * if open() succeeds, it means argument is jobfile-path which
			 * is not allowed with -s option. Print the usage error and exit
			 */
			if (fp > 0) {
				print_usage();
				close(fp);
				exit(1);
			}
		}

		/* If open () fails to open the jobfile, assume argument is jobid */
		if (fp < 0) {

#ifdef PRINTJOBSVR
			if (print_db_job(jobfile, no_attributes) == 0) {
				continue;
			}
			else {
				if (conn != NULL) {
					pbs_db_disconnect(conn);
				}
				exit(1);
			}
#else
			/*
			 * On non-server host, execute the following code when
			 * the job-id is given to open the job file in mom_priv
			 */
			job_id = (char *) malloc(strlen(jobfile)+ strlen(pbs_conf.pbs_server_name)+2);
			if (job_id == NULL) {
				perror("malloc failed");
				exit(1);
			}
			strcpy(job_id, jobfile);
			if (strchr(job_id, '.') == 0) {
				strcat(job_id, ".");
				strcat(job_id, pbs_conf.pbs_server_name);
			}

			/*frame the jobfile to contain $PBS_HOME/mom_priv/jobs/jobid.JB */
			jobfile = (char *) malloc(strlen(pbs_conf.pbs_home_path)+(strlen(job_id))
				+(strlen("/mom_priv/jobs/.JB"))+1);
			if (jobfile == NULL) {
				perror("malloc failed");
				free(job_id);
				exit(1);
			}
			sprintf(jobfile, "%s/mom_priv/jobs/%s.JB", pbs_conf.pbs_home_path, job_id);
			fp = open(jobfile, O_RDONLY, 0);

			/* If open() fails, the jobfile formed by jobid is not found in $PBS_HOME */
			if (fp < 0) {
				fprintf(stderr, "Job %s not found\n", job_id);
				free(job_id);
				free(jobfile);
				exit(1);
			}
#endif
		}

		/* If not asked for displaying of script, execute below code */
		if (!display_script) {
			svrattrl *pal, *pali;
			char *state = "";
			char *substate = "";

			amt = read(fp, &xjob.ji_qs, sizeof(xjob.ji_qs));
			if (amt != sizeof(xjob.ji_qs)) {
				fprintf(stderr, "Short read of %d bytes, file %s\n",
					amt, jobfile);
			}

			/* if present, skip over extended area */
			if (xjob.ji_qs.ji_jsversion > 500) {
				amt = read(fp, &xjob.ji_extended, sizeof(xjob.ji_extended));
				if (amt != sizeof(xjob.ji_extended)) {
					fprintf(stderr, "Short read of %d bytes, file %s\n",
						amt, jobfile);
				}
			}

			/* if array job, skip over sub job table */
			if (xjob.ji_qs.ji_svrflags & JOB_SVFLG_ArrayJob) {
				size_t xs;
				ajinfo_t *ajtrk;

				if (read(fp, (char *)&xs, sizeof(xs)) != sizeof(xs)) {
					if ((ajtrk = (ajinfo_t *)malloc(xs)) == NULL) {
						(void)close(fp);
						return 1;
					}
					read(fp, (char *)ajtrk + sizeof(xs), xs - sizeof(xs));
					free(ajtrk);
				}
			}

			pal = read_all_attrs(fp, &state, &substate);

			/* Print the summary first */
			prt_job_struct(&xjob, state, substate);

			/* now do attributes, one at a time */
			if (no_attributes == 0) {
				/* Now print all attributes */
				printf("--attributes--\n");

				pali = GET_NEXT(pal->al_link);
				while (pali != NULL) {
					print_attr(pali);
					if (pali->al_link.ll_next == NULL)
						break;
					pali = GET_NEXT(pali->al_link);
				}
			}

			(void)close(fp);
			printf("\n");

			len = strlen(jobfile);
			if (len <= 2 ||
				jobfile[len-2] != 'J' ||
				jobfile[len-1] != 'B')
				continue;
			dirname = malloc(len + 50);
			strcpy(dirname, jobfile);

			dirname[len-2] = 'T';
			dirname[len-1] = 'K';
			dirp = opendir(dirname);

			if (dirp == NULL) {
				free(dirname);
				continue;
			}

			dirname[len++] = '/';
			dirname[len] = '\0';
			while (errno = 0, (dp = readdir(dirp)) != NULL) {
				if (dp->d_name[0] == '.')
					continue;
				strcpy(&dirname[len], dp->d_name);

				printf("task file %s\n", dirname);
				fp = open(dirname, O_RDONLY, 0);
				if (fp < 0) {
					perror("open failed");
					continue;
				}

				amt = read(fp, &xtask.ti_qs, sizeof(xtask.ti_qs));
				if (amt != sizeof(xtask.ti_qs)) {
					fprintf(stderr,
						"Short read of %d bytes\n", amt);
					continue;
				}

				prt_task_struct(&xtask);
				close(fp);
			}
			if (errno != 0 && errno != ENOENT) {
				perror("readdir failed");
				free(dirname);
				closedir(dirp);
				continue;
			}
			free(dirname);
			if (jobfile != argv[f])
				free(jobfile);
			if (job_id)
				free(job_id);
			closedir(dirp);
		}
		/* if asked for displaying of script, execute below code  (for mom-side) */
		else {

			len = strlen(jobfile);
			jobfile[len - 2] = 'S';
			jobfile[len - 1] = 'C';

			fp_script = fopen(jobfile, "r");

			/* If fopen fails, display the usage error */
			if (fp_script == NULL) {
				print_usage();
				exit(1);
			}
			if (job_id) {
				printf("--------------------------------------------------\n");
				printf("jobscript for %s\n", job_id);
				printf("--------------------------------------------------\n");
			}
			while ((fgets(job_script, BUF_SIZE-1, fp_script)) != NULL) {
				if (fputs(job_script, stdout) < 0) {
					fprintf(stderr, "Error reading job-script file\n");
					exit(1);
				}
			}
			printf("\n");
			free(jobfile);
			fclose(fp_script);
			free(job_id);
			close(fp);
		}
	}
#ifdef PRINTJOBSVR
	if (conn != NULL) {
		pbs_db_disconnect(conn);
	}
#endif
	return (0);
}
