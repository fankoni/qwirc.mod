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

#include "qwirc.h"
#include "../irc.mod/irc.h"
#include "../server.mod/server.h"
#include "../channels.mod/channels.h"
#include "../../cmdt.h"
#include "../../tclegg.h"
#include "../../eggdrop.h"
#include "../../tclhash.h"

/*
==============
qwirc_start
Starts the module
==============
 */
char *qwirc_start(Function* global_funcs) {
    p_tcl_bind_list H_temp;
    global = global_funcs;

    module_register(MODULE_NAME, qwirc_table, VER1, VER2);

    // Check module dependencies
    if (!(irc_funcs = module_depend(MODULE_NAME, "irc", 1, 0)))
        return "You need the irc module v1.0 to use the QuakeWorld IRC module.";
    if (!(server_funcs = module_depend(MODULE_NAME, "server", 1, 0)))
        return "You need the server module v1.0 to use the QuakeWorld IRC module.";
    if (!(channels_funcs = module_depend(MODULE_NAME, "channels", 1, 1)))
        return "You need the channels module v1.1 to use the QuakeWorld IRC module.";

    // Add TCL bindings
    add_tcl_strings(qwirc_tcl_strings);
    add_tcl_ints(qwirc_tcl_ints);
    if ((H_temp = find_bind_table("pub")))
        add_builtins(H_temp, qwirc_public_cmds);

    // Register chanflag +qwirc
    initudef(UDEF_FLAG, MODULE_NAME, 1);

    putlog(LOG_MISC, "*", "QuakeWorld IRC module (%s) v%d.%d loaded.", MODULE_NAME, VER1, VER2);
    irc_msg_buf[0] = 0;

    // Init mutex
    pthread_mutexattr_init(&qw_attr);
    pthread_mutexattr_settype(&qw_attr, PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(&qw_mutex, &qw_attr);

    // Init QuakeWorld character decoding table
    qw_cleantext_init();

    // Default colors for chat text
    color_chattext = 15;
    color_statusmessage = 9;
    color_normaltext = 16;
    color_centerprint = 6;

    return NULL;
}

/*
==============
qwirc_report
Prints out module status information.
==============
 */
static void qwirc_report(int idx, int details) {
    if (details) {
        dprintf(idx, "    by aku.hasanen@kapsi.fi.\n");
        dprintf(idx, "    Using approximately %d bytes of memory.\n", qwirc_expmem());
    }
}

/*
==============
qwirc_expmem
Calculates module memory usage (bytes). Only an estimation.
==============
 */
static int qwirc_expmem() {
    int total_umem = 0, counter = 0;
    cmd_t *cur_cmd;
    tcl_strings *cur_str;
    tcl_ints *cur_int;
    
    // These are prone to change, sizes are just calculated based on 
    // declared variables in qwirc.h as of version 1.0
    total_umem += (sizeof(int) * 11) + (sizeof(long) * 2); // Integers
    total_umem += 25 + 25 + 100 + 100 + 100 + 256 + MAX_PRINT_MSG + 40; // Char tables
    total_umem += sizeof(qw_mutex) + sizeof(qw_thread) + sizeof(qw_attr); // Threading
    
    // Count public commands
    for (cur_cmd = qwirc_public_cmds; cur_cmd->name; cur_cmd++, counter++);
    total_umem += sizeof(cmd_t) * counter;
    // Count TCL strings
    counter = 0;
    for (cur_str = qwirc_tcl_strings; cur_str->name; cur_str++, counter++);
    total_umem += sizeof(tcl_strings) * counter;
    // Count TCL ints
    counter = 0;
    for (cur_int = qwirc_tcl_ints; cur_int->name; cur_int++, counter++);
    total_umem += sizeof(tcl_ints) * counter;
    
    // Get QuakeWorld thread resource usage
    if (qw_running) {
        pthread_mutex_lock(&qw_mutex);
        total_umem += qw_maxrss * 1024; // Convert to bytes
        pthread_mutex_unlock(&qw_mutex);
    }
    return total_umem;
}

/*
==============
qwirc_shutdown
Shuts down module.
==============
 */
static int qwirc_shutdown(char* channel) {
    p_tcl_bind_list H_temp;

    // Mark thread as to be shut down
    if (qw_running) {
        pthread_mutex_lock(&qw_mutex);
        qw_running = false;
        pthread_mutex_unlock(&qw_mutex);
    }

    // Remove TCL bindings
    if ((H_temp = find_bind_table("pub")))
        rem_builtins(H_temp, qwirc_public_cmds);
    rem_tcl_ints(qwirc_tcl_ints);
    rem_tcl_strings(qwirc_tcl_strings);
    module_undepend(MODULE_NAME);

    return 0;
}

/*
==============
qw_connect
Starts the QuakeWorld client in its own thread.
==============
 */
static void qw_connect(char* nick, char* host, char* hand, char* channel, char* text) {
    if (!qw_name[0] || !qw_server[0]) {
        dprintf(DP_HELP, "PRIVMSG %s :Error while loading settings. Make sure that "
                "the tcl variables qw_name and qw_server are set.\n", channel);
        return;
    } else if (!(ngetudef(MODULE_NAME, channel))) {
        dprintf(DP_HELP, "PRIVMSG %s :QuakeWorld IRC module is not enabled on "
                "this channel. Set the +qwirc chanflag.\n", channel);
        return;
    }

    // Check if !qconnect is allowed by default. If not, check for uflag 'Q'
    if (!(PERM_DEFAULT & PERM_QCONNECT)) {
        if (!has_qflag(hand, channel)) {
            dprintf(DP_HELP, "PRIVMSG %s :You don't have permission to "
                    "use !qconnect.", channel);
            return;
        }
    }

    pthread_mutex_lock(&qw_mutex);
    if (qw_running) {
        dprintf(DP_HELP, "PRIVMSG %s :QuakeWorld client is already "
            "running!\n", channel);
        return;
    }
    else
        qw_running = true;
    pthread_mutex_unlock(&qw_mutex);
    
    // Store current irc channel name
    strncpy(qw_channel, channel, strlen(channel) + 1);

    // Create thread
    qw_thread_status = pthread_create(&qw_thread, NULL, qw_init, (void *) 0);

    if (qw_thread_status) {
        pthread_mutex_lock(&qw_mutex);
        qw_running = false;
        pthread_mutex_unlock(&qw_mutex);
        dprintf(DP_HELP, "PRIVMSG %s :Error %d while creating QW thread!\n", channel, qw_thread_status);
    }
}

/*
==============
qw_disconnect
Terminates the QuakeWorld thread.
==============
 */
static void qw_disconnect(char *nick, char *host, char *hand, char *channel, char *text) {
    if (qw_running && ngetudef(MODULE_NAME, channel)) {
        // Check if !qdisconnect is allowed by default. If not, check for uflag 'Q'
        if (!(PERM_DEFAULT & PERM_QDISCONNECT)) {
            if (!has_qflag(hand, channel)) {
                dprintf(DP_HELP, "PRIVMSG %s :You don't have permission to "
                        "use !qdisconnect.", channel);
                return;
            }
        }
        // Mark thread as to be shut down
        pthread_mutex_lock(&qw_mutex);
        qw_running = false;
        pthread_mutex_unlock(&qw_mutex);
    }
}

/*
==============
qw_say
Transmits chat messages to the QuakeWorld thread.
==============
 */
static void qw_say(char *nick, char *host, char *hand, char *channel, char *text, int idx) {
    if (qw_running && ngetudef(MODULE_NAME, channel)) {

        // Check if !qsay is allowed by default. If not, check for uflag 'Q'
        if (!(PERM_DEFAULT & PERM_QSAY)) {
            if (!has_qflag(hand, channel)) {
                dprintf(DP_HELP, "PRIVMSG %s :You don't have permission to "
                        "use !qsay.", channel);
                return;
            }
        }

        char prefix[strlen(channel) + strlen(nick) + 13];
        char irc_msg[MAX_STRING_CHARS];

        // +7 comes from the added string "@IRC: " and "\n"
        if ((strlen(text) + strlen(nick) + 7) < MAX_STRING_CHARS) {
            // Add nick@IRC:
            snprintf(irc_msg, strlen(nick) + strlen(text) + 7, "%s@IRC: %s\n", nick, text);
            pthread_mutex_lock(&qw_mutex);
            strncat(irc_msg_buf, irc_msg, strlen(irc_msg)); // Append to buffer
            pthread_mutex_unlock(&qw_mutex);
        } else
            dprintf(DP_HELP, "PRIVMSG %s :%s: Can't handle a line that long!\n", prefix, text);
    }
}

/*
==============
qw_rcon
Transmits rcon messages to the QuakeWorld thread.
==============
 */
static void qw_rcon(char *nick, char *host, char *hand, char *channel, char *text, int idx) {
    if (qw_running && ngetudef(MODULE_NAME, channel)) {
        // Check if !qrcon is allowed by default. If not, check for uflag 'Q'
        if (!(PERM_DEFAULT & PERM_QRCON)) {
            if (!has_qflag(hand, channel)) {
                dprintf(DP_HELP, "PRIVMSG %s :You don't have permission to "
                        "use !qrcon.", channel);
                return;
            }
        }
        if (text)
            exec_rcon(text);
    }
}

/*
==============
qw_mapinfo
Prints the name of the current map
==============
 */
static void qw_mapinfo(char *nick, char *host, char *hand, char *channel, char *text, int idx) {
    if (qw_running && ngetudef(MODULE_NAME, channel)) {
        // Check if !qmap is allowed by default. If not, check for uflag 'Q'
        if (!(PERM_DEFAULT & PERM_QMAP)) {
            if (!has_qflag(hand, channel)) {
                dprintf(DP_HELP, "PRIVMSG %s :You don't have permission to "
                        "use !qrcon.", channel);
                return;
            }
        }
    }
    pthread_mutex_lock(&qw_mutex);
    dprintf(DP_HELP, "PRIVMSG %s :Current map: %s", channel, qw_map);
    pthread_mutex_unlock(&qw_mutex);
}

/*
==============
qw_help
Prints available commands
==============
 */
static void qw_help(char *nick, char *host, char *hand, char *channel, char *text, int idx) {
    int cmd_count = 0;
    char cmd_list[200] = {0};
    
    if (ngetudef(MODULE_NAME, channel)) {
        // Check if !qhelp is allowed by default. If not, check for uflag 'Q'
        if (!(PERM_DEFAULT & PERM_QHELP)) {
            if (!has_qflag(hand, channel)) {
                dprintf(DP_HELP, "PRIVMSG %s :You don't have permission to "
                        "use !qrcon.", channel);
                return;
            }
        }
    }
    // Count commands first
    for (cmd_t* cur_cmd = qwirc_public_cmds; cur_cmd->name; cur_cmd++, cmd_count++);
    
    // Now print to string
    for (cmd_t* cur_cmd = qwirc_public_cmds; cur_cmd->name; cur_cmd++) {
        if (cmd_list[0]) {
            strncat(cmd_list, ", ", 2);
        }
        strncat(cmd_list, cur_cmd->name, strlen(cur_cmd->name) + 1);   
    }
    
    // Print to irc
    dprintf(DP_HELP, "PRIVMSG %s :Available commands: %s.", channel, cmd_list);

}

/*
==============
qw_to_irc_print
Handles printing incoming text from QuakeWorld in IRC
==============
 */
void qw_to_irc_print(char* msg, int color) {
    static bool ignore = false;
    static char msg_buffer[MAX_PRINT_MSG];
    
    if (msg[0]) {
        char* irc_msg = nmalloc(strlen(msg) + 1);
        strncpy(irc_msg, msg, strlen(msg) + 1);
        // Remove leading newlines
        if (irc_msg[0] == '\n') {
            memmove(msg, msg + 1, strlen(irc_msg) + 1);
        }

        // Get rid of QuakeWorld's character encoding
        qw_cleantext(irc_msg);

        // Check for trigger messages.. We don't want to print all the
        // player stats at the end of the map in ktx.. Ugly check but 
        // there's no other way without modifying the QW server
        if (!ignore && color == color_chattext) {
            if (strnstr(irc_msg, 18, "Player statistics"))
                ignore = true;
        }
        // Start printing again after this
        else if (ignore && color == color_chattext) {
            if (strnstr(irc_msg, strlen(msg), "top scorers"))
                ignore = false;
        }

        if (!ignore) {
            // Check if the string begins with the bot's nick. If so, remove.
            char* bot_say_prefix = nmalloc(strlen(qw_name) + 3);
            snprintf(bot_say_prefix, strlen(qw_name) + 3, "%s: ", qw_name);
            if (strnstr(irc_msg, strlen(bot_say_prefix), bot_say_prefix) != NULL) {
                memmove(irc_msg, irc_msg + strlen(bot_say_prefix), strlen(irc_msg) - strlen(bot_say_prefix));
                // terminate
                msg[strlen(irc_msg) - strlen(bot_say_prefix) + 1] = 0;
            }
            nfree(bot_say_prefix);
            
            // Append to buffer
            strncat(msg_buffer, irc_msg, strlen(irc_msg));
            // Print only if there's a new line
            if (strnstr(msg_buffer, strlen(msg_buffer), "\n") != NULL) {
                dprintf(DP_HELP, "PRIVMSG %s :\003%d%s", qw_channel, color, msg_buffer);
                msg_buffer[0] = 0;
            }
        }
        nfree(irc_msg);
    }
}

/*
====================
qw_cleantext_init
Initializes QuakeWorld character encoding table.
This code is from the mvdsv project.
====================
 */
static void qw_cleantext_init(void) {
    int i;

    for (i = 0; i < 32; i++)
        qw_char_tbl[i] = qw_char_tbl[i + 128] = '#';
    for (i = 32; i < 128; i++)
        qw_char_tbl[i] = qw_char_tbl[i + 128] = i;

    // Special cases
    qw_char_tbl[10] = 10;
    qw_char_tbl[13] = 13;

    // Dot
    qw_char_tbl[5] = qw_char_tbl[14] = qw_char_tbl[15] = qw_char_tbl[28] = qw_char_tbl[46] = '.';
    qw_char_tbl[5 + 128] = qw_char_tbl[14 + 128] = qw_char_tbl[15 + 128] = qw_char_tbl[28 + 128] = qw_char_tbl[46 + 128] = '.';

    // Numbers
    for (i = 18; i < 28; i++)
        qw_char_tbl[i] = qw_char_tbl[i + 128] = i + 30;

    // Brackets
    qw_char_tbl[16] = qw_char_tbl[16 + 128] = '[';
    qw_char_tbl[17] = qw_char_tbl[17 + 128] = ']';

    // Left arrow
    qw_char_tbl[127] = '>';
    // Right arrow
    qw_char_tbl[141] = '<';

    // '-'
    qw_char_tbl[30] = qw_char_tbl[129] = qw_char_tbl[30 + 128] = '-';
    qw_char_tbl[29] = qw_char_tbl[29 + 128] = qw_char_tbl[128] = '-';
    qw_char_tbl[31] = qw_char_tbl[31 + 128] = qw_char_tbl[130] = '-';
}

/*
==============
qw_cleantext
Gets rid of QuakeWorld's character encoding in order to display the text
cleanly in IRC.
==============
 */
static void qw_cleantext(char *text) {
    for (; *text; text++) {
        *text = qw_char_tbl[(unsigned char)*text];
        // Remove double newlines
        if (*text == '\n') {
            if (text + 1) {
                if (*(text + 1) == '\n')
                    *text = ' ';
            }
        }
    }
}

/*
==============
has_qflag
Checks whether or not an eggdrop user has the chanflag +Q 
==============
 */
static int has_qflag(char *handle, char *channel) {
    struct userrec *user = NULL;
    struct chanuserrec *chanuser;

    // Check for user record
    if (!(user = get_user_by_handle(userlist, handle)))
        return 0;
    // Check for channel-specific flags
    if (!(chanuser = get_chanrec(user, channel)))
        return 0;
    // Check for chanflag 'Q'
    if (!(chanuser->flags_udef & UFLAG_QWADMIN))
        return 0;

    return 1;
}

/*
==============
qw_eggdrop_malloc
Allocate memory using eggdrop nmalloc function
==============
 */
void* qw_eggdrop_malloc(int size) {
    return nmalloc(size);
}

/*
==============
qw_eggdrop_free
Allocate memory using eggdrop nfree function
==============
 */
void qw_eggdrop_free(void* pointer) {
    nfree(pointer);
}
