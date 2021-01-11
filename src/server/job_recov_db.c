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

#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include <sys/types.h>
#include <sys/param.h>
#include <execinfo.h>

#include "pbs_ifl.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <unistd.h>
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "job.h"
#include "reservation.h"
#include "queue.h"
#include "log.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include <memory.h>
#include "libutil.h"
#include "pbs_db.h"


#define MAX_SAVE_TRIES 3

extern void *svr_db_conn;
extern int server_init_type;
extern pbs_list_head svr_allresvs;
#define BACKTRACE_BUF_SIZE 50
void print_backtrace(char *);

/* global data items */
extern time_t time_now;

job *recov_job_cb(pbs_db_obj_info_t *dbobj, int *refreshed);
resc_resv *recov_resv_cb(pbs_db_obj_info_t *dbobj, int *refreshed);

/**
 * @brief
 *		convert job structure to DB format
 *
 * @see
 * 		job_save_db
 *
 * @param[in]	pjob - Address of the job in the server
 * @param[out]	dbjob - Address of the database job object
 *
 * @retval	-1  Failure
 * @retval	>=0 What to save: 0=nothing, OBJ_SAVE_NEW or OBJ_SAVE_QS
 */
static int
job_to_db(job *pjob, pbs_db_job_info_t *dbjob)
{
	int savetype = 0;
	int save_all_attrs = 0;

	strcpy(dbjob->ji_jobid, pjob->ji_qs.ji_jobid);

	if (check_job_state(pjob, JOB_STATE_LTR_FINISHED))
		save_all_attrs = 1;

	if ((encode_attr_db(job_attr_def, pjob->ji_wattr, JOB_ATR_LAST, &dbjob->db_attr_list, save_all_attrs)) != 0)
		return -1;

	if (pjob->newobj) /* object was never saved/loaded before */
		savetype |= (OBJ_SAVE_NEW | OBJ_SAVE_QS);

	if (compare_obj_hash(&pjob->ji_qs, sizeof(pjob->ji_qs), pjob->qs_hash) == 1) {
		int statenum;

		savetype |= OBJ_SAVE_QS;

		statenum = get_job_state_num(pjob);
		if (statenum == -1) {
			log_errf(PBSE_INTERNAL, __func__, "get_job_state_num failed for job state %c",
					get_job_state(pjob));
			return -1;
		}

		dbjob->ji_state     = statenum;
		dbjob->ji_substate  = get_job_substate(pjob);
		dbjob->ji_svrflags  = pjob->ji_qs.ji_svrflags;
		dbjob->ji_stime     = pjob->ji_qs.ji_stime;
		strcpy(dbjob->ji_queue, pjob->ji_qs.ji_queue);
		strcpy(dbjob->ji_destin, pjob->ji_qs.ji_destin);
		dbjob->ji_un_type   = pjob->ji_qs.ji_un_type;
		if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_NEW) {
			dbjob->ji_fromsock  = pjob->ji_qs.ji_un.ji_newt.ji_fromsock;
			dbjob->ji_fromaddr  = pjob->ji_qs.ji_un.ji_newt.ji_fromaddr;
		} else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_EXEC)
			dbjob->ji_exitstat  = pjob->ji_qs.ji_un.ji_exect.ji_exitstat;
		else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_ROUTE) {
			dbjob->ji_quetime   = pjob->ji_qs.ji_un.ji_routet.ji_quetime;
			dbjob->ji_rteretry  = pjob->ji_qs.ji_un.ji_routet.ji_rteretry;
		} else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_MOM) {
			dbjob->ji_exitstat  = pjob->ji_qs.ji_un.ji_momt.ji_exitstat;
		}
		/* extended portion */
		strcpy(dbjob->ji_jid, pjob->ji_extended.ji_ext.ji_jid);
		dbjob->ji_credtype  = pjob->ji_extended.ji_ext.ji_credtype;
		dbjob->ji_qrank = get_jattr_long(pjob, JOB_ATR_qrank);
	}

	return savetype;
}

/**
 * @brief
 *		convert from database to job structure
 *
 * @see
 * 		job_recov_db
 *
 * @param[out]	pjob - Address of the job in the server
 * @param[in]	dbjob - Address of the database job object
 *
 * @retval   !=0  Failure
 * @retval   0    Success
 */
static int
db_to_job(job *pjob,  pbs_db_job_info_t *dbjob)
{
	char statec;

	/* Variables assigned constant values are not stored in the DB */
	pjob->ji_qs.ji_jsversion = JSVERSION;
	strcpy(pjob->ji_qs.ji_jobid, dbjob->ji_jobid);

	statec = state_int2char(dbjob->ji_state);
	if (statec == '0') {
		log_errf(PBSE_INTERNAL, __func__, "state_int2char failed to convert state %d", dbjob->ji_state);
		return 1;
	}
	set_job_state(pjob, statec);
	set_job_substate(pjob, dbjob->ji_substate);

	pjob->ji_qs.ji_svrflags = dbjob->ji_svrflags;
	pjob->ji_qs.ji_stime = dbjob->ji_stime;
	strcpy(pjob->ji_qs.ji_queue, dbjob->ji_queue);
	strcpy(pjob->ji_qs.ji_destin, dbjob->ji_destin);
	pjob->ji_qs.ji_fileprefix[0] = 0;
	pjob->ji_qs.ji_un_type = dbjob->ji_un_type;
	if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_NEW) {
		pjob->ji_qs.ji_un.ji_newt.ji_fromsock = dbjob->ji_fromsock;
		pjob->ji_qs.ji_un.ji_newt.ji_fromaddr = dbjob->ji_fromaddr;
		pjob->ji_qs.ji_un.ji_newt.ji_scriptsz = 0;
	} else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_EXEC)
		pjob->ji_qs.ji_un.ji_exect.ji_exitstat = dbjob->ji_exitstat;
	else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_ROUTE) {
		pjob->ji_qs.ji_un.ji_routet.ji_quetime = dbjob->ji_quetime;
		pjob->ji_qs.ji_un.ji_routet.ji_rteretry = dbjob->ji_rteretry;
	} else if (pjob->ji_qs.ji_un_type == JOB_UNION_TYPE_MOM) {
		pjob->ji_qs.ji_un.ji_momt.ji_svraddr = 0;
		pjob->ji_qs.ji_un.ji_momt.ji_exitstat = dbjob->ji_exitstat;
		pjob->ji_qs.ji_un.ji_momt.ji_exuid = 0;
		pjob->ji_qs.ji_un.ji_momt.ji_exgid = 0;
	}

	/* extended portion */
	strcpy(pjob->ji_extended.ji_ext.ji_jid, dbjob->ji_jid);
	pjob->ji_extended.ji_ext.ji_credtype = dbjob->ji_credtype;

	if ((decode_attr_db(pjob, &dbjob->db_attr_list.attrs, job_attr_idx, job_attr_def, pjob->ji_wattr, JOB_ATR_LAST, JOB_ATR_UNKN)) != 0)
		return -1;

	compare_obj_hash(&pjob->ji_qs, sizeof(pjob->ji_qs), pjob->qs_hash);

	pjob->newobj = 0;

	return 0;
}

/**
 * @brief
 *		Save job to database
 *
 * @param[in]	pjob - The job to save
 *
 * @return      Error code
 * @retval	 0 - Success
 * @retval	-1 - Failure
 * @retval	 1 - Jobid clash, retry with new jobid
 *
 */
int
job_save_db(job *pjob)
{
	pbs_db_job_info_t dbjob = {{0}};
	pbs_db_obj_info_t obj;
	void *conn = svr_db_conn;
	int savetype;
	int rc = -1;
	int old_mtime, old_flags;
	char *conn_db_err = NULL;

	old_mtime = get_jattr_long(pjob, JOB_ATR_mtime);
	old_flags = (get_jattr(pjob, JOB_ATR_mtime))->at_flags;

	if ((savetype = job_to_db(pjob, &dbjob)) == -1)
		goto done;

	obj.pbs_db_obj_type = PBS_DB_JOB;
	obj.pbs_db_un.pbs_db_job = &dbjob;

	/* update mtime before save, so the same value gets to the DB as well */
	set_jattr_l_slim(pjob, JOB_ATR_mtime, time_now, SET);
	if ((rc = pbs_db_save_obj(conn, &obj, savetype)) == 0)
		pjob->newobj = 0;

done:
	free_db_attr_list(&dbjob.db_attr_list);

	if (rc != 0) {
		/* revert mtime, flags update */
		set_jattr_l_slim(pjob, JOB_ATR_mtime, old_mtime, SET);
		(get_jattr(pjob, JOB_ATR_mtime))->at_flags = old_flags;

		pbs_db_get_errmsg(PBS_DB_ERR, &conn_db_err);
		log_errf(PBSE_INTERNAL, __func__, "Failed to save job %s %s", pjob->ji_qs.ji_jobid, conn_db_err? conn_db_err : "");
		if (conn_db_err) {
			if ((savetype & OBJ_SAVE_NEW) && strstr(conn_db_err, "duplicate key value"))
				rc = 1;
			free(conn_db_err);
		}

		if (rc == -1)
			panic_stop_db();
	}

	return (rc);
}

/**
 * @brief
 *	Utility function called inside job_recov_db
 *
 * @param[in]	dbjob - Pointer to the database structure of a job
 * @param[in]   pjob  - Pointer to job structure to populate
 *
 * @retval	 NULL - Failure
 * @retval	!NULL - Success, pointer to job structure recovered
 *
 */
job *
job_recov_db_spl(pbs_db_job_info_t *dbjob, job *pjob)
{
	job *pj = NULL;

	if (!pjob) {
		pj = job_alloc();
		pjob = pj;
	}

	if (pjob) {
		if (db_to_job(pjob, dbjob) == 0)
			return (pjob);
	}

	/* error case */
	if (pj)
		job_free(pj); /* free if we allocated here */

	log_errf(PBSE_INTERNAL, __func__,  "Failed to decode job %s", dbjob->ji_jobid);

	return (NULL);
}

/**
 * @brief
 *	Recover job from database
 *
 * @param[in]	jid - Job id of job to recover
 * @param[in]	pjob - job pointer, if any, to be updated
 *
 * @return      The recovered job
 * @retval	 NULL - Failure
 * @retval	!NULL - Success, pointer to job structure recovered
 *
 */
job *
job_recov_db(char *jid, job *pjob)
{
	pbs_db_job_info_t dbjob = {{0}};
	pbs_db_obj_info_t obj;
	int rc = -1;
	void *conn = svr_db_conn;
	char *conn_db_err = NULL;

	strcpy(dbjob.ji_jobid, jid);

	rc = pbs_db_load_obj(conn, &obj);
	if (rc == -2)
		return pjob; /* no change in job, return the same job */

	if (rc == 0)
		pjob = job_recov_db_spl(&dbjob, pjob);
	else {
		pbs_db_get_errmsg(PBS_DB_ERR, &conn_db_err);
		log_errf(PBSE_INTERNAL, __func__, "Failed to load job %s %s", jid, conn_db_err? conn_db_err : "");
		free(conn_db_err);
	}

	free_db_attr_list(&dbjob.db_attr_list);

	return (pjob);
}

/**
 * @brief
 *		convert resv structure to DB format
 *
 * @see
 * 		resv_save_db
 *
 * @param[in]	presv - Address of the resv in the server
 * @param[out]  dbresv - Address of the database resv object
 *
 * @retval   -1  Failure
 * @retval   >=0 What to save: 0=nothing, OBJ_SAVE_NEW or OBJ_SAVE_QS
 */
static int
resv_to_db(resc_resv *presv,  pbs_db_resv_info_t *dbresv)
{
	int savetype = 0;

	strcpy(dbresv->ri_resvid, presv->ri_qs.ri_resvID);

	if ((encode_attr_db(resv_attr_def, presv->ri_wattr, (int)RESV_ATR_LAST, &(dbresv->db_attr_list), 0)) != 0)
		return -1;

	if (presv->newobj) /* object was never saved or loaded before */
		savetype |= (OBJ_SAVE_NEW | OBJ_SAVE_QS);

	if (compare_obj_hash(&presv->ri_qs, sizeof(presv->ri_qs), presv->qs_hash) == 1) {
		savetype |= OBJ_SAVE_QS;

		strcpy(dbresv->ri_queue, presv->ri_qs.ri_queue);
		dbresv->ri_duration = presv->ri_qs.ri_duration;
		dbresv->ri_etime = presv->ri_qs.ri_etime;
		dbresv->ri_state = presv->ri_qs.ri_state;
		dbresv->ri_stime = presv->ri_qs.ri_stime;
		dbresv->ri_substate = presv->ri_qs.ri_substate;
		dbresv->ri_svrflags = presv->ri_qs.ri_svrflags;
		dbresv->ri_tactive = presv->ri_qs.ri_tactive;
	}

	return savetype;
}

/**
 * @brief
 *		convert from database to resv structure
 *
 * @param[out]	presv - Address of the resv in the server
 * @param[in]	dbresv - Address of the database resv object
 *
 * @retval   !=0  Failure
 * @retval   0    Success
 */
static int
db_to_resv(resc_resv *presv, pbs_db_resv_info_t *dbresv)
{
	strcpy(presv->ri_qs.ri_resvID, dbresv->ri_resvid);
	strcpy(presv->ri_qs.ri_queue, dbresv->ri_queue);
	presv->ri_qs.ri_duration = dbresv->ri_duration;
	presv->ri_qs.ri_etime = dbresv->ri_etime;
	presv->ri_qs.ri_state = dbresv->ri_state;
	presv->ri_qs.ri_stime = dbresv->ri_stime;
	presv->ri_qs.ri_substate = dbresv->ri_substate;
	presv->ri_qs.ri_svrflags = dbresv->ri_svrflags;
	presv->ri_qs.ri_tactive = dbresv->ri_tactive;

	if ((decode_attr_db(presv, &dbresv->db_attr_list.attrs, resv_attr_idx, resv_attr_def, presv->ri_wattr, RESV_ATR_LAST, RESV_ATR_UNKN)) != 0)
		return -1;

	compare_obj_hash(&presv->ri_qs, sizeof(presv->ri_qs), presv->qs_hash);

	presv->newobj = 0;

	return 0;

}

/**
 * @brief
 *	Save resv to database
 *
 * @param[in]	presv - The resv to save
 * @param[in]   updatetype:
 *		SAVERESV_QUICK - Quick update without attributes
 *		SAVERESV_FULL  - Full update with attributes
 *		SAVERESV_NEW   - New resv, insert into database
 *
 * @return      Error code
 * @retval	 0 - Success
 * @retval	-1 - Failure
 * @retval	 1 - resvid clash, retry with new resvid
 *
 */
int
resv_save_db(resc_resv *presv)
{
	pbs_db_resv_info_t dbresv = {{0}};
	pbs_db_obj_info_t obj;
	void *conn = svr_db_conn;
	int savetype;
	int rc = -1;
	int old_mtime, old_flags;
	char *conn_db_err = NULL;
	attribute *mtime;

	mtime = get_rattr(presv, RESV_ATR_mtime);
	old_mtime = get_attr_l(mtime);
	old_flags = mtime->at_flags;

	if ((savetype = resv_to_db(presv, &dbresv)) == -1)
		goto done;

	obj.pbs_db_obj_type = PBS_DB_RESV;
	obj.pbs_db_un.pbs_db_resv = &dbresv;

	/* update mtime before save, so the same value gets to the DB as well */
	set_rattr_l_slim(presv, RESV_ATR_mtime, time_now, SET);
	if ((rc = pbs_db_save_obj(conn, &obj, savetype)) == 0)
		presv->newobj = 0;

done:
	free_db_attr_list(&dbresv.db_attr_list);

	if (rc != 0) {
		set_attr_l(mtime, old_mtime, SET);
		mtime->at_flags = old_flags;

		pbs_db_get_errmsg(PBS_DB_ERR, &conn_db_err);
		log_errf(PBSE_INTERNAL, __func__, "Failed to save resv %s %s", presv->ri_qs.ri_resvID, conn_db_err? conn_db_err : "");
		if(conn_db_err) {
			if ((savetype & OBJ_SAVE_NEW) && strstr(conn_db_err, "duplicate key value"))
				rc = 1;
			free(conn_db_err);
		}

		if (rc == -1)
			panic_stop_db();
	}

	return (rc);
}

/**
 * @brief
 *	Recover resv from database
 *
 * @param[in]	resvid - Resv id to recover
 * @param[in]	presv - Resv pointer, if any, to be updated
 *
 * @return      The recovered reservation
 * @retval	 NULL - Failure
 * @retval	!NULL - Success, pointer to resv structure recovered
 *
 */
resc_resv *
resv_recov_db(char *resvid, resc_resv *presv)
{
	resc_resv *pr = NULL;
	pbs_db_resv_info_t dbresv = {{0}};
	pbs_db_obj_info_t obj;
	void *conn = svr_db_conn;
	int rc = -1;
	char *conn_db_err = NULL;

	if (!presv) {
		if ((pr = resv_alloc(resvid)) == NULL) {
			log_err(-1, __func__, "resv_alloc failed");
			return NULL;
		}
		presv = pr;
	}

	strcpy(dbresv.ri_resvid, resvid);
	obj.pbs_db_obj_type = PBS_DB_RESV;
	obj.pbs_db_un.pbs_db_resv = &dbresv;

	rc = pbs_db_load_obj(conn, &obj);
	if (rc == -2)
		return presv; /* no change in resv */

	if (rc == 0)
		rc = db_to_resv(presv, &dbresv);

	free_db_attr_list(&dbresv.db_attr_list);

	if (rc != 0) {
		presv = NULL; /* so we return NULL */

		pbs_db_get_errmsg(PBS_DB_ERR, &conn_db_err);
		log_errf(PBSE_INTERNAL, __func__, "Failed to load resv %s %s", resvid, conn_db_err? conn_db_err : "");
		free(conn_db_err);
		if (pr)
			resv_free(pr); /* free if we allocated here */
	}

	return presv;
}

/**
 * @brief
 *	Refresh/retrieve job from database and add it into AVL tree if not present
 *
 *	@param[in]  dbobj     - The pointer to the wrapper job object of type pbs_db_job_info_t
 * 	@param[out]  refreshed - To check if job is refreshed
 *
 * @return	The recovered job
 * @retval	NULL - Failure
 * @retval	!NULL - Success, pointer to job structure recovered
 *
 */
job *
recov_job_cb(pbs_db_obj_info_t *dbobj, int *refreshed)
{
	job *pj = NULL;
	pbs_db_job_info_t *dbjob = dbobj->pbs_db_un.pbs_db_job;
	static int numjobs = 0;

	*refreshed = 0;
	if ((pj = job_recov_db_spl(dbjob, NULL)) == NULL) {
		if ((server_init_type == RECOV_COLD) || (server_init_type == RECOV_CREATE)) {
			/* remove the loaded job from db */
			if (pbs_db_delete_obj(svr_db_conn, dbobj) != 0)
				log_errf(PBSE_SYSTEM, __func__, "job %s not purged", dbjob->ji_jobid);
		}
		goto err;
	}

	pbsd_init_job(pj, server_init_type);
	*refreshed = 1;

	if ((++numjobs % 20) == 0) {
		/* periodically touch the file so the  */
		/* world knows we are alive and active */
		update_svrlive();
	}

err:
	free_db_attr_list(&dbjob->db_attr_list);
	if (pj == NULL)
		log_errf(PBSE_SYSTEM, __func__, "Failed to recover job %s", dbjob->ji_jobid);
	return pj;
}

/**
 * @brief
 * 		recov_resv_cb - callback function to process and load
 * 					  resv database result to pbs structure.
 *
 * @param[in]	dbobj	- database resv structure to C.
 * @param[out]	refreshed - To check if reservation recovered
 *
 * @return	resv structure - on success
 * @return 	NULL - on failure
 */
resc_resv *
recov_resv_cb(pbs_db_obj_info_t *dbobj, int *refreshed)
{
	resc_resv *presv = NULL;
	pbs_db_resv_info_t *dbresv = dbobj->pbs_db_un.pbs_db_resv;
	int load_type = 0;

	*refreshed = 0;
	/* if resv is not in list, load the resv from database */
	if ((presv = resv_recov_db(dbresv->ri_resvid, NULL)) == NULL)
		goto err;

	pbsd_init_resv(presv, load_type);
	*refreshed = 1;
err:
	free_db_attr_list(&dbresv->db_attr_list);
	if (presv == NULL)
		log_errf(-1, __func__, "Failed to recover resv %s", dbresv->ri_resvid);
	return presv;
}
