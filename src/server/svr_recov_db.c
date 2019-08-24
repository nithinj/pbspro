/*
 * Copyright (C) 1994-2018 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * For a copy of the commercial license terms and conditions,
 * go to: (http://www.pbspro.com/UserArea/agreement.html)
 * or contact the Altair Legal Department.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */


/**
 * @file    svr_recov_db.c
 *
 * @brief
 * 		svr_recov_db.c - contains functions to save server state and recover
 *
 * Included functions are:
 *	svr_recov_db()
 *	svr_save_db()
 *	update_svrlive()
 *	svr_to_db_svr()
 *	db_to_svr_svr()
 *	svr_to_db_sched()
 *	db_to_svr_sched()
 *	sched_recov_db()
 *	sched_save_db()
 *
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "pbs_ifl.h"
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "job.h"
#include "reservation.h"
#include "queue.h"
#include "server.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "log.h"
#include "pbs_db.h"
#include "pbs_sched.h"
#include "pbs_share.h"

/* Global Data Items: */

extern struct server server;
extern pbs_list_head svr_queues;
extern attribute_def svr_attr_def[];
extern char	*path_priv;
extern time_t	time_now;
extern char	*msg_svdbopen;
extern char	*msg_svdbnosv;
extern char	*path_svrlive;

#ifndef PBS_MOM
extern char *pbs_server_name;
extern pbs_db_conn_t	*svr_db_conn;
#endif

#ifdef NAS /* localmod 005 */
/* External Functions Called */
extern int save_attr_db(pbs_db_conn_t *conn, pbs_db_attr_info_t *p_attr_info,
	struct attribute_def *padef, struct attribute *pattr,
	int numattr, int newparent);
extern int recov_attr_db(pbs_db_conn_t *conn,
	void *parent,
	pbs_db_attr_info_t *p_attr_info,
	struct attribute_def *padef,
	struct attribute *pattr,
	int limit,
	int unknown);
extern int utimes(const char *path, const struct timeval *times);
#endif /* localmod 005 */
extern pbs_sched *sched_alloc(char *sched_name, int append);

/**
 * @brief
 *		Update the $PBS_HOME/server_priv/svrlive file timestamp
 *
 * @return	Error code
 * @retval	0	: Success
 * @retval	-1	: Failed to update timestamp
 *
 */
int
update_svrlive()
{
	static int fdlive = -1;
	if (fdlive == -1) {
		/* first time open the file */
		fdlive = open(path_svrlive, O_WRONLY | O_CREAT, 0600);
		if (fdlive < 0)
			return -1;
#ifdef WIN32
		secure_file(path_svrlive, "Administrators",
			READS_MASK|WRITES_MASK|STANDARD_RIGHTS_REQUIRED);
#endif
	}
	(void)utimes(path_svrlive, NULL);
	return 0;
}

/**
 * @brief
 *	Load a database server object from a server object in the server
 *
 * @param[in]	ps	-	Address of the server in pbs server
 * @param[out]	pdbsvr	-	Address of the database server object
 * @param[in]   updatetype -    quick or full update
 *
 * @return   !=0   - Failure
 * @return   0     - Success
 *
 */
static int
svr_to_db_svr(struct server *ps, pbs_db_svr_info_t *pdbsvr, int updatetype)
{
	memset(pdbsvr, 0, sizeof(pbs_db_svr_info_t));

	if (updatetype != PBS_UPDATE_DB_QUICK) {
		if ((encode_attr_db(svr_attr_def,
			ps->sv_attr,
			(int)SRV_ATR_LAST, &pdbsvr->attr_list, 1)) != 0) /* encode all attributes */
			return -1;
	}

	return 0;
}

/**
 * @brief
 *	Load a server object in pbs_server from a database server object
 *
 * @param[out]	ps	-	Address of the server in pbs server
 * @param[in]	pdbsvr	-	Address of the database server object
 *
 * @return   !=0   - Failure
 * @return   0     - Success
 */
int
db_to_svr_svr(struct server *ps, pbs_db_svr_info_t *pdbsvr)
{
	strcpy(ps->sv_savetm, pdbsvr->sv_savetm);

	if ((decode_attr_db(ps, &pdbsvr->attr_list, svr_attr_def, ps->sv_attr, (int) SRV_ATR_LAST, 0)) != 0)
		return -1;

	return 0;
}

/**
 * @brief
 *	Load a scheduler object in pbs_server from a database scheduler object
 *
 * @param[in]	ps - Address of the scheduler in pbs server
 * @param[out] pdbsched  - Address of the database scheduler object
 * @param[in] updatetype - quick or full update
 *
 * @return   !=0   - Failure
 * @return   0     - Success
 */
static int
svr_to_db_sched(struct pbs_sched *ps, pbs_db_sched_info_t *pdbsched, int updatetype)
{
	pdbsched->sched_name[sizeof(pdbsched->sched_name) - 1] = '\0';
	strncpy(pdbsched->sched_name, ps->sc_name, sizeof(pdbsched->sched_name));

	if (updatetype != PBS_UPDATE_DB_QUICK) {
		if ((encode_attr_db(sched_attr_def,
			ps->sch_attr,
			(int)SCHED_ATR_LAST, &pdbsched->attr_list, 1)) != 0) /* encode all attributes */
			return -1;
	}

	return 0;
}

/**
 * @brief
 *		Recover server information and attributes from server database
 *
 * @par FunctionalitY:
 *		This function is only called on Server initialization at start up.
 *
 * @par	Note:
 *		server structure, extern struct server server, must be preallocated and
 *		all default values should already be set.
 *
 * @see	pbsd_init.c
 *
 * @return	Error code
 * @retval	0	: On successful recovery and creation of server structure
 * @retval	-1	: On failutre to open or read file.
 *
 */
int
svr_recov_db(int lock)
{
	pbs_db_conn_t *conn = (pbs_db_conn_t *) svr_db_conn;
	pbs_db_svr_info_t dbsvr;
	pbs_db_obj_info_t obj;
	int rc;

	/* load server_qs */
	dbsvr.attr_list.attr_count = 0;
	dbsvr.attr_list.attributes = NULL;

	obj.pbs_db_obj_type = PBS_DB_SVR;
	obj.pbs_db_un.pbs_db_svr = &dbsvr;

	if (!server.loaded) {
		dbsvr.sv_savetm[0] = '\0';
	} else {
		strcpy(dbsvr.sv_savetm, server.sv_savetm);
		if (memcache_good(&server.trx_status, lock))
			return 0;
	}

	/* read in job fixed sub-structure */
    rc = pbs_db_load_obj(conn, &obj, lock);

	if (rc == -1)
		goto db_err;

	if (rc == -2) {
		memcache_update_state(&server.trx_status, lock);
		return 0;
	}

	if (db_to_svr_svr(&server, &dbsvr) != 0)
		goto db_err;

	pbs_db_reset_obj(&obj);
	memcache_update_state(&server.trx_status, lock);

	server.loaded = 1;
	return (0);

db_err:
	sprintf(log_buffer, "Failed to load server object");
	return -1;
}

/**
 * @brief
 *		Save the state of the server, server quick save sub structure and
 *		optionally the attributes.
 *
 * @par Functionality:
 *		Saving is done in one of two modes:
 *		Quick - only the "quick save sub structure" is saved
 *		Full  - The quick save sub structure is saved, the set
 *		and non-default valued attributes are then saved by calling
 *		save_attr_db()
 *
 * @param[in]	ps   -	Pointer to struct server
 * @param[in]	mode -  type of save, either SVR_SAVE_QUICK or SVR_SAVE_FULL
 *
 * @return	Error code
 * @retval	 0	: Successful save of data.
 * @retval	-1	: Failure
 *
 */

int
svr_save_db(struct server *ps, int mode)
{
	pbs_db_conn_t *conn = (pbs_db_conn_t *) svr_db_conn;
	pbs_db_svr_info_t dbsvr;
	pbs_db_obj_info_t obj;
	int savetype = PBS_UPDATE_DB_FULL;
	int rc;

	/* as part of the server save, update svrlive file now,
	 * used in failover
	 */
	if (update_svrlive() !=0)
		return -1;

	if (mode == SVR_SAVE_FULL)
		savetype = PBS_UPDATE_DB_FULL;
	else
		savetype = PBS_INSERT_DB;

	if (svr_to_db_svr(ps, &dbsvr, savetype) != 0)
		goto db_err;

	obj.pbs_db_obj_type = PBS_DB_SVR;
	obj.pbs_db_un.pbs_db_svr = &dbsvr;

	rc = pbs_db_save_obj(conn, &obj, savetype);
	if (rc != 0) {
		savetype = PBS_INSERT_DB;
		rc = pbs_db_save_obj(conn, &obj, savetype);
	}
	strcpy(server.sv_savetm, dbsvr.sv_savetm);

	pbs_db_reset_obj(&obj);

	if (rc != 0)
		goto db_err;

	return (0);

db_err:
	strcpy(log_buffer, msg_svdbnosv);
	if (conn->conn_db_err != NULL)
		strncat(log_buffer, conn->conn_db_err, LOG_BUF_SIZE - strlen(log_buffer) - 1);
	log_err(-1, __func__, log_buffer);

	panic_stop_db(log_buffer);
	return (-1);
}




static char *schedemsg = "unable to save scheddb ";

/**
 * @brief
 *		Save the state of the scheduler structure which consists only of
 *		attributes.
 *
 * @par Functionality:
 *		Saving is done only in Full mode:
 *		Full  - The attributes which are set and non-default are saved by
 *		save_attr_db()
 *
 * @param[in]	ps   -	Pointer to struct sched
 * @param[in]	mode -  type of save, only SVR_SAVE_FULL
 *
 * @return	Error code
 * @retval	 0 :	Successful save of data.
 * @retval	-1 :	Failure
 *
 */

int
sched_save_db(pbs_sched *ps, int mode)
{
	pbs_db_conn_t *conn = (pbs_db_conn_t *) svr_db_conn;
	pbs_db_sched_info_t dbsched;
	pbs_db_obj_info_t obj;
	int savetype = PBS_UPDATE_DB_FULL;
	int rc;

	if (mode == SVR_SAVE_FULL)
		savetype = PBS_UPDATE_DB_FULL;
	else
		savetype = PBS_INSERT_DB;

	if (svr_to_db_sched(ps, &dbsched, savetype) != 0)
		goto db_err;

	obj.pbs_db_obj_type = PBS_DB_SCHED;
	obj.pbs_db_un.pbs_db_sched = &dbsched;

	rc = pbs_db_save_obj(conn, &obj, savetype);
	if (rc != 0) {
		savetype = PBS_INSERT_DB;
		rc = pbs_db_save_obj(conn, &obj, savetype);
	}

	strcpy(ps->sch_svtime, dbsched.sched_savetm);
	
	/* free the attribute list allocated by encode_attrs */
	pbs_db_reset_obj(&obj);

	if (rc != 0)
		goto db_err;

	return (0);

db_err:
	strcpy(log_buffer, schedemsg);
	if (conn->conn_db_err != NULL)
		strncat(log_buffer, conn->conn_db_err, LOG_BUF_SIZE - strlen(log_buffer) - 1);
	log_err(-1, __func__, log_buffer);

	panic_stop_db(log_buffer);
	return (-1);
}
