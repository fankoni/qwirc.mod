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

netadr_t net_local_adr;                         // Local host
netadr_t net_from;                              // Remote host
netbuf_t net_message;                           // Network message
int net_socket;                                 // UDP socket
byte net_message_buffer[MAX_UDP_PACKET];        // Network message buffer

/*
 * Network channel functions for connection-oriented transmission
 */

/*
===============
netchan_keepalive
Sends some fake game data to the server so that we also get some data back
===============
 */
void netchan_keepalive(void) {
    // The client command tmove is harmless enough.
    net_write_integer(&netchan.message, clc_tmove, 1);
    // The server expects three short integers as coordinates for tmove.
    // Might just use the value 1 for each, doesn't matter.
    net_write_integer(&netchan.message, 1, 2);
    net_write_integer(&netchan.message, 1, 2);
    net_write_integer(&netchan.message, 1, 2);
}

/*
===============
net_oob_transmit
Transmits an out-of-band datagram.
================
 */
void net_oob_transmit(netadr_t adr, int length, char *data) {
    netbuf_t send;
    byte send_buf[MAX_MSG_LEN + QW_HEADER_LEN];

    // write the packet header
    send.data = send_buf;
    send.max_size = sizeof (send_buf);
    send.cur_size = 0;

    net_write_integer(&send, 0xFFFFFFFF, 4);
    buf_write(&send, data, length);

    // send the datagram
    udp_transmit(send.cur_size, send.data, adr);
}

/*
==============
netchan_setup
Sets up the network channel used for all connection-oriented traffic
==============
 */
void netchan_setup(netchan_t *chan, netadr_t adr, int qport) {
    memset(chan, 0, sizeof (*chan));

    chan->remote_address = adr;
    chan->last_recv.time = qw.realtime;

    chan->message.data = chan->message_buf;
    chan->message.max_size = sizeof (chan->message_buf);

    chan->qport = qport;
}

/*
===============
netchan_transmit
Transmits all outgoing connection-oriented traffic. Handles reliability.
================
 */
void netchan_transmit(netchan_t *chan, int length, byte *data) {
    netbuf_t send;
    byte send_buf[MAX_MSG_LEN + QW_HEADER_LEN];
    bool rel_payload = false;
    uint32_t header_seq, header_ack;

    // Check for buffer overflow
    if (chan->message.overflowed) {
        irc_print("Fatal error: outgoing message overflow!\n", color_statusmessage);
        // Signal thread to be shut down
        pthread_mutex_lock(&qw_mutex);
        qw_running = false;
        pthread_mutex_unlock(&qw_mutex);
        return;
    }

    // Check if last reliable transmission was lost. If that's the case,
    // retransmit it.
    if (chan->last_recv.remote_acked_seq > chan->last_sent.last_rel_seq
            && chan->last_recv.remote_acked_rel_flag != chan->last_sent.rel_flag)
        rel_payload = true;

    // If the reliable transmit buffer is empty, copy the current message out
    if (!chan->reliable_length && chan->message.cur_size) {
        memcpy(chan->reliable_buf, chan->message_buf, chan->message.cur_size);
        chan->reliable_length = chan->message.cur_size;
        chan->message.cur_size = 0;
        chan->last_sent.rel_flag ^= 1;
        rel_payload = true;
    }

    send.data = send_buf;
    send.max_size = sizeof (send_buf);
    send.cur_size = 0;

    // Store header integers
    header_seq = chan->last_sent.seq | (rel_payload << 31);
    header_ack = chan->last_recv.seq | (chan->last_recv.rel_flag << 31);

    // Update stats
    chan->last_sent.seq++;
    chan->last_sent.time = qw.realtime;

    // Transform to big-endian and write according to the QW protocol
    net_write_integer(&send, header_seq, 4);
    net_write_integer(&send, header_ack, 4);
    net_write_integer(&send, qw.qport, 2);

    // Copy the reliable message to the packet first, right after the header
    if (rel_payload) {
        buf_write(&send, chan->reliable_buf, chan->reliable_length);
        chan->last_sent.last_rel_seq = chan->last_sent.seq;
    }

    // Add the unreliable part if there is still space left
    if (send.max_size - send.cur_size >= length)
        buf_write(&send, data, length);

    // Send datagram
    udp_transmit(send.cur_size, send.data, chan->remote_address);
}

/*
=================
netchan_process
Processes all incoming connection-oriented traffic. Handles reliability.
=================
 */
bool netchan_process(netchan_t *chan) {
    unsigned header_seq, header_ack;
    unsigned rel_acked_flag, rel_payload;

    if (!netadr_compare(net_from, chan->remote_address))
        return false;

    // Read packet header: packet sequence and acknowledged sequence.
    net_begin_read();
    header_seq = net_read_bytes(4);
    header_ack = net_read_bytes(4);

    // Get reliable flags
    rel_payload = header_seq >> 31;
    rel_acked_flag = header_ack >> 31;

    // Remove reliable flags to get the sequence numbers
    header_seq &= ~(1 << 31);
    header_ack &= ~(1 << 31);

    // Discard stale or duplicated packets
    if (header_seq <= chan->last_recv.seq)
        return false;

    // If the current outgoing reliable message has been acknowledged
    // clear the buffer to make way for the next
    if (rel_acked_flag == chan->last_sent.rel_flag)
        chan->reliable_length = 0;

    // If this message contains a reliable message, bump rel_flag
    chan->last_recv.seq = header_seq;
    chan->last_recv.remote_acked_seq = header_ack;
    chan->last_recv.remote_acked_rel_flag = rel_acked_flag;
    if (rel_payload)
        chan->last_recv.rel_flag ^= 1;
    chan->last_recv.time = qw.realtime;

    return true;
}

/*
 * Network address functions
 */

/*
=====================
netadr_to_saddr
Converts a netadr_t address to sockaddr_in
=====================
 */
void netadr_to_saddr(netadr_t *a, struct sockaddr_in *s) {
    memset(s, 0, sizeof (struct sockaddr_in));
    s->sin_family = AF_INET;

    s->sin_addr.s_addr = a->ip.as_int;
    s->sin_port = a->port;
}

/*
=====================
saddr_to_netadr
Converts a sockaddr_in address to netadr_t
=====================
 */
void saddr_to_netadr(struct sockaddr_in *s, netadr_t *a) {
    a->ip.as_int = s->sin_addr.s_addr;
    a->port = s->sin_port;
}

/*
=====================
 netadr_compare
 Compares two network addresses
=====================
 */
bool netadr_compare(netadr_t a, netadr_t b) {
    if (a.ip.as_int == b.ip.as_int && a.port == b.port)
        return true;
    return false;
}

/*
=====================
netadr_to_string
Converts netadr_t to network address string
=====================
 */
char *netadr_to_string(netadr_t a) {
    static char adr_str[32];

    snprintf(adr_str, sizeof(adr_str), "%i.%i.%i.%i:%i", a.ip.as_byte[0], a.ip.as_byte[1],
            a.ip.as_byte[2], a.ip.as_byte[3], ntohs(a.port));

    return adr_str;
}

/*
=============
string_to_netadr
Converts a network address string to netadr_t
=============
 */
bool string_to_netadr(char *s, netadr_t *a) {
    struct hostent *h;
    struct sockaddr_in saddr;
    char *colon;
    char copy[128];


    memset(&saddr, 0, sizeof (saddr));
    saddr.sin_family = AF_INET;

    saddr.sin_port = 0;

    strncpy(copy, s, sizeof(copy));
    // strip off a trailing :port if present
    for (colon = copy; *colon; colon++)
        if (*colon == ':') {
            *colon = 0;
            saddr.sin_port = htons(atoi(colon + 1));
        }

    if (copy[0] >= '0' && copy[0] <= '9') {
        *(int *) &saddr.sin_addr = inet_addr(copy);
    } else {
        if (!(h = gethostbyname(copy)))
            return 0;
        *(int *) &saddr.sin_addr = *(int *) h->h_addr_list[0];
    }

    saddr_to_netadr(&saddr, a);

    return true;
}


/*
=====================
netadr_local_setup
Sets up local network address struct
=====================
 */
void netadr_local_setup(void) {
    char buff[MAXHOSTNAMELEN];
    char err_str[100];
    struct sockaddr_in address;
    int namelen;

    gethostname(buff, MAXHOSTNAMELEN);
    buff[MAXHOSTNAMELEN - 1] = 0;

    string_to_netadr(buff, &net_local_adr);

    namelen = sizeof (address);
    if (getsockname(net_socket, (struct sockaddr *) &address, (socklen_t*) & namelen) == -1) {
        snprintf(err_str, sizeof (err_str), "Error while getting local address: getsockname() returned %s.\n", strerror(errno));
        irc_print(err_str, color_statusmessage);
    }
    net_local_adr.port = address.sin_port;
}

/*
 * UDP layer, basic network transmission
 */


/*
=====================
udp_process
Processes incoming UDP datagrams
=====================
 */
bool udp_process(void) {
    int ret;
    struct sockaddr_in from;
    int fromlen;

    fromlen = sizeof (from);
    ret = recvfrom(net_socket, net_message_buffer, sizeof (net_message_buffer), 0, (struct sockaddr *) &from, (socklen_t *) & fromlen);
    if (ret == -1) {
        if (errno == EWOULDBLOCK) {
            return false;
        }
        if (errno == ECONNREFUSED) {
            return false;
        }
        printf("Error: recvfrom returned %s. (udp_process())\n", strerror(errno));
        return false;
    }
    net_message.cur_size = ret;
    saddr_to_netadr(&from, &net_from);

    return ret;
}

/*
=====================
udp_transmit
Transmits outgoing UDP datagrams
=====================
 */
void udp_transmit(int length, void *data, netadr_t to) {
    int ret;
    struct sockaddr_in addr;

    if (!to.ip.as_int)
        return;

    netadr_to_saddr(&to, &addr);
    ret = sendto(net_socket, data, length, 0, (struct sockaddr *) &addr, sizeof (addr));
    if (ret == -1) {
        if (errno == EWOULDBLOCK)
            return;
        if (errno == ECONNREFUSED)
            return;
        printf("Error: sendto() returned: %s. (udp_transmit())\n", strerror(errno));

    }
}

/*
=====================
udp_open
Opens the UDP socket used for all communications
=====================
 */
int udp_open(int port) {
    int qw_socket = -1;
    struct sockaddr_in address;
    char err_str[100];
    char non_blocking = 1;

    if ((qw_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        snprintf(err_str, sizeof (err_str), "Error: socket() returned %s. (udp_open())\n", strerror(errno));
        irc_print(err_str, color_statusmessage);
    }
    if (ioctl(qw_socket, FIONBIO, &non_blocking) == -1) {
        snprintf(err_str, sizeof (err_str), "Error: ioctl() returned %s. (udp_open())\n", strerror(errno));
        irc_print(err_str, color_statusmessage);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;

    address.sin_port = htons((short) port);

    if (bind(qw_socket, (void *) &address, sizeof (address)) == -1) {
        snprintf(err_str, sizeof (err_str), "Error: bind() returned %s. (udp_open())\n", strerror(errno));
        irc_print(err_str, color_statusmessage);
    }

    return qw_socket;
}

/*
====================
con_init
Initializes variables for connection
====================
 */
void con_init(int port) {
    // Open and set up the UDP socket used for QuakeWorld communications
    net_socket = udp_open(port);

    // Init the message buffer
    net_message.max_size = sizeof (net_message_buffer);
    net_message.data = net_message_buffer;

    // Get local network address and name
    netadr_local_setup();

    con_state = disconnected;
    irc_print("QuakeWorld UDP Initialized.\n", color_statusmessage);

    // Assign a random qport number
    qw.qport = ((int) (getpid() + getuid() * 1000) * time(NULL)) & 0xFFFF;

}

/*
====================
con_clear
Clears and frees networking structs and the UDP socket
====================
 */
void con_clear(void) {
    close(net_socket);
    memset(&netchan, 0, sizeof (netchan_t));
    infostring_clear(userinfo_root, true);
    infostring_clear(serverinfo_root, true);
}

/*
 * Command handling
 */

/*
=====================
net_parse_command
Ṕarses incoming commands received via netchan
=====================
 */
void net_parse_command(void) {
    int cmd;
    bool read_msg = true;

    while (1 && read_msg) {
        if (net_read_err) {
            printf("Error: Bad server message. (net_parse_command())\n");
            break;
        }

        // Get the command byte
        cmd = net_read_bytes(1);
        if (cmd == -1) {
            net_read_count++;
            break;
        }

        // Act on command
        switch (cmd) {
            case svc_nop:
                break;

            case svc_disconnect:
                if (con_state == connected)
                    irc_print("Server disconnected\nServer version may not be compatible\n", color_statusmessage);
                else
                    irc_print("Server disconnected\n", color_statusmessage);
                break;

            case svc_print:
                net_skip_bytes(1);
                
                char cur_line[MAX_STRING_CHARS];
                // Get string
                char* original = net_read_string(false);
                // Split by newlines
                char* lines = strtok(original, "\n");

                while (lines != NULL) {
                    // Add "\n" because the result of strtok doesn't include the token
                    snprintf(cur_line, strlen(lines) + 2, "%s\n", lines);
                    irc_print(cur_line, color_chattext);

                    // Get next line
                    lines = strtok(NULL, "\n");
                }
                break;

            case svc_centerprint:
                net_read_string(false);
                break;

            case svc_stufftext:
                exec_stufftext(net_read_string(false));
                break;
                
            case svc_serverdata:
                exec_serverdata();
                break;

            case svc_finale:
                irc_print(net_read_string(false), color_statusmessage);
                break;

            case svc_updateuserinfo:
                exec_updateuserinfo();
                break;

            case svc_setinfo:
                exec_setinfo(userinfo_root);
                break;

            case svc_serverinfo:
                exec_setinfo(serverinfo_root);
                break;
                
            case svc_sound:
                exec_sound();
                break;
                
            // Skip some messages entirely
            case svc_playerinfo:
            case svc_packetentities:
            case svc_deltapacketentities:
                net_skip_message();
                read_msg = false;
                break;
                
            // With the rest we skip just enough bytes to read the next command
            case svc_cdtrack:
                net_skip_bytes(1);
                break;
                
            case svc_lightstyle:
                net_skip_bytes(1);
                net_read_string(false);
                break;
                
            case svc_updatepl:
            case svc_updatestat:
            case svc_stopsound:
                net_skip_bytes(2);
                break;
                
            case svc_updatefrags:
            case svc_updateping:
            case svc_setangle:
                net_skip_bytes(3);
                break;
                
            case svc_entgravity:
            case svc_maxspeed:
            case svc_time:
                net_skip_bytes(4);
                break;
                
            case svc_updateentertime:
            case svc_updatestatlong:
                net_skip_bytes(5);
                break;
                
            case svc_temp_entity:
                net_skip_bytes(7);
                break;
                
            default:
                printf("Received unimplemented server message: %d\n", cmd);
                net_skip_message();
                read_msg = false;
                break;
        }
    }
}

/*
=============
net_console_execute
Executes QuakeWorld console commands and forwards unimplemented ones to the server
=============
*/
int net_console_execute(char *cmd_str) {
    while (*cmd_str && *cmd_str == '\n')
        cmd_str++;

    parser_tokenize(cmd_str, true);
    
    if (!strcmp(parser_argv(0), "cmd")) {
        // Send command to the server
        net_write_integer(&netchan.message, clc_stringcmd, 1);
        buf_write_string(&netchan.message, parser_args());
        return 1;
    } else if (!strcmp(parser_argv(0), "changing")) {
        // This will force reconnect on map change
        con_state = connected; 
        return 1;
    } else if (!strcmp(parser_argv(0), "reconnect")) {
        // Reconnect request
        net_reconnect();
        return 1;
    } else if (!strcmp(parser_argv(0), "disconnect")) {
        // Stuffed disconnect requests are ignored
        return 1;
    } else if (!strcmp(parser_argv(0), "packet")) {
        // Send a network packet with predetermined contents to a predetermined host
        exec_packet();
        return 1;
    } else if (!strcmp(parser_argv(0), "fullserverinfo")) {
        // Store complete server info
        exec_fullserverinfo();
        return 1;
    } else if (!strcmp(parser_argv(0), "wait") || !strcmp(parser_argv(0), "alias") || !strcmp(parser_argv(0), "sinfoset")
            || !strcmp(parser_argv(0), "ktx_sinfoset") || !strcmp(parser_argv(0), "on_spec_enter_ffa") || !strcmp(parser_argv(0), "play")) {
        // We don't need to act on these
        return 1;
    }
    else {
        if (cmd_str[0]) {
            // Unknown commands are forwarded back to the server.
            printf("Unknown command '%s', forwarding to server.\n", parser_argv(0));
            net_write_integer(&netchan.message, clc_stringcmd, 1);
            buf_write_string(&netchan.message, parser_argv(0));
            buf_write_string(&netchan.message, " ");
            buf_write_string(&netchan.message, parser_args());
        }
    }
    return 0;
}


/*
=============
exec_stufftext
Executes commands that were stuffed to client console by the server
=============
 */
void exec_stufftext(char *stuff_cmd) {
    char *cur_char = stuff_cmd;
    int cmd_begin_ptr = 0;

    while (cur_char - stuff_cmd < strlen(stuff_cmd)) {
        // Stop on newline, ";" or when out of characters
        for (; *cur_char && (*cur_char != '\n' && *cur_char != ';'); cur_char++);
        // Mark as end of command
        *(cur_char) = '\0'; 
        // Execute command string
        net_console_execute(stuff_cmd + cmd_begin_ptr);
        // Move pointers to the beginning of the next command string
        cmd_begin_ptr = (cur_char - stuff_cmd) + 1;
        cur_char++;
    }
}


/*
==============
exec_updateuserinfo
Updates userinfo according to a full userinfo string received from server
==============
 */
void exec_updateuserinfo(void) {
    net_skip_bytes(1);
    // Not used for anything at the moment, but stored anyway.
    qw.user_id = net_read_bytes(4);

    // Clear the tree but keep the root
    infostring_clear(userinfo_root, false);
    // Construct a new tree from the received string
    infostring_from_string(userinfo_root, net_read_string(false));
}

/*
==================
exec_serverdata
Processes a serverdata packet that is received when connecting
==================
 */
void exec_serverdata(void) {
    char temp_str[100] = "";
    int proto_ver;

    // Clear message
    buf_clear(&netchan.message);

    // Parse protocol version number
    proto_ver = net_read_bytes(4);
    if (proto_ver != QW_PROTOCOL_VERSION) {
        snprintf(temp_str, sizeof(temp_str), "Server returned protocol version %i, not %i. Aborting.", proto_ver, QW_PROTOCOL_VERSION);
        irc_print(temp_str, color_statusmessage);
        pthread_mutex_lock(&qw_mutex);
        qw_running = false;
        pthread_mutex_unlock(&qw_mutex);
    }

    qw.server_id = net_read_bytes(4);

    // Game directory
    qw.game = net_read_string(false);

    // Parse player slot, high bit means spectator
    qw.player_num = net_read_bytes(1);
    if (qw.player_num & 128) {
        qw.player_num &= ~128;
    }

    // Get the full level name
    strncpy(qw.map, net_read_string(false), sizeof(qw.map));

    // Movevars can be ignored
    net_skip_bytes(40);

    // Print the name of the current map in IRC
    snprintf(temp_str, 14 + sizeof(qw.map), "Current map: %s\n", qw.map);
    pthread_mutex_lock(&qw_mutex);
    // Store in shared variable as well (for !qmap IRC command)
    strncpy(qw_map, qw.map, sizeof(qw_map));
    pthread_mutex_unlock(&qw_mutex);
    irc_print(temp_str, color_statusmessage);

    // Now waiting for downloads, etc
    con_state = processing;
}

/*
==================
exec_fullserverinfo
Updates serverinfo according to a a fullserverinfo string sent by server
==================
 */
void exec_fullserverinfo(void) {
    if (parser_argc() != 2) {
        printf("Usage: fullserverinfo <complete info string>\n");
        return;
    }

    // Clear tree but keep the root
    infostring_clear(serverinfo_root, false);
    // Construct new tree from the received string
    infostring_from_string(serverinfo_root, parser_argv(1));

    // Join the game if this is the first fullserverinfo we got
    if (con_state != active) {
        char begin_cmd[10];
        net_write_integer(&netchan.message, clc_stringcmd, 1);
        snprintf(begin_cmd, sizeof(begin_cmd), "begin %d", qw.server_id);
        net_write_string(&netchan.message, begin_cmd);
        con_state = active;
        if (print_ver_info) {
            exec_chat("QuakeWorld eggdrop module %d.%d by aku.hasanen@kapsi.fi connected.", VER1, VER2, color_statusmessage);
            print_ver_info = false;
        }
    }
}

/*
====================
exec_packet
Sends an out-of-band packet according to server's request
====================
 */
void exec_packet(void) {
    char *msg;
    netadr_t adr;

    if (parser_argc() != 3) {
        printf("Usage: packet <destination> <contents>\n");
        return;
    }

    if (!string_to_netadr(parser_argv(1), &adr)) {
        printf("Error: Bad address. (exec_packet())\n");
        return;
    }

    msg = parser_argv(2);
    msg[strlen(msg)] = 0;
    net_oob_transmit(adr, strlen(msg), msg);
}

/*
==============
exec_setinfo
Updates an infonode according to server's request
==============
 */
void exec_setinfo(infonode_t* node) {
    char key[MAX_MSG_LEN];
    char value[MAX_MSG_LEN];

    net_skip_bytes(1);

    strncpy(key, net_read_string(false), sizeof (key) - 1);
    key[sizeof (key) - 1] = 0;
    strncpy(value, net_read_string(false), sizeof (value) - 1);
    key[sizeof (value) - 1] = 0;

    infostring_update_node(node, key, value);
}

/*
=================
net_reconnect
Reconnects to the server due to server or user request
=================
 */
void net_reconnect(void) {
    if (con_state == connected) {
        irc_print("Reconnecting...\n", color_statusmessage);
        net_write_integer(&netchan.message, clc_stringcmd, 1);
        net_write_string(&netchan.message, "new");
        return;
    }

    pthread_mutex_lock(&qw_mutex);
    if (!*qw_server) {
        pthread_mutex_unlock(&qw_mutex);
        irc_print("No server to reconnect to...\n", color_statusmessage);
        return;
    }
    pthread_mutex_unlock(&qw_mutex);

    net_disconnect();
    qw.connect_time = -999;
    net_request_challenge();
}

/*
==================
exec_sound
Basically just skips the svc_sound parameters
==================
 */
void exec_sound(void) {
    int channel = net_read_bytes(2);

    if (channel & (1 << 15))
        net_skip_bytes(1);
    if (channel & (1 << 14))
        net_skip_bytes(1);

    net_skip_bytes(7);
}

/*
==================
net_request_connection
Sends an out-of-band connection request that will enable
connection-oriented communication via netchan.
==================
 */
void net_request_connection(void) {
    netadr_t adr;
    char data[256];
    char userinfo[MAX_INFO_STRING] = "";

    if (con_state != disconnected)
        return;

    pthread_mutex_lock(&qw_mutex);
    if (!string_to_netadr(qw_server, &adr)) {
        printf("Bad server address!\n");
        qw.connect_time = -1;
        pthread_mutex_unlock(&qw_mutex);
        return;
    }

    if (adr.port == 0)
        adr.port = byteswap_short(qw_server_port);

    pthread_mutex_unlock(&qw_mutex);

    qw.connect_time = get_time();

    infostring_update_node(userinfo_root, "*ip", netadr_to_string(adr));
    infostring_print(userinfo_root, userinfo);

    snprintf(data, sizeof(data), "connect %i %i %i \"%s\"\n", QW_PROTOCOL_VERSION, 
            qw.qport, qw.challenge, userinfo);
    net_oob_transmit(adr, strlen(data), data);
}

/*
=================
net_connect
Starts the connecting process by asking for a challenge number
=================
 */
void net_request_challenge(void) {
    char irc_msg[64];
    netadr_t adr;

    if (qw.connect_time == -1)
        return;
    if (con_state != disconnected)
        return;
    if (qw.connect_time && qw.realtime - qw.connect_time < 5.0)
        return;

    pthread_mutex_lock(&qw_mutex);
    if (!string_to_netadr(qw_server, &adr)) {
        printf("Error: Bad server address. (net_connect())\n");
        qw.connect_time = -1;
        pthread_mutex_unlock(&qw_mutex);
        return;
    }

    if (adr.port == 0)
        adr.port = byteswap_short(qw_server_port);

    // For retransmit requests
    qw.connect_time = get_time(); 

    snprintf(irc_msg, sizeof(irc_msg), "Connecting to %s...\n", qw_server);
    pthread_mutex_unlock(&qw_mutex);

    irc_print(irc_msg, color_statusmessage);

    net_oob_transmit(adr, 13, "getchallenge\n");
}

/*
=====================
 exec_rcon
 Transmits remote console commands
 Encryption part is based on code from the mvdsv project.
=====================
 */
void exec_rcon(char* cmd) {
    char message[1024] = "";
    char cmds[1024] = "";
    char *hex_tmp;
    int i;
    SHA_CTX qw_ctx;
    SHA1_Init(&qw_ctx);
    unsigned char hash[SHA_DIGEST_LENGTH];

    pthread_mutex_lock(&qw_mutex);
    if (!qw_rcon_password[0]) {
        pthread_mutex_unlock(&qw_mutex);
        irc_print("You must set the tcl variable 'qw_rcon_password' before "
                "issuing an rcon command.\n", color_normaltext);
        return;
    }
    pthread_mutex_unlock(&qw_mutex);
    strncpy(cmds, cmd, strlen(cmd));

    if (qw_encrypt_rcon) {
        strncpy(message, "rcon ", 5);
        time_t client_time;
        char client_time_str[32] = "";
        char *commands;

        time(&client_time);
        for (client_time_str[0] = i = 0; i < sizeof (client_time); i++) {
            char tmp[3];
            snprintf(tmp, sizeof (tmp), "%02X", (unsigned int) ((client_time >> 
                    (i * 8)) & 0xFF));
            strncat(client_time_str, tmp, sizeof (client_time_str) - (strlen(client_time_str) -1));
        }

        SHA1_Update(&qw_ctx, (unsigned char *) "rcon ", 5);

        SHA1_Update(&qw_ctx, (unsigned char *) qw_rcon_password, strlen(qw_rcon_password));
        SHA1_Update(&qw_ctx, (unsigned char *) client_time_str, strlen(client_time_str));

        SHA1_Update(&qw_ctx, (unsigned char *) " ", 1);

        // Split by space                 
        commands = strtok(cmds, " ");

        while (commands != NULL) {
            SHA1_Update(&qw_ctx, (unsigned char *) commands, strlen(commands));
            SHA1_Update(&qw_ctx, (unsigned char *) " ", 1);
            commands = strtok(NULL, " ");
        }

        SHA1_Final(hash, &qw_ctx);
        hex_tmp = bin2hex(hash);
        strncat(message, hex_tmp, sizeof (message) - (strlen(message) - 1));
        strncat(message, client_time_str, sizeof (message) - (strlen(message) - 1));
        strncat(message, " ", sizeof (message));
        strncat(message, cmd, sizeof (message) - (strlen(message) - 1));
        strncat(message, " ", sizeof (message));
    } else
        snprintf(message, 6 + sizeof(qw_rcon_password) + strlen(cmd), "rcon %s %s", qw_rcon_password, cmd);


    if (con_state >= connected)
        net_oob_transmit(netchan.remote_address, strlen(message) + 1, message);
}

/*
=====================
net_disconnect
Sends a disconnect message to the server
=====================
 */
void net_disconnect(void) {
    qw.connect_time = -1;
    
    if (con_state != disconnected) {
        byte drop_cmd[] = {clc_stringcmd, ' ', 'd', 'r', 'o', 'p'};
        netchan_transmit(&netchan, 6, drop_cmd);
        netchan_transmit(&netchan, 6, drop_cmd);
        netchan_transmit(&netchan, 6, drop_cmd);
        con_state = disconnected;
    }
}

/*
=====================
net_oob_process
Parses incoming out-of-band datagrams
=====================
 */
void net_oob_process(void) {
    int cmd;
    char *tmp;
    char reply[2];

    net_begin_read();
    // Skip the OOB header
    net_read_bytes(4); 
    // Read the command byte
    cmd = net_read_bytes(1);
    
    switch (cmd) {
        case CONNECTION_RESPONSE:
            // Check if already connected	
            if (con_state >= connected)
                return;
            // Open network channel for connection-oriented transmission
            netchan_setup(&netchan, net_from, qw.qport);
            net_write_integer(&netchan.message, clc_stringcmd, 1);
            net_write_string(&netchan.message, "new");
            con_state = connected;
            irc_print("Connected.\n", color_statusmessage);
            break;
        case CHALLENGE_RESPONSE:
            tmp = net_read_string(false);
            qw.challenge = atoi(tmp);
            // Send out-of-band connect packet
            net_request_connection();
            break;
        case OOB_PRINT:
            tmp = net_read_string(false);
            printf("Received out-of-band print:\n");
            printf("%s", tmp);
            break;
        case OOB_PING:
            reply[0] = OOB_ACK;
            reply[1] = 0;
            printf("Received out-of-band ping. Acknowledging.\n");
            net_oob_transmit(net_from, 2, reply);
            break;
        default:
            printf("Ignored unknown out-of-band command: %c.\n", cmd);
    }
}

/*
==============
exec_chat
Transmits in-game chat messages
==============
 */
void exec_chat(char *fmt, ...) {
    va_list argptr;
    char msg[MAX_PRINT_MSG];
    char msg2[MAX_PRINT_MSG];

    va_start(argptr, fmt);
    vsprintf(msg, fmt, argptr);
    va_end(argptr);

    snprintf(msg2, 5 + strlen(msg), "say %s\n", msg);
    net_write_integer(&netchan.message, clc_stringcmd, 1);
    net_write_string(&netchan.message, msg2);

}


