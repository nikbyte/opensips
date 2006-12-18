/* 
 * $Id$
 *
 * SNMPStats Module 
 * Copyright (C) 2006 SOMA Networks, INC.
 * Written by: Jeffrey Magder (jmagder@somanetworks.com)
 *
 * This file is part of openser, a free SIP server.
 *
 * openser is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * openser is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * History:
 * --------
 * 2006-11-23 initial version (jmagder)
 * 
 * This file was created to group together utility functions that were useful
 * throughout the SNMPStats module, without belonging to any file in particular.
 */

#include <stdlib.h>
#include <string.h>

#include "utilities.h"

#include "../../str.h"
#include "../../locking.h"
#include "../../mem/mem.h"

/*
 * This function copies an OpenSER "str" datatype into a '\0' terminated char*
 * string. 
 *
 * NOTE: Make sure to free the memory allocated to *copiedString, when you no
 *       longer have any use for it. (It is allocated with shm_malloc(), so make
 *       sure to deallocate it with shm_free()) 
 */
int convertStrToCharString(str *strToConvert, char **copiedString) 
{
	/* We want enough space for the string, plus 1 for the '\0' character. */
	*copiedString = shm_malloc(sizeof(char) * (strToConvert->len + 1));

	if (*copiedString == NULL)
	{
		return 0;
	}

	memcpy(*copiedString, strToConvert->s, strToConvert->len);
	(*copiedString)[strToConvert->len] = '\0';

	return 1;
}


/* Silently returns 1 if the supplied parameters are sane.  Otherwise, an error
 * message is logged for parameterName, and 0 returned. */
int stringHandlerSanityCheck( modparam_t type, void *val, char *parameterName) 
{
	char *theString = (char *)val;

	/* Make sure the function was called correctly. */
	if (type != STR_PARAM) {
		LOG(L_ERR,"ERROR: SNMPStats: The %s parameter was assigned "
				" a type %d instead of %d\n", parameterName, 
					type, STR_PARAM);
		return 0;
	}

	/* An empty string was supplied.  We consider this illegal */
	if (theString==0 || (theString[0])==0) {
		LOG(L_ERR, "ERROR: SNMPStats: The %s parameter was specified "
				" with an empty string\n", parameterName); 
		return 0;
	}

	return 1;
}



/* 
 * This function is a wrapper around the standard statistic framework.  It will
 * return the value of the statistic denoted with statName, or zero if the
 * statistic was not found. 
 */
int get_statistic(char *statName) 
{
	int result = 0;

	str theStr;

	theStr.s   = statName;
	theStr.len = strlen(statName);

	stat_var *theVar = get_stat(&theStr);
	
	if (theVar==0) {
		LOG(L_INFO, "INFO: SNMPStats: Couldn't retrieve statistics "
				"for %s\n", statName);
	} else {
		result = get_stat_val(theVar);
	}

	return result;
}

/* Returns a pointer to an SNMP DateAndTime OCTET STRING representation of the
 * time structure.  Note that the pointer is to static data, so it shouldn't be
 * counted on to be around if this function is called again. */
char * convertTMToSNMPDateAndTime(struct tm *timeStructure) 
{
	static char dateAndTime[8];

	/* The tm structure stores the number of years since 1900.  We need to
	 * change the offset. */
	int currentYear = timeStructure->tm_year + 1900;

	/* See SNMPv2-TC for the conversion details */
	dateAndTime[0] = (char) ((currentYear & 0xFF00) >> 8);
	dateAndTime[1] = (char)  currentYear & 0xFF;
	dateAndTime[2] = (char) timeStructure->tm_mon + 1;
	dateAndTime[3] = (char) timeStructure->tm_mday;
	dateAndTime[4] = (char) timeStructure->tm_hour;
	dateAndTime[5] = (char) timeStructure->tm_min;
	dateAndTime[6] = (char) timeStructure->tm_sec;
	dateAndTime[7] = 0;

	return dateAndTime;
}

