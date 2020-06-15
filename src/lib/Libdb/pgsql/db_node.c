/*
 * Copyright (C) 1994-2020 Altair Engineering, Inc.
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
 * @file    db_node.c
 *
 * @brief
 *      Implementation of the node data access functions for postgres
 */

#include <pbs_config.h>   /* the master config generated by configure */
#include "pbs_db.h"
#include "db_postgres.h"

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
pbs_db_prepare_node_sqls(void *conn)
{
	char    conn_sql[MAX_SQL_LENGTH];
	char    select_sql[SELECT_SQL_LEN];

	snprintf(conn_sql, MAX_SQL_LENGTH, "insert into pbs.node("
		"nd_name, "
		"nd_index, "
		"mom_modtime, "
		"nd_hostname, "
		"nd_state, "
		"nd_ntype, "
		"nd_pque, "
		"nd_savetm, "
		"nd_creattm, "
		"attributes "
		") "
		"values "
		"($1, $2, $3, $4, $5, $6, $7, localtimestamp, localtimestamp, hstore($8::text[])) "
		"returning to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm");

	if (db_prepare_stmt(conn, STMT_INSERT_NODE, conn_sql, 8) != 0)
		return -1;

	/* in case of nodes do not use || with existing attributes, since we re-write all attributes */
	snprintf(conn_sql, MAX_SQL_LENGTH, "update pbs.node set "
		"nd_index = $2, "
		"mom_modtime = $3, "
		"nd_hostname = $4, "
		"nd_state = $5, "
		"nd_ntype = $6, "
		"nd_pque = $7, "
		"nd_savetm = localtimestamp, "
		"attributes = attributes || hstore($8::text[]) "
		"where nd_name = $1 "
		"returning to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm");
	if (db_prepare_stmt(conn, STMT_UPDATE_NODE, conn_sql, 8) != 0)
		return -1;

	snprintf(conn_sql, MAX_SQL_LENGTH, "update pbs.node set "
		"nd_index = $2, "
		"mom_modtime = $3, "
		"nd_hostname = $4, "
		"nd_state = $5, "
		"nd_ntype = $6, "
		"nd_pque = $7, "
		"nd_savetm = localtimestamp "
		"where nd_name = $1 "
		"returning to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm");
	if (db_prepare_stmt(conn, STMT_UPDATE_NODE_QUICK, conn_sql, 7) != 0)
		return -1;

	snprintf(conn_sql, MAX_SQL_LENGTH, "update pbs.node set "
		"nd_savetm = localtimestamp,"
		"attributes = attributes || hstore($2::text[]) "
		"where nd_name = $1 "
		"returning to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm");
	if (db_prepare_stmt(conn, STMT_UPDATE_NODE_ATTRSONLY, conn_sql, 2) != 0)
		return -1;

	snprintf(conn_sql, MAX_SQL_LENGTH, "update pbs.node set "
		"nd_savetm = localtimestamp,"
		"attributes = attributes - $2::text[] "
		"where nd_name = $1 "
		"returning to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm");
	if (db_prepare_stmt(conn, STMT_REMOVE_NODEATTRS, conn_sql, 2) != 0)
		return -1;

	snprintf(select_sql, MAX_SQL_LENGTH, "select "
		"nd_name, "
		"nd_index, "
		"mom_modtime, "
		"nd_hostname, "
		"nd_state, "
		"nd_ntype, "
		"nd_pque, "
		"to_char(nd_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as nd_savetm, "
		"hstore_to_array(attributes) as attributes "
		"from pbs.node");

	snprintf(conn_sql, MAX_SQL_LENGTH, "%s where nd_name = $1", select_sql);
	if (db_prepare_stmt(conn, STMT_SELECT_NODE, conn_sql, 1) != 0)
		return -1;

#ifdef NAS /* localmod 079 */
	snprintf(conn_sql, MAX_SQL_LENGTH, "%s n left outer join pbs.nas_node i on "
		"n.nd_name=i.nd_name order by i.nd_nasindex", select_sql);
#else
	snprintf(conn_sql, MAX_SQL_LENGTH, "%s order by nd_index, nd_creattm", select_sql);
#endif /* localmod 079 */
	if (db_prepare_stmt(conn, STMT_FIND_NODES_ORDBY_INDEX, conn_sql, 0) != 0)
		return -1;

	snprintf(conn_sql, MAX_SQL_LENGTH, "%s where nd_savetm > to_timestamp($1, 'YYYY-MM-DD HH24:MI:SS:US') "
		"order by nd_index, nd_creattm", select_sql);
	if (db_prepare_stmt(conn, STMT_FIND_NODES_ORDBY_INDEX_FILTERBY_SAVETM, conn_sql, 1) != 0)
		return -1;

	snprintf(conn_sql, MAX_SQL_LENGTH, "%s where nd_hostname = $1 order by nd_index, nd_creattm", select_sql);
	if (db_prepare_stmt(conn, STMT_FIND_NODES_ORDBY_INDEX_FILTERBY_HOSTNAME, conn_sql, 1) != 0)
		return -1;

	snprintf(conn_sql, MAX_SQL_LENGTH, "delete from pbs.node where nd_name = $1");
	if (db_prepare_stmt(conn, STMT_DELETE_NODE, conn_sql, 1) != 0)
		return -1;

	snprintf(conn_sql, MAX_SQL_LENGTH, "select "
		"mit_time, "
		"mit_gen "
		"from pbs.mominfo_time ");
	if (db_prepare_stmt(conn, STMT_SELECT_MOMINFO_TIME, conn_sql, 0) != 0)
		return -1;

	snprintf(conn_sql, MAX_SQL_LENGTH, "insert into pbs.mominfo_time("
		"mit_time, "
		"mit_gen) "
		"values "
		"($1, $2)");
	if (db_prepare_stmt(conn, STMT_INSERT_MOMINFO_TIME, conn_sql, 2) != 0)
		return -1;

	snprintf(conn_sql, MAX_SQL_LENGTH, "update pbs.mominfo_time set "
		"mit_time = $1, "
		"mit_gen = $2 ");
	if (db_prepare_stmt(conn, STMT_UPDATE_MOMINFO_TIME, conn_sql, 2) != 0)
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
 *
 */
static int
load_node(PGresult *res, pbs_db_node_info_t *pnd, int row)
{
	char		*raw_array;
	static int	nd_name_fnum, mom_modtime_fnum, nd_hostname_fnum, nd_state_fnum, nd_ntype_fnum,
			nd_pque_fnum, attributes_fnum, nd_svtime_fnum;
	char		db_savetm[DB_TIMESTAMP_LEN + 1];
	static int	fnums_inited = 0;

	if (fnums_inited == 0) {
		nd_name_fnum = PQfnumber(res, "nd_name");
		mom_modtime_fnum = PQfnumber(res, "mom_modtime");
		nd_hostname_fnum = PQfnumber(res, "nd_hostname");
		nd_state_fnum = PQfnumber(res, "nd_state");
		nd_ntype_fnum = PQfnumber(res, "nd_ntype");
		nd_pque_fnum = PQfnumber(res, "nd_pque");
		nd_svtime_fnum = PQfnumber(res, "nd_savetm");
		attributes_fnum = PQfnumber(res, "attributes");
		fnums_inited = 1;
	}

	GET_PARAM_STR(res, row, db_savetm,  nd_svtime_fnum);
	if (strcmp(pnd->nd_savetm, db_savetm) == 0)
		return -2;

	strcpy(pnd->nd_savetm, db_savetm);

	GET_PARAM_STR(res, row, pnd->nd_name, nd_name_fnum);
	GET_PARAM_BIGINT(res, row, pnd->mom_modtime, mom_modtime_fnum);
	GET_PARAM_STR(res, row, pnd->nd_hostname, nd_hostname_fnum);
	GET_PARAM_INTEGER(res, row, pnd->nd_state, nd_state_fnum);
	GET_PARAM_INTEGER(res, row, pnd->nd_ntype, nd_ntype_fnum);
	GET_PARAM_STR(res, row, pnd->nd_pque, nd_pque_fnum);
	GET_PARAM_BIN(res, row, raw_array, attributes_fnum);

	/* convert attributes from postgres raw array format */
	return (dbarray_2_attrlist(raw_array, &pnd->db_attr_list));
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
pbs_db_save_node(void *conn, pbs_db_obj_info_t *obj, int savetype)
{
	pbs_db_node_info_t *pnd = obj->pbs_db_un.pbs_db_node;
	char *stmt = NULL;
	int params;
	char *raw_array = NULL;
	static int nd_svtime_fnum;
	static int fnums_inited = 0;
	PGresult *res;

	SET_PARAM_STR(conn_data, pnd->nd_name, 0);
	
	if (savetype & OBJ_SAVE_QS) {
		SET_PARAM_INTEGER(conn_data, pnd->nd_index, 1);
		SET_PARAM_BIGINT(conn_data, pnd->mom_modtime, 2);
		SET_PARAM_STR(conn_data, pnd->nd_hostname, 3);
		SET_PARAM_INTEGER(conn_data, pnd->nd_state, 4);
		SET_PARAM_INTEGER(conn_data, pnd->nd_ntype, 5);
		SET_PARAM_STR(conn_data, pnd->nd_pque, 6);
		params = 7;
		stmt = STMT_UPDATE_NODE_QUICK;
	}

	/* are there attributes to save to memory or local cache? */
	if (pnd->cache_attr_list.attr_count > 0) {
		dist_cache_save_attrs(pnd->nd_name, &pnd->cache_attr_list);
	}

	if ((pnd->db_attr_list.attr_count > 0) || (savetype & OBJ_SAVE_NEW)) {
		int len = 0;
		/* convert attributes to postgres raw array format */
		if ((len = attrlist_2_dbarray(&raw_array, &pnd->db_attr_list)) <= 0)
			return -1;

		if (savetype & OBJ_SAVE_QS) {
			SET_PARAM_BIN(conn_data, raw_array, len, 7);
			params = 8;
			stmt = STMT_UPDATE_NODE;
		} else {
			SET_PARAM_BIN(conn_data, raw_array, len, 1);
			params = 2;
			stmt = STMT_UPDATE_NODE_ATTRSONLY;
		}
	}

	if (savetype & OBJ_SAVE_NEW)
		stmt = STMT_INSERT_NODE;

	if (stmt != NULL) {
		if (db_cmd(conn, stmt, params, &res) != 0) {
			free(raw_array);
			return -1;
		}
		if (fnums_inited == 0) {
			nd_svtime_fnum = PQfnumber(res, "nd_savetm");
			fnums_inited = 1;
		}
		GET_PARAM_STR(res, 0, pnd->nd_savetm, nd_svtime_fnum);
		PQclear(res);
		free(raw_array);
	}

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
 * @retval	 1 -  Success but no rows loaded
 *
 */
int
pbs_db_load_node(void *conn, pbs_db_obj_info_t *obj)
{
	PGresult *res;
	int rc;
	pbs_db_node_info_t *pnd = obj->pbs_db_un.pbs_db_node;

	SET_PARAM_STR(conn_data, pnd->nd_name, 0);

	if ((rc = db_query(conn, STMT_SELECT_NODE, 1, &res)) != 0)
		return rc;

	rc = load_node(res, pnd, 0);

	PQclear(res);

	if (rc == 0) {
		/* in case of multi-server, also read NOSAVM attributes from distributed cache */
		/* call in this functions since all call paths lead to this before decode */
		//if (use_dist_cache) {
		//	dist_cache_recov_attrs(pnd->nd_name, &pnd->nd_savetm, &pnd->cache_attr_list);
		//}
	}

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
pbs_db_find_node(void *conn, void *st, pbs_db_obj_info_t *obj,
	pbs_db_query_options_t *opts)
{
	PGresult         *res;
	int              rc;
	int              params;
	char             conn_sql[MAX_SQL_LENGTH];
	db_query_state_t *state = (db_query_state_t *) st;

	if (!state)
		return -1;
	
	if (opts && opts->flags == 1 && opts->hostname) {
		SET_PARAM_STR(conn_data, opts->hostname, 0);
		strcpy(conn_sql, STMT_FIND_NODES_ORDBY_INDEX_FILTERBY_HOSTNAME);
		params = 1;
	} else if (opts && opts->timestamp) {
		SET_PARAM_STR(conn_data, opts->timestamp, 0);
		strcpy(conn_sql, STMT_FIND_NODES_ORDBY_INDEX_FILTERBY_SAVETM);
		params = 1;
	} else {
		strcpy(conn_sql, STMT_FIND_NODES_ORDBY_INDEX);
		params = 0;
	}

	if ((rc = db_query(conn, conn_sql, params, &res)) != 0)
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
pbs_db_next_node(void *conn, void *st, pbs_db_obj_info_t *obj)
{
	obj->pbs_db_un.pbs_db_node->nd_savetm[0] = '\0';
	db_query_state_t *state = (db_query_state_t *) st;

	return (load_node(state->res, obj->pbs_db_un.pbs_db_node, state->row));
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
pbs_db_delete_node(void *conn, pbs_db_obj_info_t *obj)
{
	pbs_db_node_info_t *pnd = obj->pbs_db_un.pbs_db_node;
	SET_PARAM_STR(conn_data, pnd->nd_name, 0);
	return (db_cmd(conn, STMT_DELETE_NODE, 1, NULL));
}



/**
 * @brief
 *	Deletes attributes of a node
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj_id  - Node id
 * @param[in]	sv_time  - Node save time
 * @param[in]	attr_list - List of attributes
 *
 * @return      Error code
 * @retval	 0 - Success
 * @retval	-1 - On Failure
 *
 */
int
pbs_db_del_attr_node(void *conn, void *obj_id, char *sv_time, pbs_db_attr_list_t *attr_list)
{
	char *raw_array = NULL;
	int len = 0;
	static int nd_savetm_fnum;
	static int fnums_inited = 0;
	PGresult *res;

	if ((len = attrlist_2_dbarray_ex(&raw_array, attr_list, 1)) <= 0)
		return -1;

	SET_PARAM_STR(conn_data, obj_id, 0);
	SET_PARAM_BIN(conn_data, raw_array, len, 1);

	if (db_cmd(conn, STMT_REMOVE_NODEATTRS, 2, &res) == -1) {
		free(raw_array);
		return -1;
	}
	if (fnums_inited == 0) {
		nd_savetm_fnum = PQfnumber(res, "nd_savetm");
	}
	GET_PARAM_STR(res, 0, sv_time, nd_savetm_fnum);
	PQclear(res);
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
pbs_db_save_mominfo_tm(void *conn, pbs_db_obj_info_t *obj, int savetype)
{
	char *stmt;
	pbs_db_mominfo_time_t *pmi = obj->pbs_db_un.pbs_db_mominfo_tm;

	SET_PARAM_BIGINT(conn_data, pmi->mit_time, 0);
	SET_PARAM_INTEGER(conn_data, pmi->mit_gen, 1);

	if (savetype & OBJ_SAVE_NEW)
		stmt = STMT_INSERT_MOMINFO_TIME;
	else
		stmt = STMT_UPDATE_MOMINFO_TIME;

	if (db_cmd(conn, stmt, 2, NULL) == -1)
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
pbs_db_load_mominfo_tm(void *conn, pbs_db_obj_info_t *obj)
{
	PGresult *res;
	int rc;
	pbs_db_mominfo_time_t *pmi = obj->pbs_db_un.pbs_db_mominfo_tm;
	static int mit_time_fnum = -1;
	static int mit_gen_fnum = -1;

	if ((rc = db_query(conn, STMT_SELECT_MOMINFO_TIME, 0, &res)) != 0)
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
