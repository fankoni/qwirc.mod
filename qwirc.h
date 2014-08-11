/*
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

#ifndef QWIRC_H
#define	QWIRC_H

#include "qw_common.h"
#include "../module.h"

#define MAKING_QWIRC
#define MODULE_NAME "qwirc"

#define UFLAG_QWADMIN     0x00010000    // This corresponds to the flag 'Q' in an eggdrop uflag integer

// Public command permission flags
#define PERM_QSAY         0x00000001    // 1
#define PERM_QCONNECT     0x00000002    // 2
#define PERM_QDISCONNECT  0x00000004    // 4
#define PERM_QRCON        0x00000008    // 8
#define PERM_QMAP         0x00000010    // 16
#define PERM_QHELP        0x00000020    // 32
#define PERM_ALL          0x3F          // 63
// !qsay, !qmap and !qhelp are allowed for all users by default
#define PERM_DEFAULT      (PERM_QSAY | PERM_QMAP | PERM_QHELP)

// QuakeWorld server settings, text output colors. These are TCL-configurable
char qw_name[25], qw_server[100], qw_password[100], qw_rcon_password[100];
int qw_bottomcolor, qw_topcolor, qw_msgmode, qw_rate, qw_encrypt_rcon, qw_server_port;
int color_statusmessage, color_centerprint, color_normaltext, color_chattext;

// Table for decoding QuakeWorld-encoded chat messages
char qw_char_tbl[256]; 
// Chat messages queued from IRC to QuakeWorld
char irc_msg_buf[MAX_PRINT_MSG]; 
// Amount of memory used by the QuakeWorld thread
long qw_maxrss = 0;
// Current QW map name
char qw_map[40];
// IRC channel the module is being used on
char qw_channel[25];

// QuakeWorld game loop thread handling
pthread_mutex_t qw_mutex;
pthread_mutexattr_t qw_attr;
pthread_t qw_thread;
int qw_thread_status = 1;

// Module functions
void irc_print(char* msg, int color);
static int has_qflag(char* nick, char* channel);
static void qw_cleantext(unsigned char *text);
static void qw_cleantext_init (void);
static void qw_rcon(char *nick, char *host, char *hand, char *channel, char *text, int idx);
static void qw_mapinfo(char *nick, char *host, char *hand, char *channel, char *text, int idx);
static void qw_help(char *nick, char *host, char *hand, char *channel, char *text, int idx);
static void qw_connect(char *nick, char *host, char *hand, char *channel, char *text);
static void qw_disconnect(char *nick, char *host, char *hand, char *channel, char *text);
static void qw_say(char *nick, char *host, char *hand, char *channel, char *text, int idx);

static int qwirc_shutdown(char *channel);
static void qwirc_report(int idx, int details);
static int qwirc_expmem();

// Function tables of required module dependencies
static Function *global = NULL;
static Function *irc_funcs, *server_funcs, *channels_funcs = NULL;

// Exported functions
EXPORT_SCOPE char *qwirc_start();
static Function qwirc_table[] =
{
    (Function) qwirc_start,
    (Function) qwirc_shutdown,
    (Function) qwirc_expmem,
    (Function) qwirc_report,
};

// Public commands
static cmd_t qwirc_public_cmds[] =
{
    {"!qconnect",             "",               (IntFunc) qw_connect,    NULL},
    {"!qdisconnect",          "",               (IntFunc) qw_disconnect, NULL},
    {"!qsay",                 "",               (IntFunc) qw_say,        NULL},
    {"!qrcon",                "",               (IntFunc) qw_rcon,       NULL},
    {"!qmap",                 "",               (IntFunc) qw_mapinfo,    NULL},
    {"!qhelp",                "",               (IntFunc) qw_help,       NULL},
    {NULL,                    NULL,             NULL,                    NULL}
};

// TCL-configurable strings
static tcl_strings qwirc_tcl_strings[] =
{
    {"qw_server",             qw_server,        512,  0},
    {"qw_name",               qw_name,          512,  0},
    {"qw_rcon_password",      qw_rcon_password, 512,  0},
    {"qw_password",           qw_password,      512,  0},
    {0,                       0,                0,    0}
};

// TCL-configurable integers
static tcl_ints qwirc_tcl_ints[] =
{
  {"qw_color_statusmessage", &color_statusmessage, 1},
  {"qw_color_normaltext",    &color_normaltext,    1},
  {"qw_color_centerprint",   &color_centerprint,   1},
  {"qw_color_chattext",      &color_chattext,      1},
  {"qw_server_port",         &qw_server_port,      0},
  {"qw_encrypt_rcon",        &qw_encrypt_rcon,     0},
  {"qw_bottomcolor",         &qw_bottomcolor,      0},
  {"qw_topcolor",            &qw_topcolor,         0},
  {"qw_msgmode",             &qw_msgmode,          0},
  {"qw_rate",                &qw_rate,             0},
  {0,                        0,                    0}
};      

#endif	/* QWIRC_H */

