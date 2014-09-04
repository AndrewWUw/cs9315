/*
 * src/tutorial/email.c
 *
 ******************************************************************************
  This file contains routines that can be bound to a Postgres backend and
  called by the backend in the process of processing queries.  The calling
  format for these routines is dictated by Postgres architecture.
******************************************************************************/

#include "postgres.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>  
#include <string.h>
#include <ctype.h>
#include "fmgr.h"
#include "libpq/pqformat.h"		/* needed for send/recv functions */
#include "access/hash.h"

PG_MODULE_MAGIC;

/**
 * EmailAddress Type Grammar
 *
 * EmailAddress ::= Local '@' Domain
 * Local        ::= NamePart NameParts
 * Domain       ::= NamePart '.' NamePart NameParts
 * NamePart     ::= Letter | Letter NameChars (Letter|Digit)
 * NameParts    ::= Empty | '.' NamePart NameParts
 * NameChars    ::= Empty | (Letter|Digit|'-') NameChars
 * Letter       ::= 'a' | 'b' | ... | 'z' | 'A' | 'B' | ... 'Z'
 * Digit        ::= '0' | '1' | '2' | ... | '8' | '9'
 */

#define MAX_LENGTH 128
 
typedef struct EmailAddress
{
	char local[MAX_LENGTH];
	char domain[MAX_LENGTH];
	char full_address[2 * MAX_LENGTH + 1];
}	EmailAddress;

/*
 * Since we use V1 function calling convention, all these functions have
 * the same signature as far as C is concerned.  We provide these prototypes
 * just to forestall warnings when compiled with gcc -Wmissing-prototypes.
 */
Datum		email_address_in(PG_FUNCTION_ARGS);
Datum		email_address_out(PG_FUNCTION_ARGS);
Datum		email_address_recv(PG_FUNCTION_ARGS);
Datum		email_address_send(PG_FUNCTION_ARGS);

Datum		email_address_abs_lt(PG_FUNCTION_ARGS);
Datum		email_address_abs_le(PG_FUNCTION_ARGS);
Datum		email_address_abs_eq(PG_FUNCTION_ARGS);
Datum		email_address_abs_neq(PG_FUNCTION_ARGS);
Datum		email_address_abs_ge(PG_FUNCTION_ARGS);
Datum		email_address_abs_gt(PG_FUNCTION_ARGS);
Datum		email_address_abs_cmp(PG_FUNCTION_ARGS);
Datum		email_address_abs_hash(PG_FUNCTION_ARGS);
Datum		email_address_abs_match_domain(PG_FUNCTION_ARGS);
Datum		email_address_abs_not_match_domain(PG_FUNCTION_ARGS);

bool is_valid_email_address(const char *);

bool is_valid_email_address(const char * addr)
{
	int count = 0;
	const char *c;
	const char *domain;
	static char rfc822_specials[] = { '(', ')', '<', '>', '@', ',', ';', ':',
			'\\', '[', ']', ' ' };

	/* Email address format : n@d
	 * n = local Name
	 * d = email address Domain
	 */

	if (sizeof(addr) > (128 + 128 + 1 + 1))
		return 0;

	// Check n in n@d
	for (c = addr; *c != '\0'; ++c) {
		if (*c == ' ' && (c == addr || *(c - 1) == '.' || *(c - 1) == ' ')) {
			while (*++c) {
				if (*c == ' ') {
					break;
				}
				if (*c == '\\' && (*++c == ' ')) {
					continue;
				}
				if (*c <= ' ' || *c >= 127) {
					return 0;
				}
			}
			if (!*c++) {
				return 0;
			}
			if (*c == '@') {
				break;
			}
			if (*c != '.') {
				return 0;
			}
			continue;
		}
		if (*c == '@') {
			break;
		}
		if (*c <= ' ' || *c >= 127) {
			return 0;
		}
		if (strchr(rfc822_specials, *c)) {
			return 0;
		}
	}
	if (c == addr || *(c - 1) == '.') {
		return 0;
	}

	// Check d in n@d
	if (!*(domain = ++c)) {
		return 0;
	}
	do {
		if (*c == '.') {
			if (c == domain || *(c - 1) == '.') {
				return 0;
			}
			count++;
		}
		if (*c <= ' ' || *c >= 127) {
			return 0;
		}
		if (strchr(rfc822_specials, *c)) {
			return 0;
		}
	} while (*++c);

	return (count >= 1);
}

/*****************************************************************************
 * Input/Output functions
 *****************************************************************************/

PG_FUNCTION_INFO_V1(email_address_in);

Datum
email_address_in(PG_FUNCTION_ARGS)
{
	char *str = PG_GETARG_CSTRING(0);

	if (!is_valid_email_address(str))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for EmailAddress: \"%s\"", str)));

	int i = 0;
	while (str[i] != '\0') {
		str[i] = (tolower(str[i]));
		++i;
	}

	EmailAddress *result;
	result = (EmailAddress *) palloc(sizeof(EmailAddress));

	strcpy(result->full_address, str);

	char * token = palloc(sizeof(char) * strlen(str));
	strcpy(token, str);

	token = strtok(token, "@");
	strcpy(result->local, token);

	token = strtok(NULL, "@");

	strcpy(result->domain, token);
	strcat(result->full_address, "@");

	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(email_address_out);

Datum
email_address_out(PG_FUNCTION_ARGS)
{
	EmailAddress *email_address = (EmailAddress *) PG_GETARG_POINTER(0);
	char	  	 *result;

	result = (char *) palloc(sizeof(char) * strlen(email_address->full_address));
	snprintf(result, sizeof(char) * strlen(email_address->full_address), "%s\n", email_address->full_address);
//	snprintf(result, sizeof(char) * strlen(email_address->full_address), "%s@%s", email_address->local, email_address->domain);
	PG_RETURN_CSTRING(result);
}

/*****************************************************************************
 * Binary Input/Output functions
 *
 * These are optional.
 *****************************************************************************/

PG_FUNCTION_INFO_V1(email_address_recv);

Datum
email_address_recv(PG_FUNCTION_ARGS)
{
	StringInfo	buf = (StringInfo) PG_GETARG_POINTER(0);
	EmailAddress *result;

	result = (EmailAddress *) palloc(sizeof(EmailAddress));
	strcpy(result->local, pq_getmsgstring(buf));
	strcpy(result->domain, pq_getmsgstring(buf));
	strcpy(result->full_address, pq_getmsgstring(buf));

	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(email_address_send);

Datum
email_address_send(PG_FUNCTION_ARGS)
{
	EmailAddress *email_address = (EmailAddress *) PG_GETARG_POINTER(0);
	StringInfoData buf;

	pq_begintypsend(&buf);
	pq_sendstring(&buf, email_address->local);
	pq_sendstring(&buf, email_address->domain);
	pq_sendstring(&buf, email_address->full_address);
	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}


/*****************************************************************************
 * Operator class for defining B-tree index
 *
 * It's essential that the comparison operators and support function for a
 * B-tree index opclass always agree on the relative ordering of any two
 * data values.  Experience has shown that it's depressingly easy to write
 * unintentionally inconsistent functions.	One way to reduce the odds of
 * making a mistake is to make all the functions simple wrappers around
 * an internal three-way-comparison function, as we do here.
 *****************************************************************************/

static int
email_address_abs_cmp_internal(EmailAddress * a, EmailAddress * b)
{
	if (strcmp(a->full_address, b->full_address) != 0) {

		if (strcmp(a->domain, b->domain) != 0) {
			return(strcmp(a->domain, b->domain));
		} else {
			return(strcmp(a->local, b->local));
		}
	}
	return 0;
}


PG_FUNCTION_INFO_V1(email_address_abs_lt);

Datum
email_address_abs_lt(PG_FUNCTION_ARGS)
{
	EmailAddress *a = (EmailAddress *) PG_GETARG_POINTER(0);
	EmailAddress *b = (EmailAddress *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(email_address_abs_cmp_internal(a, b) < 0);
}

PG_FUNCTION_INFO_V1(email_address_abs_le);

Datum
email_address_abs_le(PG_FUNCTION_ARGS)
{
	EmailAddress *a = (EmailAddress *) PG_GETARG_POINTER(0);
	EmailAddress *b = (EmailAddress *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(email_address_abs_cmp_internal(a, b) <= 0);
}

PG_FUNCTION_INFO_V1(email_address_abs_eq);

Datum
email_address_abs_eq(PG_FUNCTION_ARGS)
{
	EmailAddress *a = (EmailAddress *) PG_GETARG_POINTER(0);
	EmailAddress *b = (EmailAddress *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(email_address_abs_cmp_internal(a, b) == 0);
}

PG_FUNCTION_INFO_V1(email_address_abs_neq);

Datum
email_address_abs_neq(PG_FUNCTION_ARGS)
{
	EmailAddress *a = (EmailAddress *) PG_GETARG_POINTER(0);
	EmailAddress *b = (EmailAddress *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(email_address_abs_cmp_internal(a, b) != 0);
}

PG_FUNCTION_INFO_V1(email_address_abs_ge);

Datum
email_address_abs_ge(PG_FUNCTION_ARGS)
{
	EmailAddress *a = (EmailAddress *) PG_GETARG_POINTER(0);
	EmailAddress *b = (EmailAddress *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(email_address_abs_cmp_internal(a, b) >= 0);
}

PG_FUNCTION_INFO_V1(email_address_abs_gt);

Datum
email_address_abs_gt(PG_FUNCTION_ARGS)
{
	EmailAddress *a = (EmailAddress *) PG_GETARG_POINTER(0);
	EmailAddress *b = (EmailAddress *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(email_address_abs_cmp_internal(a, b) > 0);
}

PG_FUNCTION_INFO_V1(email_address_abs_cmp);

Datum
email_address_abs_cmp(PG_FUNCTION_ARGS)
{
	EmailAddress *a = (EmailAddress *) PG_GETARG_POINTER(0);
	EmailAddress *b = (EmailAddress *) PG_GETARG_POINTER(1);

	PG_RETURN_INT32(email_address_abs_cmp_internal(a, b));
}

PG_FUNCTION_INFO_V1(email_address_abs_hash);

Datum
email_address_abs_hash(PG_FUNCTION_ARGS)
{
	EmailAddress *a = (EmailAddress *) PG_GETARG_POINTER(0);
	int len = strlen(a->full_address);
//	int hash = DatumGetUInt32(hash_any((const unsigned char * )a->full_address, len));

	PG_RETURN_INT32(DatumGetUInt32(hash_any((const unsigned char * )a->full_address, len)));
}

PG_FUNCTION_INFO_V1(email_address_abs_match_domain);

Datum
email_address_abs_match_domain(PG_FUNCTION_ARGS)
{
	EmailAddress *a = (EmailAddress *) PG_GETARG_POINTER(0);
	EmailAddress *b = (EmailAddress *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(strcmp(a->domain, b->domain) == 0);
//	if (strcmp(a->domain, b->domain)) {
//		PG_RETURN_BOOL(0);
//	} else {
//		PG_RETURN_BOOL(1);
//	}
}

PG_FUNCTION_INFO_V1(email_address_abs_not_match_domain);

Datum
email_address_abs_not_match_domain(PG_FUNCTION_ARGS)
{
	EmailAddress *a = (EmailAddress *) PG_GETARG_POINTER(0);
	EmailAddress *b = (EmailAddress *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(strcmp(a->domain, b->domain) != 0);
//	if (strcmp(a->domain, b->domain)) {
//		PG_RETURN_BOOL(1);
//	} else {
//		PG_RETURN_BOOL(0);
//	}
}
