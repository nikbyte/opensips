/*
 * Copyright (C) 2005 Juha Heinanen
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 *
 * History:
 * -------
 *  2005-11-29 splitted from lcr module (bogdan)
 */

/*!
 * \file
 * \brief Sequential forking implementation
 */
#define _ISOC11_SOURCE
#define _DEFAULT_SOURCE

#include <assert.h>

#include "str.h"
#include "qvalue.h"
#include "usr_avp.h"
#include "dset.h"
#include "action.h"
#include "route.h"
#include "parser/msg_parser.h"
#include "parser/parse_rr.h"
#include "mem/mem.h"


struct serial_contact {
	str enc_info;
	qvalue_t q;
	unsigned short q_flag;
	int next;
};

#define Q_FLAG            (1<<4)		/*!< usr_avp flag for sequential forking */
#define SERIAL_AVP_ALIAS  "serial_branch"	/*!< avp alias to be used */
#define SERIAL_AVL_ID     0xff3434		/*!< avp ID of serial AVP */

static int serial_avp;



int init_serialization(void)
{
	str alias = { SERIAL_AVP_ALIAS, sizeof(SERIAL_AVP_ALIAS)-1 };

	if (parse_avp_spec(&alias, &serial_avp)) {
		LM_ERR("cannot parse avp spec\n");
		return -1;
	}
	return 0;
}

#define seras(p, var, sertype) { \
	sertype _tval = 0; \
	static_assert(sizeof(var) <= sizeof(_tval), "variable " #var " is too big for " #sertype); \
	memcpy(&_tval, (var), sizeof(*(var))); \
	memcpy(p, &_tval, sizeof(sertype));}

#define unseras(var, p, sertype) { \
        sertype _tval; \
        memcpy(&_tval, (p), sizeof(sertype)); \
        *(var) = (typeof(*(var)))_tval;}

/*! \brief
 * Loads contacts in destination set into "serial_avp" AVP in reverse
 * priority order and associated each contact with Q_FLAG telling if
 * contact is the last one in its priority class.  Finally, removes
 * all branches from destination set.
 */
int serialize_branches(struct sip_msg *msg, int clean_before, int keep_order)
{
	static struct serial_contact contacts[MAX_BRANCHES];
	struct msg_branch *branch;
	int n, last, first, i, prev, bflags;
	str *ruri;
	qvalue_t ruri_q;
	char *p;
	str enc_info;
	int_str val;
	int idx;

	/* Check if anything needs to be done */
	if (get_dset_size() == 0) {
		LM_DBG("nothing to do - no branches!\n");
		return 0;
	}

	ruri = GET_RURI(msg);
	ruri_q = get_ruri_q(msg);
	bflags = getb0flags(msg);

	for (idx = 0; (branch = get_msg_branch(idx)); idx++) {
		if (branch->q != ruri_q)
			break;
	}

	if (branch==NULL && !keep_order) {
		LM_DBG("nothing to do - all same q!\n");
		return 0;
	}

	/* reset contact array */
	n = 0;

	/* Insert Request-URI to contact list */
	enc_info.len = 3 * sizeof(long)
			+ ruri->len + msg->dst_uri.len + msg->path_vec.len + 3;
	enc_info.s = (char*) pkg_malloc (enc_info.len);

	if (!enc_info.s) {
		LM_ERR("no pkg memory left\n");
		goto error; /* nothing to free here */
	}

	memset(enc_info.s, 0, enc_info.len);
	p = enc_info.s;

	LM_DBG("Msg information <%.*s,%.*s,%.*s,%d,%u>\n",
			ruri->len, ruri->s,
			msg->dst_uri.len, msg->dst_uri.s,
			msg->path_vec.len, msg->path_vec.s,
			ruri_q, bflags);

	seras(p, &msg->force_send_socket, long);
	p += sizeof(long);
	seras(p, &bflags, long);
	p += sizeof(long);
	seras(p, &ruri_q, long);
	p += sizeof(long);

	memcpy(p , ruri->s, ruri->len);
	p += ruri->len + 1;
	memcpy(p, msg->dst_uri.s, msg->dst_uri.len);
	p += msg->dst_uri.len + 1;
	memcpy(p, msg->path_vec.s, msg->path_vec.len);

	contacts[n].enc_info = enc_info;
	contacts[n].q = ruri_q;
	contacts[n].next = -1;
	last = n;
	first = n;
	n++;

	/* Insert branch URIs to contact list in increasing q order */
	for (idx = 0;(branch=get_msg_branch(idx))!=NULL; idx++){

		enc_info.len = 3 * sizeof(long)
			+ branch->uri.len + branch->dst_uri.len + branch->path.len + 3;
		enc_info.s = (char*) pkg_malloc (enc_info.len);

		if (!enc_info.s) {
			LM_ERR("no pkg memory left\n");
			goto error_free;
		}

		memset(enc_info.s, 0, enc_info.len);
		p = enc_info.s;

		LM_DBG("Branch information <%.*s,%.*s,%.*s,%d,%u>\n",
				branch->uri.len, branch->uri.s,
				branch->dst_uri.len, branch->dst_uri.s,
				branch->path.len, branch->path.s,
				branch->q, branch->bflags);

		seras(p, &branch->force_send_socket, long);
		p += sizeof(long);
		seras(p, &branch->bflags, long);
		p += sizeof(long);
		seras(p, &branch->q, long);
		p += sizeof(long);

		memcpy(p , branch->uri.s, branch->uri.len);
		p += branch->uri.len + 1;
		memcpy(p, branch->dst_uri.s, branch->dst_uri.len);
		p += branch->dst_uri.len + 1;
		memcpy(p, branch->path.s, branch->path.len);

		contacts[n].enc_info = enc_info;
		contacts[n].q = branch->q;

		if (keep_order) {
			contacts[n].next = first;
			first = n++;
			continue;
		}

		/* insert based on ascending q values, so add_avp() reverses them */
		for (i = first, prev = -1;
			i != -1 && contacts[i].q < branch->q;
			prev = i ,i = contacts[i].next);

		if (i == -1) {
			/* append */
			last = contacts[last].next = n;
			contacts[n].next = -1;
		} else {
			if (i == first) {
				/* first element */
				contacts[n].next = first;
				first = n;
			} else {
				/* before pos i */
				contacts[n].next = contacts[prev].next;
				contacts[prev].next = n;
			}
		}

		n++;
	}

	/* Assign values for q_flags */
	for (i = first; contacts[i].next != -1; i = contacts[i].next) {
		if (keep_order || contacts[i].q < contacts[contacts[i].next].q)
			contacts[contacts[i].next].q_flag = Q_FLAG;
		else
			contacts[contacts[i].next].q_flag = 0;
	}

	if (clean_before)
		destroy_avps( 0/*type*/, serial_avp, 1/*all*/);

	/* Add contacts to "contacts" AVP */
	for (i = first; i != -1; i = contacts[i].next) {
		val.s = contacts[i].enc_info;

		if (add_avp( AVP_VAL_STR|contacts[i].q_flag, serial_avp, val)) {
			LM_ERR("failed to add avp\n");
			goto error_free;
		}

		pkg_free(contacts[i].enc_info.s);
		contacts[i].enc_info.s = NULL;
	}

	/* Clear all branches */
	clear_dset();

	return 0;
error_free:
	for( i=0 ; i<n ; i++) {
		if (contacts[i].enc_info.s)
			pkg_free(contacts[i].enc_info.s);
	}
error:
	return -1;
}



/*! \brief
 * Adds to request a destination set that includes all highest priority
 * class contacts in "serial_avp" AVP.   If called from a route block,
 * rewrites the request uri with first contact and adds the remaining
 * contacts as branches.  If called from failure route block, adds all
 * contacts as brances.  Removes added contacts from "serial_avp" AVP.
 */
int next_branches( struct sip_msg *msg)
{
	struct msg_branch branch;
	struct usr_avp *avp, *prev;
	int_str val;
	struct socket_info *sock_info;
	qvalue_t q;
	str uri, dst_uri, path, path_dst;
	char *p;
	unsigned int flags, last_parallel_fork;
	int rval;

	if (route_type != REQUEST_ROUTE && route_type != FAILURE_ROUTE ) {
		/* unsupported route type */
		LM_ERR("called from unsupported route type %d\n", route_type);
		goto error;
	}

	/* Find first avp  */
	avp = search_first_avp(0, serial_avp, &val, 0);

	if (!avp) {
		LM_DBG("no AVPs -- we are done!\n");
		goto error;
	}

	if (!val.s.s) {
		LM_ERR("invalid avp value\n");
		goto error;
	}

	/* *sock_info, flags, q, uri, 0, dst_uri, 0, path, 0,... */

	p = val.s.s;
	unseras(&sock_info, p, long);
	p += sizeof(long);
	unseras(&flags, p, long);
	p += sizeof(long);
	unseras(&q, p, long);
	p += sizeof(long);
	uri.s = p;
	uri.len = strlen(p);
	p += uri.len + 1;
	dst_uri.s = p;
	dst_uri.len = strlen(p);
	p += dst_uri.len + 1;
	path.s = p;
	path.len = strlen(p);

	/* set PATH and DURI */
	if (path.s && path.len) {
		if (get_path_dst_uri(&path, &path_dst) < 0) {
			LM_ERR("failed to get first hop from Path\n");
			goto error1;
		}
		if (set_path_vector( msg, &path) < 0) {
			LM_ERR("failed to set path vector\n");
			goto error1;
		}
		if (set_dst_uri( msg, &path_dst) < 0) {
			LM_ERR("failed to set dst_uri of Path\n");
			goto error1;
		}
	} else {
		if (set_dst_uri( msg, &dst_uri) < 0) {
			goto error1;
		}
	}

	/* Set Request-URI */
	if ( set_ruri(msg, &uri) == -1 )
		goto error1;

	msg->force_send_socket = sock_info;
	set_ruri_q( msg, q );
	setb0flags( msg, flags );

	LM_DBG("Msg information <%.*s,%.*s,%.*s,%d,%u> (avp flag=%u)\n",
				uri.len, uri.s,
				dst_uri.len, dst_uri.s,
				path.len, path.s,
				q, flags, avp->flags);

	last_parallel_fork = (avp->flags & Q_FLAG);
	prev = avp;
	avp = search_next_avp(avp, &val);
	destroy_avp(prev);

	if (last_parallel_fork)
		goto done;

	/* Append branches until out of branches or Q_FLAG is set */
	while (avp != NULL) {

		if (!val.s.s) {
			LM_ERR("invalid avp value\n");
			goto next_avp;
		}

		memset( &branch, 0, sizeof branch);
		p = val.s.s;
		unseras(&branch.force_send_socket, p, long);
		p += sizeof(long);
		unseras(&branch.bflags, p, long);
		p += sizeof(long);
		unseras(&branch.q, p, long);
		p += sizeof(long);
		branch.uri.s = p;
		branch.uri.len = strlen(p);
		p += strlen(p) + 1;
		branch.dst_uri.s = p;
		branch.dst_uri.len = strlen(p);
		p += strlen(p) + 1;
		branch.path.s = p;
		branch.path.len = strlen(p);

		LM_DBG("Branch information <%.*s,%.*s,%.*s,%d,%u> (avp flag=%u)\n",
				branch.uri.len, branch.uri.s,
				branch.dst_uri.len, branch.dst_uri.s,
				branch.path.len, branch.path.s,
				branch.q, branch.bflags, avp->flags);


		rval = append_msg_branch(&branch);

		if (rval == -1) {
			LM_ERR("append_branch failed\n");
			goto error1;
		}

	next_avp:
		last_parallel_fork = (avp->flags & Q_FLAG);
		prev = avp;
		avp = search_next_avp(avp, &val);
		destroy_avp(prev);

		if (last_parallel_fork)
			goto done;
	}

	return 2;
done:
	return avp ? 1 : 2;
error1:
	destroy_avp(avp);
error:
	return -1;
}
