// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <util/alloc.h>
#include <util/parser.h>


/**
 * match_once: - Determines if a string matches a simple pattern
 * @str: the string to examine for presence of the pattern
 * @ptrn: the string containing the pattern
 * @matched: 0 / 1 on no-match / match
 * @val_found: 0 / 1 on no-match / match
 * @val: struct substring with beginning / end of value to read
 *
 * This function takes as input a string to be scanned and a pattern defining
 * the scan. The patterns are of the form "name" or "name=%?" where "%?" is
 * a printf()-like conversion specifier drawn from 's', 'd', 'o', 'u', and 'x'.
 *
 * If the string to be scanned does not begin with "name", then "matched" and
 * "val_found" will be set to 0 and the function returns. Repeated '%'
 * characters are treated as literals, as are any number of '=' that are not
 * followed by a conversion specifier. If there is no conversion specifier and
 * the string to be scanned is "name", then "matched" will be set to 1 and
 * "val_found" set to 0.
 *
 * If the pattern has a conversion specifier, then for the 'd', 'o', 'u', and
 * 'x' cases the portion of string to be scanned after the '=' will be
 * processed to determine if it is a valid decimal, octal, unsigned decimal,
 * or hexadecimal number respectively. If not then "matched" and "val_found"
 * will be set to 0 and the function returns. If so, then "matched" and
 * "val_found" will be set to 1 and val.ss_begin will be set to the first
 * byte of the numeric string and val.ss_end will be set to the byte following
 * the numeric string.
 *
 * In the 's' case, then "matched" and "val_found" will be set to 1 and
 * val.ss_begin will be set to the first byte of the string and val.ss_end
 * will be set to the byte after the string. There may be a non-negative
 * integer between the '%' and the 's in the conversion specifier in which
 * case the maximum length of the "val.ss_begin"/"val.ss_end" string is
 * capped to that length.
 *
 * This function is similar to the match_once() function in linux/lib/parser.c
 * but has (believe it or not) simpler semantics that are nonetheless
 * sufficient to satisfy all uses of match_token() in the linux kernel.
 */
static void
match_once(
	const char     *str,
	const char     *ptrn,
	u32            *matched,
	u32            *val_found,
	substring_t    *val)
{
	const char  *scan = str;
	const char  *p, *beg;

	s32    value_len = -1;
	u32    ptrn_prefix_len;

	if (strlen(scan) == 0) {
		if (strlen(ptrn) == 0)
			goto match_no_val;
		else
			goto no_match;
	}

	/* if the pattern doesn't have a conversion specifier, just compare */
	p = strchr(ptrn, '%');
	if (!p) {
		if (strcmp(scan, ptrn) == 0)
			goto match_no_val;
		else
			goto no_match;
	}

	/* pattern has one or more '%', so compare prefix */
	ptrn_prefix_len = p - ptrn;
	if (strncmp(scan, ptrn, ptrn_prefix_len))
		goto no_match;

	/* skip over already matched portion of string & pattern */
	scan = scan + ptrn_prefix_len;
	ptrn = p;

	/* if we have matches for literal %'s, move past them ... */
	while (*scan && *ptrn) {
		if (*ptrn == '%' && *scan == '%') {
			++ptrn;
			++scan;
		} else
			break;
	}

	/* if both strings are now exhausted we have a match */
	if (!*ptrn && !*scan)
		goto match_no_val;
	/* otherwise if pattern is empty but scan isn't we have no match */
	if (!*ptrn)
		goto no_match;

	ptrn++;
	if (isdigit(*ptrn))
		value_len = strtoul(ptrn, (char **)&ptrn, 10);

	beg = scan;
	if (*ptrn == 's') {
		u32 scan_len = strlen(scan);

		if (value_len == -1 || value_len > scan_len)
			value_len = scan_len;

		val->from = beg;
		val->to   = scan + value_len;

		goto match_val;
	} else {
		char *end;

		errno = 0;
		switch (*ptrn) {
		case 'd':
			strtol(scan, &end, 0);
			break;
		case 'u':
			strtoul(scan, &end, 0);
			break;
		case 'o':
			strtol(scan, &end, 8);
			break;
		case 'x':
			strtol(scan, &end, 16);
			break;
		default:
			goto no_match;
		}
		if (errno || beg == end)
			goto no_match;

		val->from = beg;
		val->to   = end;

		goto match_val;
	}

no_match:
	*matched   = 0;
	*val_found = 0;
	return;

match_no_val:
	*matched   = 1;
	*val_found = 0;
	return;

match_val:
	*matched   = 1;
	*val_found = 1;
}

int match_token(const char *str, const match_table_t table, substring_t *val)
{
	u32 matched;
	u32 val_found;
	int i;

	for (i = 0; table[i].pattern; ++i) {
		if (!str || !val)
			continue;

		match_once(str, table[i].pattern, &matched, &val_found, val);
		if (matched)
			break;
	}

	return table[i].token;
}
