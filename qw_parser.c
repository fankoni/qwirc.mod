/*
Based on QuakeWorld source code, copyright (C) 1996-1997 Id Software Inc.
Copyright (C) 2014 aku.hasanen@kapsi.fi

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "qw_common.h"

static int cmd_argc;
static char *cmd_argv[MAX_STRING_TOKENS];
static char cmd_args[MAX_STRING_CHARS];
static char *cmd_null_string = "";
char com_token[MAX_TOKEN_CHARS];


/*
 * Command parsing functions:
 * Handle parsing incoming commands.
 */

/*
============
parser_argc
Returns command argument count
============
 */
int parser_argc(void) {
    return cmd_argc;
}

/*
============
parser_argv
============
 */
char *parser_argv(int arg) {
    if ((unsigned) arg >= cmd_argc)
        return cmd_null_string;
    return cmd_argv[arg];
}



/*
============
parser_args
Returns a single string containing argv(1) to argv(argc()-1)
============
 */
char *parser_args(void) {
    return cmd_args;
}

/*
==============
parser_get_token
Parse a token out of a string
==============
 */
static char *parser_get_token(char **data_p) {
    int c;
    int len;
    char *data;

    data = *data_p;
    len = 0;
    com_token[0] = 0;

    if (!data) {
        *data_p = NULL;
        return "";
    }

    // Skip whitespace
skipwhite:
    while ((c = *data) <= ' ') {
        if (c == 0) {
            *data_p = NULL;
            return "";
        }
        data++;
    }

    // Skip // comments
    if (c == '/' && data[1] == '/') {
        while (*data && *data != '\n')
            data++;
        goto skipwhite;
    }

    // Handle quoted strings specially
    if (c == '\"') {
        data++;
        while (1) {
            c = *data++;
            if ((c == '\"') || !c) {
                com_token[len] = 0;
                *data_p = data;
                return com_token;
            }

            if (len < MAX_TOKEN_CHARS) {
                com_token[len] = c;
                len++;
            }
        }
    }

    // Parse a regular word
    do {
        if (len < MAX_TOKEN_CHARS) {
            com_token[len] = c;
            len++;
        }
        data++;
        c = *data;
    } while (c > 32);

    if (len == MAX_TOKEN_CHARS)
        len = 0;

    com_token[len] = 0;

    *data_p = data;
    return com_token;
}

/*
=============
parser_macro_expand
=============
 */
static char *parser_macro_expand(char *text) {
    int i, j, count = 0, len;
    bool in_quotes = false;
    char *scan = text;
    static char expanded[MAX_STRING_CHARS];
    char temporary[MAX_STRING_CHARS];
    char *token, *start;

    len = (int) strlen(scan);
    if (len >= MAX_STRING_CHARS) {
        printf("Line exceeded %i chars, discarded. (parser_macro_expand())\n", MAX_STRING_CHARS);
        return NULL;
    }

    for (i = 0; i < len; i++) {
        if (scan[i] == '"')
            in_quotes ^= 1;
        if (in_quotes)
            continue; // Don't expand inside quotes
        if (scan[i] != '$')
            continue;
        // Scan out the complete macro
        start = scan + i + 1;
        token = parser_get_token(&start);
        if (!start)
            continue;

        j = (int) strlen(token);
        len += j;
        if (len >= MAX_STRING_CHARS) {
            printf("Expanded line exceeded %i chars, discarded. (parser_macro_expand())\n", MAX_STRING_CHARS);
            return NULL;
        }

        strncpy(temporary, scan, i);
        strcpy(temporary + i, token);
        strcpy(temporary + i + j, start);

        strcpy(expanded, temporary);
        scan = expanded;

        i--;

        if (++count == 100) {
            printf("Macro expansion loop, discarded. (parser_macro_expand())\n");
            return NULL;
        }
    }

    if (in_quotes) {
        printf("Line has unmatched quote, discarded. (parser_macro_expand())\n");
        return NULL;
    }

    return scan;
}

/*
============
parser_tokenize
============
 */
void parser_tokenize(char *text, bool macro_expand) {
    int i;
    char *com_token;

    // Clear the args from the last string
    for (i = 0; i < cmd_argc; i++)
        qw_eggdrop_free(cmd_argv[i]);

    cmd_argc = 0;
    cmd_args[0] = 0;

    // Macro expand the text
    if (macro_expand)
        text = parser_macro_expand(text);

    if (!text)
        return;

    for (;;) {
        // Skip whitespace up to a \n
        while (*text && *text <= ' ' && *text != '\n')
            text++;

        if (*text == '\n') {
            // A newline seperates commands in the buffer
            text++;
            break;
        }

        if (!*text)
            return;

        // Set cmd_args to everything after the first arg
        if (cmd_argc == 1) {
            int l;

            strcpy(cmd_args, text);

            // Strip off any trailing whitespace
            l = (int) strlen(cmd_args) - 1;
            for (; l >= 0; l--)
                if (cmd_args[l] <= ' ')
                    cmd_args[l] = 0;
                else
                    break;
        }

        com_token = parser_get_token(&text);
        if (!text)
            return;

        if (cmd_argc < MAX_STRING_TOKENS) {
            cmd_argv[cmd_argc] = qw_eggdrop_malloc(strlen(com_token) + 1);
            strcpy(cmd_argv[cmd_argc], com_token);
            cmd_argc++;
        }
    }
}

/*
============
strnstr
Case insensitive search of an hlen character array for a substring.
Copyright (c) 1996-2000 University of Utah and the Flux Group.
All rights reserved.
============
 */
char *strnstr(char *haystack, int hlen, char *needle)
{
	int nlen = strlen(needle);

	while (hlen >= nlen)
	{
		if (!strncasecmp(haystack, needle, nlen))
			return (char *)haystack;

		haystack++;
		hlen--;
	}
	return NULL;
}

