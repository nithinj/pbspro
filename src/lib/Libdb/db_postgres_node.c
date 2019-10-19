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
 * @file    db_postgres_node.c
 *
 * @brief
 *      Implementation of the node data access functions for postgres
 */

#include <pbs_config.h>   /* the master config generated by configure */
#include "pbs_db.h"
#include "db_postgres.h"
#include "log.h"

/**
 * @brief
 *	Prepare all the node related sqls. Typically called after connect
 *	and before any other sql exeuction
 *
 * @param[in]	conn - Database connection handle
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 *
 */
int
pg_db_prepare_node_sqls(pbs_db_conn_t *conn)
{
	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "insert into pbs.node("
		"nd_name, "
		"nd_index, "
		"mom_modtime, "
		"nd_hostname, "
		"nd_state, "
		"nd_ntype, "
		"nd_pque, "
		"nd_deleted, "
		"nd_savetm, "
		"nd_creattm, "
		"attributes "
		") "
		"values "
		"($1, $2, $3, $4, $5, $6, $7, $8, localtimestamp, localtimestamp, hstore($9::text[])) "
		"returning to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm");

	if (pg_prepare_stmt(conn, STMT_INSERT_NODE, conn->conn_sql, 9) != 0)
		return -1;

	/* in case of nodes do not use || with existing attributes, since we re-write all attributes */
	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "update pbs.node set "
		"nd_index = $2, "
		"mom_modtime = $3, "
		"nd_hostname = $4, "
		"nd_state = $5, "
		"nd_ntype = $6, "
		"nd_pque = $7, "
		"nd_deleted = $8, "
		"nd_savetm = localtimestamp, "
		"attributes = hstore($9::text[]) "
		" where nd_name = $1 "
		"returning to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm");
	if (pg_prepare_stmt(conn, STMT_UPDATE_NODE, conn->conn_sql, 8) != 0)
		return -1;

	/* update a nd_deleted attribute only */
	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "update pbs.node set "
			"nd_deleted = $2, "
			"nd_savetm = localtimestamp "
			"where nd_name = $1 "
			"returning to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm");
	if (pg_prepare_stmt(conn, STMT_UPDATE_NODE_AS_DELETED, conn->conn_sql, 2) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "update pbs.node set "
		"nd_savetm = localtimestamp,"
		"attributes = delete(attributes, $2::text[]) "
		"where nd_name = $1 "
		"returning to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm");
	if (pg_prepare_stmt(conn, STMT_REMOVE_NODEATTRS, conn->conn_sql, 2) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "update pbs.node set "
			"nd_savetm = localtimestamp,"
			"attributes = attributes || hstore($2::text[]) "
			"where nd_name = $1 "
			"returning to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm");
	if (pg_prepare_stmt(conn, STMT_UPDATE_NODEATTRS, conn->conn_sql, 2) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "select "
		"nd_name, "
		"mom_modtime, "
		"nd_hostname, "
		"nd_state, "
		"nd_ntype, "
		"nd_pque, "
		"nd_deleted, "
		"to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm, "
		"to_char(nd_creattm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_creattm, "
		"hstore_to_array(attributes) as attributes "
		"from pbs.node "
		"where nd_name = $1 ");
	if (pg_prepare_stmt(conn, STMT_SELECT_NODE, conn->conn_sql, 1) != 0)
		return -1;

	strcat(conn->conn_sql, " FOR UPDATE");
	if (pg_prepare_stmt(conn, STMT_SELECT_NODE_LOCKED, conn->conn_sql, 1) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "select "
		"nd_name, "
		"nd_index, "
		"mom_modtime, "
		"nd_hostname, "
		"nd_state, "
		"nd_ntype, "
		"nd_pque, "
		"nd_deleted, "
		"to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm, "
		"to_char(nd_creattm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_creattm, "
		"hstore_to_array(attributes) as attributes "
		"from pbs.node order by nd_creattm");
	if (pg_prepare_stmt(conn, STMT_FIND_NODES_ORDBY_CREATTM, conn->conn_sql, 0) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "select "
#ifdef NAS /* localmod 079 */
		"n.nd_name, "
		"n.mom_modtime, "
		"n.nd_hostname, "
		"n.nd_state, "
		"n.nd_ntype, "
		"n.nd_pque "
		"from pbs.node n left outer join pbs.nas_node i on "
		"n.nd_name=i.nd_name order by i.nd_nasindex");
#else
		"nd_name, "
		"mom_modtime, "
		"nd_hostname, "
		"nd_state, "
		"nd_ntype, "
		"nd_pque, "
		"nd_deleted, "
		"to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm, "
		"to_char(nd_creattm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_creattm, "
		"hstore_to_array(attributes) as attributes "
		"from pbs.node "
		"order by nd_index, nd_creattm");
#endif /* localmod 079 */
	if (pg_prepare_stmt(conn, STMT_FIND_NODES_ORDBY_INDEX, conn->conn_sql, 0) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "select "
		"nd_name, "
		"mom_modtime, "
		"nd_hostname, "
		"nd_state, "
		"nd_ntype, "
		"nd_pque, "
		"nd_deleted, "
		"to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm, "
		"to_char(nd_creattm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_creattm, "
		"hstore_to_array(attributes) as attributes "
		"from pbs.node "
		"where nd_savetm > to_timestamp($1, 'YYYY-MM-DD HH24:MI:SS:US') "
		"order by nd_index, nd_creattm");
	if (pg_prepare_stmt(conn, STMT_FIND_NODES_ORDBY_INDEX_FILTERBY_SAVETM, conn->conn_sql, 1) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "delete from pbs.node where nd_name = $1");
	if (pg_prepare_stmt(conn, STMT_DELETE_NODE, conn->conn_sql, 1) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "select "
		"mit_time, "
		"mit_gen "
		"from pbs.mominfo_time ");
	if (pg_prepare_stmt(conn, STMT_SELECT_MOMINFO_TIME, conn->conn_sql, 0) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "insert into pbs.mominfo_time("
		"mit_time, "
		"mit_gen) "
		"values "
		"($1, $2)");
	if (pg_prepare_stmt(conn, STMT_INSERT_MOMINFO_TIME, conn->conn_sql, 2) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "update pbs.mominfo_time set "
		"mit_time = $1, "
		"mit_gen = $2 ");
	if (pg_prepare_stmt(conn, STMT_UPDATE_MOMINFO_TIME, conn->conn_sql, 2) != 0)
		return -1;

	return 0;
}

/**
 * @brief
 *	Load node data from the row into the node object
 *
 * @param[in]	res - Resultset from a earlier query
 * @param[in]	pnd  - Node object to load data into
 * @param[in]	row - The current row to load within the resultset
 *
 * @return      Error code
 * @retval	-1 - On Error
 * @retval	 0 - On Success
 * @retval	>1 - Number of attributes
 * @retval 	-2 -  Success but data same as old, so not loading data (but locking if lock requested)
 *
 */
static int
load_node(PGresult *res, pbs_db_node_info_t *pnd, int row)
{
	char *raw_array;
	char db_savetm[DB_TIMESTAMP_LEN + 1];
	static int nd_name_fnum, mom_modtime_fnum, nd_hostname_fnum, nd_state_fnum, nd_ntype_fnum,
	nd_pque_fnum, nd_deleted_fnum, nd_svtime_fnum, nd_creattm_fnum, attributes_fnum;
	static int fnums_inited = 0;

	DBPRT(("Loading node from database"))

	if (fnums_inited == 0) {
		nd_name_fnum = PQfnumber(res, "nd_name");
		mom_modtime_fnum = PQfnumber(res, "mom_modtime");
		nd_hostname_fnum = PQfnumber(res, "nd_hostname");
		nd_state_fnum = PQfnumber(res, "nd_state");
		nd_ntype_fnum = PQfnumber(res, "nd_ntype");
		nd_pque_fnum = PQfnumber(res, "nd_pque");
		nd_deleted_fnum = PQfnumber(res,  "nd_deleted");
		nd_svtime_fnum = PQfnumber(res, "nd_savetm");
		nd_creattm_fnum = PQfnumber(res, "nd_creattm");
		attributes_fnum = PQfnumber(res, "attributes");
		fnums_inited = 1;
	}

	GET_PARAM_STR(res, row, db_savetm,  nd_svtime_fnum);
	if (strcmp(pnd->nd_savetm, db_savetm) == 0) {
		DBPRT(("data same as read last time"))
		/* data same as read last time, so no need to read any further, return success from here */
		/* however since we loaded data from the database, the row is locked if a lock was requested */
		return -2;
	}
	strcpy(pnd->nd_savetm, db_savetm);  /* update the save timestamp */

	GET_PARAM_STR(res, row, pnd->nd_name, nd_name_fnum);
	GET_PARAM_BIGINT(res, row, pnd->mom_modtime, mom_modtime_fnum);
	GET_PARAM_STR(res, row, pnd->nd_hostname, nd_hostname_fnum);
	GET_PARAM_INTEGER(res, row, pnd->nd_state, nd_state_fnum);
	GET_PARAM_INTEGER(res, row, pnd->nd_ntype, nd_ntype_fnum);
	GET_PARAM_STR(res, row, pnd->nd_pque, nd_pque_fnum);
	GET_PARAM_INTEGER(res, row, pnd->nd_deleted, nd_deleted_fnum);
	GET_PARAM_STR(res, row, pnd->nd_creattm, nd_creattm_fnum);
	GET_PARAM_BIN(res, row, raw_array, attributes_fnum);

	/* convert attributes from postgres raw array format */
	return (convert_array_to_db_attr_list(raw_array, &pnd->attr_list));
}

/**
 * @brief
 *	Insert node data into the database
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - Information of node to be inserted
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 *
 */
int
pg_db_save_node(pbs_db_conn_t *conn, pbs_db_obj_info_t *obj, int savetype)
{
	pbs_db_node_info_t *pnd = obj->pbs_db_un.pbs_db_node;
	char *stmt;
	int params;
	char *raw_array = NULL;
	static int nd_svtime_fnum;
	static int fnums_inited = 0;

	SET_PARAM_STR(conn, pnd->nd_name, 0);
	if (savetype == PBS_UPDATE_DB_AS_DELETED) {
		SET_PARAM_INTEGER(conn, pnd->nd_deleted, 1);
		params = 2;
	} else {
		SET_PARAM_INTEGER(conn, pnd->nd_index, 1);
		SET_PARAM_BIGINT(conn, pnd->mom_modtime, 2);
		SET_PARAM_STR(conn, pnd->nd_hostname, 3);
		SET_PARAM_INTEGER(conn, pnd->nd_state, 4);
		SET_PARAM_INTEGER(conn, pnd->nd_ntype, 5);
		SET_PARAM_STR(conn, pnd->nd_pque, 6);
		SET_PARAM_INTEGER(conn, pnd->nd_deleted, 7);
		params = 8;
	}

	if (savetype == PBS_UPDATE_DB_FULL || savetype == PBS_INSERT_DB) {
		int len = 0;
		/* convert attributes to postgres raw array format */
		if ((len = convert_db_attr_list_to_array(&raw_array, &pnd->attr_list)) <= 0)
			return -1;

		SET_PARAM_BIN(conn, raw_array, len, 8);
		params = 9;
	}

	if (savetype == PBS_UPDATE_DB_AS_DELETED)
		stmt = STMT_UPDATE_NODE_AS_DELETED;
	else if (savetype == PBS_UPDATE_DB_FULL)
		stmt = STMT_UPDATE_NODE;
	else
		stmt = STMT_INSERT_NODE;

	if (pg_db_cmd_ret(conn, stmt, params) != 0) {
		free(raw_array);
		return -1;
	}
	
	if (fnums_inited == 0) {
		nd_svtime_fnum = PQfnumber(conn->conn_resultset, "nd_savetm");
		fnums_inited = 1;
	}
	GET_PARAM_STR(conn->conn_resultset, 0, pnd->nd_savetm, nd_svtime_fnum);
	PQclear(conn->conn_resultset);

	free(raw_array);

	return 0;
}

/**
 * @brief
 *	Load node data from the database
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - Load node information into this object
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 * @retval	>1 - Number of attributes
 * @retval 	-2 -  Success but data same as old, so not loading data (but locking if lock requested)
 *
 */
int
pg_db_load_node(pbs_db_conn_t *conn, pbs_db_obj_info_t *obj, int lock)
{
	PGresult *res;
	int rc;
	pbs_db_node_info_t *pnd = obj->pbs_db_un.pbs_db_node;

	SET_PARAM_STR(conn, pnd->nd_name, 0);

	if ((rc = pg_db_query(conn, STMT_SELECT_NODE, 1, lock, &res)) != 0)
		return -1;

	rc = load_node(res, pnd, 0);

	PQclear(res);
	return rc;
}

/**
 * @brief
 *	Find nodes
 *
 * @param[in]	conn - Connection handle
 * @param[in]	st   - The cursor state variable updated by this query
 * @param[in]	obj  - Information of node to be found
 * @param[in]	opts - Any other options (like flags, timestamp)
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 * @retval	 1 - Success, but no rows found
 *
 */
int
pg_db_find_node(pbs_db_conn_t *conn, void *st, pbs_db_obj_info_t *obj,
	pbs_db_query_options_t *opts)
{
	PGresult *res;
	int rc;
	pg_query_state_t *state = (pg_query_state_t *) st;
	int params;

	if (!state)
		return -1;

	if (opts != NULL && opts->timestamp) {
		SET_PARAM_STR(conn, opts->timestamp, 0);
		params = 1;
		strcpy(conn->conn_sql, STMT_FIND_NODES_ORDBY_INDEX_FILTERBY_SAVETM);
	} else {
		strcpy(conn->conn_sql, STMT_FIND_NODES_ORDBY_INDEX);
		params = 0;
	}

	if ((rc = pg_db_query(conn, conn->conn_sql, params, 0, &res)) != 0)
		return rc;

	state->row = 0;
	state->res = res;
	state->count = PQntuples(res);
	return 0;
}

/**
 * @brief
 *	Get the next node from the cursor
 *
 * @param[in]	conn - Connection handle
 * @param[in]	st   - The cursor state
 * @param[in]	obj  - Node information is loaded into this object
 *
 * @return      Error code
 *		(Even though this returns only 0 now, keeping it as int
 *			to support future change to return a failure)
 * @retval	 0 - Success
 *
 */
int
pg_db_next_node(pbs_db_conn_t *conn, void *st, pbs_db_obj_info_t *obj)
{
	PGresult *res = ((pg_query_state_t *) st)->res;
	pg_query_state_t *state = (pg_query_state_t *) st;

	return (load_node(res, obj->pbs_db_un.pbs_db_node, state->row));
}

/**
 * @brief
 *	Delete the node from the database
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - Node information
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 * @retval	 1 - Success but no rows deleted
 *
 */
int
pg_db_delete_node(pbs_db_conn_t *conn, pbs_db_obj_info_t *obj)
{
	pbs_db_node_info_t *pnd = obj->pbs_db_un.pbs_db_node;
	SET_PARAM_STR(conn, pnd->nd_name, 0);
	return (pg_db_cmd(conn, STMT_DELETE_NODE, 1));
}



/**
 * @brief
 *	Deletes attributes of a node
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - Node information
 * @param[in]	obj_id  - Node id
 * @param[in]	attr_list - List of attributes
 *
 * @return      Error code
 * @retval	 0 - Success
 * @retval	-1 - On Failure
 *
 */
int
pg_db_del_attr_node(pbs_db_conn_t *conn, pbs_db_obj_info_t *obj, void *obj_id, pbs_db_attr_list_t *attr_list)
{
	char *raw_array = NULL;
	int len = 0;
	static int nd_savetm_fnum;
	static int fnums_inited = 0;
	pbs_db_node_info_t *pnd = obj->pbs_db_un.pbs_db_node;

	if ((len = convert_db_attr_list_to_array(&raw_array, attr_list)) <= 0)
		return -1;

	SET_PARAM_STR(conn, obj_id, 0);
	SET_PARAM_BIN(conn, raw_array, len, 1);

	if (pg_db_cmd_ret(conn, STMT_REMOVE_NODEATTRS, 2) != 0) {
		free(raw_array);
		return -1;
	}

	if (fnums_inited == 0) {
		nd_savetm_fnum = PQfnumber(conn->conn_resultset, "nd_savetm");
	}
	GET_PARAM_STR(conn->conn_resultset, 0, pnd->nd_savetm, nd_savetm_fnum);
	PQclear(conn->conn_resultset);

	free(raw_array);

	return 0;
}


/**
 * @brief
 *	Insert mominfo_time into the database
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - Information of node to be inserted
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 *
 */
int
pg_db_save_mominfo_tm(pbs_db_conn_t *conn, pbs_db_obj_info_t *obj, int savetype)
{
	char *stmt;
	pbs_db_mominfo_time_t *pmi = obj->pbs_db_un.pbs_db_mominfo_tm;

	SET_PARAM_BIGINT(conn, pmi->mit_time, 0);
	SET_PARAM_INTEGER(conn, pmi->mit_gen, 1);

	if (savetype == PBS_INSERT_DB)
		stmt = STMT_INSERT_MOMINFO_TIME;
	else
		stmt = STMT_UPDATE_MOMINFO_TIME;

	if (pg_db_cmd(conn, stmt, 2) != 0)
		return -1;

	return 0;
}

/**
 * @brief
 *	Load node mominfo_time from the database
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - Load node information into this object
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 * @retval	 1 -  Success but no rows loaded
 *
 */
int
pg_db_load_mominfo_tm(pbs_db_conn_t *conn, pbs_db_obj_info_t *obj, int lock)
{
	PGresult *res;
	int rc;
	pbs_db_mominfo_time_t *pmi = obj->pbs_db_un.pbs_db_mominfo_tm;
	static int mit_time_fnum = -1;
	static int mit_gen_fnum = -1;

	if ((rc = pg_db_query(conn, STMT_SELECT_MOMINFO_TIME, 0, lock, &res)) != 0)
		return rc;

	if (mit_time_fnum == -1 || mit_gen_fnum == -1) {
		mit_time_fnum = PQfnumber(res, "mit_time");
		mit_gen_fnum = PQfnumber(res, "mit_gen");
	}

	GET_PARAM_BIGINT(res, 0, pmi->mit_time, mit_time_fnum);
	GET_PARAM_INTEGER(res, 0, pmi->mit_gen, mit_gen_fnum);

	PQclear(res);
	return 0;
}

/**
 * @brief
 *	Frees allocate memory of an Object
 *
 * @param[in]	obj - pbs_db_obj_info_t containing the DB object
 *
 * @return None
 *
 */
void
pg_db_reset_node(pbs_db_obj_info_t *obj)
{
	free_db_attr_list(&(obj->pbs_db_un.pbs_db_node->attr_list));
	obj->pbs_db_un.pbs_db_node->nd_name[0] = '\0';
	obj->pbs_db_un.pbs_db_node->nd_savetm[0] = '\0';
}


/**
 * @brief
 *	Frees allocated memory of an Object.
 *	As of today there is no attributes column for the table mominfo_time. Even though we
 *	don't have this column we should have the following function as a placeholder since it is
 *	invoked as a callback from the generic array db_fn_arr.
 *
 * @param[in]	obj - pbs_db_obj_info_t containing the DB object
 *
 * @return None
 *
 */
void
pg_db_reset_mominfo(pbs_db_obj_info_t *obj)
{
	obj->pbs_db_un.pbs_db_node->nd_name[0] = '\0';
	obj->pbs_db_un.pbs_db_node->nd_savetm[0] = '\0';
	return ;
}

/**
 * @brief
 *	Add or update attributes of a node
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - Node information
 * @param[in]	obj_id  - Node name
 * @param[in]	attr_list - List of attributes
 *
 * @return      Error code
 * @retval	-1 - Execution of prepared statement failed
 * @retval	 0 - Success and > 0 rows were affected
 * @retval	 1 - Execution succeeded but statement did not affect any rows
 *
 */
int
pg_db_add_update_attr_node(pbs_db_conn_t *conn, pbs_db_obj_info_t *obj, void *obj_id, pbs_db_attr_list_t *attr_list)
{
	char *raw_array = NULL;
	int len = 0;

	if ((len = convert_db_attr_list_to_array(&raw_array, attr_list)) <= 0)
		return -1;
	SET_PARAM_STR(conn, obj_id, 0);

	SET_PARAM_BIN(conn, raw_array, len, 1);

	if (pg_db_cmd(conn, STMT_UPDATE_NODEATTRS, 2) != 0) {
		free(raw_array);
		return -1;
	}

	free(raw_array);

	return 0;
}
