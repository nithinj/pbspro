/*
 * Copyright (C) 1994-2019 Altair Engineering, Inc.
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
 * @file	mom_info.c
 * @brief
 * 		mom_info.c - functions relating to the mominfo structures and vnodes
 *
 *		Some of the functions here in are used by both the Server and Mom,
 *		others are used by one or the other but not both.
 *
 * Included functions are:
 *
 * 	create_mom_entry()
 * 	delete_mom_entry()
 * 	find_mom_entry()
 * 	create_svrmom_entry()
 * 	delete_svrmom_entry()
 * 	create_mommap_entry()
 * 	delete_momvmap_entry()
 * 	find_vmap_entry()
 * 	add_mom_data()
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include "libpbs.h"
#include "list_link.h"
#include "attribute.h"
#include "resource.h"
#include "credential.h"
#include "server_limits.h"
#include "batch_request.h"
#include "server.h"
#include "pbs_nodes.h"
#include "pbs_error.h"
#include "log.h"
#include "svrfunc.h"
#include "rpp.h"
#include "pbs_internal.h"
#include "work_task.h"
#include "hook_func.h"

static char merr[] = "malloc failed";

/* Global Data Itmes */

/* mominfo_array is an array of mominfo_t pointers, one per host */

mominfo_t **mominfo_array = NULL;
int         mominfo_array_size = 0;     /* num entries in the array */
mominfo_time_t  mominfo_time = {0, 0};	/* time stamp of mominfo update */
int	    svr_num_moms = 0;

extern char	*msg_daemonname;
extern char	*path_hooks_rescdef;

extern int remove_mom_ipaddresses_list(mominfo_t *pmom);

/*
 * The following function are used by both the Server and Mom
 *	create_mom_entry()
 *	delete_mom_entry()
 *	find_mom_entry()
 */

#define GROW_MOMINFO_ARRAY_AMT 10

/**
 * @brief
 *		create_mom_entry - create both a mominfo_t entry and insert a pointer
 *		to that element into the mominfo_array which may be expanded if needed
 *
 * @par Functionality:
 *		Searches for existing mominfo_t entry with matching hostname and port;
 *		if found returns it, otherwise adds entry.   An empty slot in the
 *		mominfo_array[] will be used to hold pointer to created entry.  If no
 *		empty slot, the array is expanded by GROW_MOMINFO_ARRAY_AMT amount.
 *
 * @param[in]	hostname - hostname of host on which Mom will be running
 * @param[in]	port     - port number to which Mom will be listening
 *
 * @return	mominfo_t *
 * @retval	Returns pointer to the mominfo entry, existing or created
 * @retval	NULL on error.
 *
 * @par Side Effects: None
 *
 * @par MT-safe: no, would need lock on realloc of global mominfo_array[]
 *
 */

mominfo_t *
create_mom_entry(char *hostname, unsigned int port)
{
	int	    empty = -1;
	int         i;
	mominfo_t  *pmom;
	mominfo_t **tp;

	for (i=0; i < mominfo_array_size; ++i) {
		pmom = mominfo_array[i];
		if (pmom) {
			if ((strcasecmp(pmom->mi_host, hostname) == 0) &&
				(pmom->mi_port == port))
				return pmom;
		} else if (empty == -1) {
			empty = i;  /* save index of first empty slot */
		}
	}

	if (empty == -1) {
		/* there wasn't an empty slot in the array we can use */
		/* need to grow the array			      */

		tp = (mominfo_t **)realloc(mominfo_array,
			(size_t)(sizeof(mominfo_t *) * (mominfo_array_size+GROW_MOMINFO_ARRAY_AMT)));
		if (tp) {
			empty = mominfo_array_size;
			mominfo_array = tp;
			mominfo_array_size += GROW_MOMINFO_ARRAY_AMT;
			for (i = empty; i < mominfo_array_size; ++i)
				mominfo_array[i] = NULL;
		} else {
			log_err(errno, __func__, merr);
			return NULL;
		}
	}

	/* now allocate the memory for the mominfo_t element itself */

	pmom = (mominfo_t *)malloc(sizeof(mominfo_t));
	if (pmom) {
		(void)strncpy(pmom->mi_host, hostname, PBS_MAXHOSTNAME);
		pmom->mi_host[PBS_MAXHOSTNAME] = '\0';
		pmom->mi_port = port;
		pmom->mi_rmport = port + 1;
		pmom->mi_modtime = (time_t)0;
		pmom->mi_data    = NULL;
		pmom->mi_action = NULL;
		pmom->mi_num_action = 0;
#ifndef PBS_MOM
		if ((msg_daemonname == NULL) ||
			(strcmp(msg_daemonname, "PBS_send_hooks") != 0)) {
			/* No need to do this if executed by pbs_send_hooks */
			if (mom_hooks_seen_count() > 0) {
				struct stat sbuf;
				/* there should be at least one hook to */
				/* add mom actions below, which are in */
				/* behalf of existing hooks. */
				add_pending_mom_allhooks_action(pmom,
					MOM_HOOK_ACTION_SEND_ATTRS|MOM_HOOK_ACTION_SEND_CONFIG|MOM_HOOK_ACTION_SEND_SCRIPT);
				if (stat(path_hooks_rescdef, &sbuf) == 0)
					add_pending_mom_hook_action(pmom, PBS_RESCDEF,
						MOM_HOOK_ACTION_SEND_RESCDEF);
			}
		}
#endif

		mominfo_array[empty] = pmom;
		++svr_num_moms;			/* increment number of Moms */
	} else {
		log_err(errno, __func__, merr);
	}

	return pmom;
}


/**
 *
 * @brief
 *		Destory a mominfo_t element and null the pointer to that
 *		element in the mominfo_array;
 * @par Functionality:
 *		The heap entry pointed to by the mi_data member is freed also.
 *		However, any extra malloc-ed space in that member must be freed
 *		independently. Note, this means the mominfo_array may have null
 *		entries anywhere.
 *
 * @param[in]	pmom - the element being operated on.
 *
 * @return	void
 */

void
delete_mom_entry(mominfo_t *pmom)
{
	int i;

	if (pmom == NULL)
		return;

	/*
	 * Remove any work_task entries that may be referencing this mom
	 * BEFORE we free any data.
	 */
	delete_task_by_parm1((void *) pmom, DELETE_ONE);

	/* find the entry in the arry that does point here */
	for (i=0; i < mominfo_array_size; ++i) {
		if (mominfo_array[i] == pmom) {
			mominfo_array[i] = NULL;
			break;
		}
	}

	if (pmom->mi_action != NULL) {

#ifndef PBS_MOM
		for (i=0; i < pmom->mi_num_action; ++i) {
			if (pmom->mi_action[i] != NULL) {
				free(pmom->mi_action[i]);
				pmom->mi_action[i] = NULL;
			}
		}
#endif
		free(pmom->mi_action);
	}

	/* free the mi_data after all hook work is done, since the hook actions
	 * use the mi_data.
	 */
	if (pmom->mi_data)
		free(pmom->mi_data);

	memset(pmom, 0, sizeof(mominfo_t));
	free(pmom);
	--svr_num_moms;

	return;
}


/**
 * @brief
 * 		find_mom_entry - find and return a pointer to a mominfo_t element
 *		defined by the hostname and port
 * @note
 *		the mominfo_array may have null entries anywhere.
 *
 * @param[in]	hostname - hostname of host on which Mom will be running
 * @param[in]	port     - port number to which Mom will be listening
 *
 * @return	pointer to a mominfo_t element
 * @reval	NULL	- couldn't find.
 */

mominfo_t *
find_mom_entry(char *hostname, unsigned int port)
{
	int i;
	mominfo_t *pmom;

	for (i=0; i<mominfo_array_size; ++i) {
		pmom = mominfo_array[i];
		if (pmom &&
			(strcasecmp(pmom->mi_host, hostname) == 0) &&
			(pmom->mi_port == port))
			return pmom;
	}

	return NULL; 	/* didn't find it */
}


#ifndef PBS_MOM	/* Not Mom, i.e. the Server */

/*
 * The following functions are used by the Server only !
 */

/**
 * @brief
 * 		create_svrmom_entry - create both a mominfo entry and the mom_svrinfo
 *		entry associated with it.
 * @par Functionality:
 *		Finds an existing mominfo_t structure for the hostname/port tuple,
 *		create mominfo_t and associated mom_svrinfo_t structures; and array
 *		(size 1) of pointers to pbs nodes for the children vnodes.
 * @note
 * 		use delete_mom_entry() to delete both the mominfo and
 *		mom_svrinfo entries.
 * @see
 * 		create_pbs_node2
 *
 * @param[in]	hostname - hostname of host on which Mom will be running
 * @param[in]	port     - port number to which Mom will be listening
 * @param[in]	pul      - list of IP addresses of host; will be freed on error
 *			   				or saved in structure; caller must not free pul
 *
 * @return	mominfo_t *
 * @retval	pointer to the created mominfo entry	- success
 * @retval	NULL	- error.
 *
 * @par Side Effects: None
 *
 * @par MT-safe: see create_mom_entry() and tinsert2()
 *
 */

mominfo_t *
create_svrmom_entry(char *hostname, unsigned int port, unsigned long *pul)
{
	mominfo_t     *pmom;
	mom_svrinfo_t *psvrmom;
	extern struct tree  *ipaddrs;

	pmom = create_mom_entry(hostname, port);
	if (pmom == NULL) {
		free(pul);
		return pmom;
	}

	if (pmom->mi_data != NULL) {
		free(pul);
		return pmom;	/* already there */
	}

	psvrmom = (mom_svrinfo_t *)malloc(sizeof(mom_svrinfo_t));
	if (!psvrmom) {
		log_err(errno, "create_svrmom_entry", merr);
		delete_mom_entry(pmom);
		return NULL;
	}

	psvrmom->msr_state =INUSE_UNKNOWN | INUSE_DOWN;
	psvrmom->msr_pcpus = 0;
	psvrmom->msr_acpus = 0;
	psvrmom->msr_pmem  = 0;
	psvrmom->msr_numjobs = 0;
	psvrmom->msr_arch  = NULL;
	psvrmom->msr_pbs_ver  = NULL;
	psvrmom->msr_stream  = -1;
	CLEAR_HEAD(psvrmom->msr_deferred_cmds);
	psvrmom->msr_timedown = (time_t)0;
	psvrmom->msr_timeinit = (time_t)0;
	psvrmom->msr_wktask  = 0;
	psvrmom->msr_addrs   = pul;
	psvrmom->msr_jbinxsz = 0;
	psvrmom->msr_jobindx = NULL;
	psvrmom->msr_numvnds = 0;
	psvrmom->msr_numvslots = 1;
	psvrmom->msr_vnode_pool = 0;
	psvrmom->msr_children =
		(struct pbsnode **)calloc((size_t)(psvrmom->msr_numvslots),
		sizeof(struct pbsnode *));
	if (psvrmom->msr_children == NULL) {
		log_err(errno, "create_svrmom_entry", merr);
		free(psvrmom);
		delete_mom_entry(pmom);
		return NULL;
	}
	pmom->mi_data = psvrmom;	/* must be done before call tinsert2 */
	while (*pul) {
		tinsert2(*pul, port, pmom, &ipaddrs);
		pul++;
	}

	return pmom;
}


/**
 * @brief
 * 		delete_svrmom_entry - destroy a mom_svrinfo_t element and the parent
 *		mominfo_t element.  This special function is required because of
 *		the msr_addrs array hung off of the mom_svrinfo_t
 *
 * @see
 * 		effective_node_delete
 *
 * @param[in]	pmom	- pointer to mominfo structure
 *
 * @return	void
 */

void
delete_svrmom_entry(mominfo_t *pmom)
{
	mom_svrinfo_t *psvrmom = NULL;
	unsigned long *up;
	extern struct tree  *ipaddrs;

	if (pmom->mi_data) {

#ifndef PBS_MOM
		/* send request to this mom to delete all hooks known from this server. */
		/* we'll just send this delete request only once */
		/* if a hook fails to delete, then that mom host when it */
		/* come back will still have the hook. */
		if ((pmom->mi_action != NULL) && (mom_hooks_seen_count() > 0)) {
			/* there should be at least one hook to */
			/* add mom actions below, which are in behalf of */
			/* existing hooks. */
			(void)bg_delete_mom_hooks(pmom);
		}
#endif

		psvrmom = (mom_svrinfo_t *)pmom->mi_data;
		if (psvrmom->msr_arch)
			free(psvrmom->msr_arch);

		if (psvrmom->msr_pbs_ver)
			free(psvrmom->msr_pbs_ver);

		if (psvrmom->msr_addrs) {
			for (up = psvrmom->msr_addrs; *up; up++) {
				/* del Mom's IP addresses from tree  */
				tdelete2(*up, pmom->mi_port,  &ipaddrs);
			}
			free(psvrmom->msr_addrs);
			psvrmom->msr_addrs = NULL;
		}
		if (psvrmom->msr_children)
			free(psvrmom->msr_children);

		if (psvrmom->msr_jobindx) {
			free(psvrmom->msr_jobindx);
			psvrmom->msr_jbinxsz = 0;
			psvrmom->msr_jobindx = NULL;
		}

		/* take stream out of tree */
		(void)rpp_close(psvrmom->msr_stream);
		tdelete2((unsigned long)psvrmom->msr_stream , 0, &streams);

		if (remove_mom_ipaddresses_list(pmom) != 0) {
			snprintf(log_buffer, sizeof(log_buffer), "Could not remove IP address for mom %s:%d from cache",
					pmom->mi_host, pmom->mi_port);
			log_err(errno, __func__, log_buffer);
		}
	}
	memset((void *)psvrmom, 0, sizeof(mom_svrinfo_t));
	psvrmom->msr_stream = -1; /* always set to -1 when deleted */
	delete_mom_entry(pmom);
}

#else   /* PBS_MOM */

/*
 * The following functions are used by Mom only !
 */

/**
 * @brief
 * 		create_mommap_entry - create an entry to map a vnode to its parent Mom
 *		and initialize it.   If the actual host of the vnode, used only for
 *		MPI is not the same as the Mom host, then set it.  If the two hosts
 *		are the same, then mvm_hostn is null and the Mom name should be used
 *
 * @param[in]	vnode	- vnode for which entry needs to be made
 * @param[in]	hostn	- host name for MPI via PBS_NODEFILE
 * @param[in]	pmom	- pointer to mominfo structure
 * @param[in]	notask	- mvm_notask
 *
 * @return	momvmap_t
 * @retval	NULL	- failure
 */
momvmap_t *
create_mommap_entry(char *vnode, char *hostn, mominfo_t *pmom, int notask)
{
	int         empty = -1;
	int	    i;
	momvmap_t  *pmmape;
	momvmap_t **tpa;

#ifdef DEBUG
	assert((vnode != NULL) && (*vnode != '\0') && (pmom != NULL));
#else
	if ((vnode == NULL) || (*vnode == '\0') || (pmom == NULL)) {
		return NULL;
	}
#endif

	/* find a empty slot in the map array */

	for (i=0; i<mommap_array_size; ++i) {
		if (mommap_array[i] == NULL) {
			empty = i;
			break;
		}
	}
	if (empty == -1) {	/* need to expand array */
		tpa = (momvmap_t **)realloc(mommap_array, (size_t)(sizeof(momvmap_t *) * (mommap_array_size + GROW_MOMINFO_ARRAY_AMT)));
		if (tpa) {
			empty = mommap_array_size;
			mommap_array = tpa;
			mommap_array_size += GROW_MOMINFO_ARRAY_AMT;
			for (i=empty; i<mommap_array_size; ++i)
				mommap_array[i] = NULL;
		} else {
			log_err(errno, __func__, merr);
			return NULL;
		}
	}

	/* now allocate the entry itself and initalize it */

	pmmape = malloc(sizeof(momvmap_t));
	if (pmmape) {
		(void)strncpy(pmmape->mvm_name, vnode, PBS_MAXNODENAME);
		pmmape->mvm_name[PBS_MAXNODENAME] ='\0';
		if ((hostn == NULL) || (*hostn == '\0')) {
			pmmape->mvm_hostn = NULL;
		} else {
			pmmape->mvm_hostn = strdup(hostn);
			if (pmmape->mvm_hostn == NULL) {
				log_err(errno, __func__, merr);
			}
		}
		pmmape->mvm_notask = notask;
		pmmape->mvm_mom = pmom;

		mommap_array[empty] = pmmape;
	} else {
		log_err(errno, __func__, merr);
	}
	return (pmmape);
}

/**
 * @brief
 *		delete_momvmap_entry - delete a momvmap_t entry
 * @see
 * 		free_vnodemap
 * @param[in,out]	- a momvmap_t entry
 *
 * @return	void
 */
void
delete_momvmap_entry(momvmap_t *pmmape)
{
	if (pmmape->mvm_hostn)
		free(pmmape->mvm_hostn);
	memset(pmmape, 0, sizeof(momvmap_t));
	free(pmmape);
}

/**
 * @brief
 * 		find_vmap_entry - find the momvmap_t entry for a vnode name
 *
 * @param[in]	vname	- vnode name
 *
 * @return	momvmap_t *
 * @retval	mom_vmap entry	- success
 * @retval	NULL	- failure
 */

momvmap_t *
find_vmap_entry(const char *vname)
{
	int          i;
	momvmap_t  *pmap;

	for (i=0; i < mommap_array_size;++i) {
		pmap = mommap_array[i];
		if ((pmap != NULL) && (strcasecmp(pmap->mvm_name, vname) == 0))
			return pmap;
	}
	return NULL;
}


struct mominfo *find_mom_by_vnodename(const char *vname)
{
	momvmap_t	*pmap;

	pmap = find_vmap_entry(vname);
	if (pmap)
		return (pmap->mvm_mom);
	else
		return NULL;
}

mominfo_t *
add_mom_data(const char *vnid, void *data)
{
	mominfo_t	*pmom;

	if ((pmom = find_mom_by_vnodename(vnid)) != NULL) {
		pmom->mi_data = data;
		return (pmom);
	}

	return NULL;
}
#endif  /* PBS_MOM */

/**
 * @brief
 *		Reading PBS key from server_priv.
 *
 * @return	PBS Key
 * @retval	NULL	- Failure
 *
 */
char *
read_pbs_key()
{
	char key_file[MAXPATHLEN + 1];
	FILE *fp;

#ifndef PBS_MOM
	sprintf(key_file, "%s/server_priv/pbs.key", pbs_conf.pbs_home_path);
#else
	sprintf(key_file, "%s/mom_priv/pbs.key", pbs_conf.pbs_home_path);
#endif
	if (access( key_file, F_OK ) == -1 ) {
		sprintf(log_buffer, "file: [%s] does not exist!", key_file);
		log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
				LOG_ALERT, msg_daemonname, log_buffer);
		return NULL;
	}

	if ((fp = fopen(key_file, "r")) != NULL) {
		char *buf = NULL;
		char *ret = NULL;
		int len = 0;
		ret = pbs_fgets(&buf, &len, fp);
		if (ret == NULL) {
			sprintf(log_buffer, "EOF while reading: [%s]",
				key_file);
			log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
					LOG_ALERT, msg_daemonname, log_buffer);
			return NULL;
		}
		return ret;
	} else {
		sprintf(log_buffer, "fopen failed for: [%s], errno: [%d]",
				key_file, errno);
		log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
				LOG_ALERT, msg_daemonname, log_buffer);
	}
	return NULL;
}
