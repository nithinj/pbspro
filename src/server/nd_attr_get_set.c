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

#include <pbs_config.h>   /* the master config generated by configure */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pbs_ifl.h"
#include "libpbs.h"
#include "attribute.h"
#include "pbs_nodes.h"
#include "pbs_error.h"
#include "base_obj.h"


/**
 * @brief	Getter function for node attribute of type string
 *
 * @param[in]	pnode - pointer to the node
 * @param[in]	attr_idx - index of the attribute to return
 *
 * @return	char *
 * @retval	string value of the attribute
 * @retval	NULL if pnode is NULL
 */
char *
get_ndattr_str(const pbs_node *pnode, int attr_idx)
{
	return __get_attr_str(pnode, attr_idx, OBJ_NODE);
}

/**
 * @brief	Getter function for node attribute of type long
 *
 * @param[in]	pnode - pointer to the node
 * @param[in]	attr_idx - index of the attribute to return
 *
 * @return	long
 * @retval	long value of the attribute
 * @retval	-1 if pnode is NULL
 */
long
get_ndattr_long(const pbs_node *pnode, int attr_idx)
{
	return __get_attr_long(pnode, attr_idx, OBJ_NODE);
}

/**
 * @brief	Getter function for node attribute's user_encoded value
 *
 * @param[in]	pnode - pointer to the node
 * @param[in]	attr_idx - index of the attribute to return
 *
 * @return	svrattrl *
 * @retval	user_encoded value of the attribute
 * @retval	NULL if pnode is NULL
 */
svrattrl *
get_ndattr_usr_encoded(const pbs_node *pnode, int attr_idx)
{
	return __get_attr_usr_encoded(pnode, attr_idx, OBJ_NODE);
}

/**
 * @brief	Getter function for node attribute's priv_encoded value
 *
 * @param[in]	pnode - pointer to the node
 * @param[in]	attr_idx - index of the attribute to return
 *
 * @return	svrattrl *
 * @retval	priv_encoded value of the attribute
 * @retval	NULL if pnode is NULL
 */
svrattrl *
get_ndattr_priv_encoded(const pbs_node *pnode, int attr_idx)
{
	return __get_attr_priv_encoded(pnode, attr_idx, OBJ_NODE);
}

int
get_ndattr_flag(const pbs_node *pnode, int attr_idx)
{
	return __get_attr_flag(pnode, attr_idx, OBJ_NODE);
}

/**
 * @brief	Generic node attribute setter (call if you want at_set() action functions to be called)
 *
 * @param[in]	pnode - pointer to node
 * @param[in]	attr_idx - attribute index to set
 * @param[in]	val - new val to set
 * @param[in]	rscn - new resource val to set, if applicable
 * @param[in]	op - batch_op operation, SET, INCR, DECR etc.
 *
 * @return	int
 * @retval	0 for success
 * @retval	1 for failure
 */
int
set_ndattr_generic(pbs_node *pnode, int attr_idx, char *val, char *rscn, enum batch_op op)
{
	return __set_attr_generic(pnode, attr_idx, val, rscn, op, OBJ_NODE);
}

/**
 * @brief	"fast" node attribute setter for string values
 *
 * @param[in]	pnode - pointer to node
 * @param[in]	attr_idx - attribute index to set
 * @param[in]	val - new val to set
 * @param[in]	rscn - new resource val to set, if applicable
 *
 * @return	int
 * @retval	0 for success
 * @retval	1 for failure
 */
int
set_ndattr_str_slim(pbs_node *pnode, int attr_idx, char *val, char *rscn)
{
	return __set_attr_str_light(pnode, attr_idx, val, rscn, OBJ_NODE);
}

/**
 * @brief	"fast" node attribute setter
 *
 * @param[in]	pnode - pointer to node
 * @param[in]	attr_idx - attribute index to set
 * @param[in]	val - new val to set
 * @param[in]	rscn - new resource val to set, if applicable
 *
 * @return	int
 * @retval	0 for success
 * @retval	1 for failure
 */
int
set_ndattr_light(pbs_node *pnode, int attr_idx, void *val, enum batch_op op)
{
	return __set_attr(pnode, attr_idx, val, op, OBJ_NODE);
}

void
reset_ndattr_flag(pbs_node *pnode, int attr_idx, int flag)
{
	return __reset_attr_flag(pnode, attr_idx, flag, OBJ_NODE);
}

void
set_ndattr_flag(pbs_node *pnode, int attr_idx, int flag)
{
	return __set_attr_flag(pnode, attr_idx, flag, OBJ_NODE);
}

void
unset_ndattr_flag(pbs_node *pnode, int attr_idx, int flag)
{
	return __unset_attr_flag(pnode, attr_idx, flag, OBJ_NODE);
}

int
is_ndattr_flag_set(const pbs_node *pnode, int attr_idx, int flag)
{
	return __is_attr_flag_set(pnode, attr_idx, flag, OBJ_NODE);
}

/**
 * @brief	Check if a node attribute is set
 *
 * @param[in]	pnode - pointer to node
 * @param[in]	attr_idx - attribute index to check
 *
 * @return	int
 * @retval	1 if it is set
 * @retval	0 otherwise
 */
int
is_ndattr_set(const pbs_node *pnode, int attr_idx)
{
	return __is_attr_flag_set(pnode, attr_idx, ATR_VFLAG_SET, OBJ_NODE);
}

/**
 * @brief	Mark a node attribute as "set"
 *
 * @param[in]	pnode - pointer to node
 * @param[in]	attr_idx - attribute index to set
 *
 * @return	void
 */
void
mark_ndattr_not_set(pbs_node *pnode, int attr_idx)
{
	__unset_attr_flag(pnode, attr_idx, ATR_VFLAG_SET, OBJ_NODE);
	__set_attr_flag(pnode, attr_idx, ATR_MOD_MCACHE, OBJ_NODE);
}

/**
 * @brief	Mark a node attribute as "not set"
 *
 * @param[in]	pnode - pointer to node
 * @param[in]	attr_idx - attribute index to set
 *
 * @return	void
 */
void
mark_ndattr_set(pbs_node *pnode, int attr_idx)
{
	__unset_attr_flag(pnode, attr_idx, ATR_VFLAG_SET, OBJ_NODE);
}

/**
 * @brief	Free a node attribute
 *
 * @param[in]	pnode - pointer to node
 * @param[in]	attr_idx - attribute index to free
 *
 * @return	void
 */
void
free_ndattr(pbs_node *pnode, int attr_idx)
{
	return __free_attr(pnode, attr_idx, OBJ_NODE);
}

attribute*
get_ndattr(pbs_node *pnode, int attr_idx)
{
	return &pnode->nd_attr[ND_ATR_ResourceAvail];
}
