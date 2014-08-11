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

#ifndef QW_COMMON_H
#define	QW_COMMON_H

/*
 * Includes
 */
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define __USE_GNU // Required for resource calculation purposes 

#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/uio.h>

#include <sys/resource.h>
#include <errno.h>

#include <openssl/sha.h>

/*
 * Data type defines
 */

typedef enum {false, true} bool;
typedef unsigned char byte;

/*
 * Command string defines
 */

#define	MAX_STRING_CHARS	1024            // max length of a string passed to Cmd_TokenizeString
#define	MAX_STRING_TOKENS	80		// max tokens resulting from Cmd_TokenizeString
#define	MAX_TOKEN_CHARS		512		// max length of an individual token
#define	MAX_OSPATH		128		// max length of a filesystem pathname

/*
 * General networking defines
 */

#define	QW_HEADER_LEN           8
#define	MAX_MSG_LEN             1450            // max length of of a network message
#define	MAX_UDP_PACKET          8192
#define	MAX_PRINT_MSG           4096
#define	MAX_INFO_STRING         196
#define	MAX_SERVERINFO_STRING	512
#define	QW_PROTOCOL_VERSION	28
#define QW_FPS                  5

/*
 * Supported out-of-band network messages
 */

#define	CHALLENGE_RESPONSE	'c'
#define	CONNECTION_RESPONSE	'j'
#define	OOB_PING		'k'          
#define	OOB_ACK			'l'
#define	OOB_PRINT		'n'        

/*
 * Eggdrop stuff
 */

#define VER1 1
#define VER2 0
bool qw_running;                        // Is the QuakeWorld thread running?
bool print_ver_info;                    // Print version info on connecting?

float res_last_calc;                    // When last resource usage calculation was done
extern pthread_mutex_t qw_mutex;        // Mutex used to lock shared tcl variables

// TCL variables:
extern char qw_name[25];                // Bot's in-game name
extern char qw_server[100];             // QuakeWorld server ip
extern int qw_server_port;              // QuakeWorld server port
extern char qw_password[100];           // QuakeWorld server password
extern char qw_rcon_password[100];      // QuakeWorld server rcon password
extern int qw_encrypt_rcon;             // Whether or not to encrypt rcon messages
extern int qw_rate;                     // Same as "rate" in QuakeWorld
extern int qw_topcolor;                 // Same as "topcolor" in QuakeWorld
extern int qw_bottomcolor;              // Same as "bottomcolor" in QuakeWorld
extern int qw_msgmode;                  // Same as "msg" in QuakeWorld
extern char qw_map[40];                 // Current map

extern int color_statusmessage;         // Status message color in IRC
extern int color_centerprint;           // Centerprint message color in IRC
extern int color_chattext;              // Chat message color in IRC
extern int color_normaltext;            // Default message color in IRC

// Other shared stuff
extern void irc_print(char* msg, int color);
extern char irc_msg_buf[MAX_PRINT_MSG]; // Holding area for irc-to-qw chat
extern long qw_maxrss;                  // Amount of memory used by QW thread
struct rusage qw_rusage;                // QuakeWorld thread resource usage

/*
 * QuakeWorld connection states
 */

typedef enum {
    disconnected,                       // Not connected to a server
    connected,                          // Connection response received
    processing,                         // Attempting to join the game
    active,                             // In-game
} constate_t;

/*
 * Networking message enums, according to QW protocol v28.
 */

// server to client
typedef enum {
    svc_bad,
    svc_nop,
    svc_disconnect,
    svc_updatestat,     // [byte] [byte]
    svc_version,        // [long] server version
    svc_setview,        // [short] entity number
    svc_sound,
    svc_time,           // [float] server time
    svc_print,          // [byte] id [string] null terminated string
    svc_stufftext,      // [string] stuffed into client's console buffer. the string should be \n terminated
    svc_setangle,       // [angle3] set the view angle to this absolute value
    svc_serverdata,     // [long] protocol ...
    svc_lightstyle,     // [byte] [string]
    svc_updatename,     // [byte] [string]
    svc_updatefrags,    // [byte] [short]
    svc_clientdata,     // <shortbits + data>
    svc_stopsound, 
    svc_updatecolors,   // [byte] [byte] [byte]
    svc_particle,       // [vec3] <variable>
    svc_damage,
    svc_spawnstatic,
    svc_spawnbinary,
    svc_spawnbaseline,
    svc_temp_entity,    // variable
    svc_setpause,       // [byte] on / off
    svc_signonnum,      // [byte]  used for the signon sequence
    svc_centerprint,    // [string] to put in center of the screen
    svc_killedmonster,
    svc_foundsecret,
    svc_spawnstaticsound, // [coord3] [byte] samp [byte] vol [byte] aten
    svc_intermission,   // [vec3_t] origin [vec3_t] angle
    svc_finale,         // [string] text
    svc_cdtrack,        // [byte] track
    svc_sellscreen,
    svc_smallkick,      // set client punchangle to 2
    svc_bigkick,        // set client punchangle to 4
    svc_updateping,     // [byte] [short]
    svc_updateentertime, // [byte] [float]
    svc_updatestatlong, // [byte] [long]
    svc_muzzleflash,    // [short] entity
    svc_updateuserinfo, // [byte] slot [long] uid, [string] userinfo
    svc_download,       // [short] size [size bytes]
    svc_playerinfo,     // variable
    svc_nails,          // [byte] num [48 bits] xyzpy 12 12 12 4 8 
    svc_chokecount,     // [byte] packets choked
    svc_modellist,      // [strings]
    svc_soundlist,      // [strings]
    svc_packetentities, // [...]
    svc_deltapacketentities, // [...]
    svc_maxspeed,       // maxspeed change, for prediction
    svc_entgravity,     // gravity change, for prediction
    svc_setinfo,        // setinfo on a client
    svc_serverinfo,     // serverinfo
    svc_updatepl,       // [byte] [byte]
} svc_t;

// client to server
typedef enum {
    clc_bad,
    clc_nop,
    clc_doublemove,
    clc_move,           // [[usercmd_t]
    clc_stringcmd,      // [string] message
    clc_delta,          // [byte] sequence number, requests delta compression of message
    clc_tmove,          // teleport request, spectator only
    clc_upload,         // teleport request, spectator only
} clc_t;

/*
 * Linked lists for userinfo and serverinfo strings
 */

typedef struct {
    struct infonode_t* next;
    char key[64];
    char value[64];
} infonode_t;

infonode_t* userinfo_root;              // Root node for userinfo "tree"
infonode_t* serverinfo_root;            // Root node for serverinfo "tree"

/*
 * Networking structs
 */

typedef struct {
    // This union is used for type-punning
    union {
        int as_int;
        byte as_byte[4];
    } ip;
    unsigned short port;
    unsigned short pad;
} netadr_t;

typedef struct {
    bool overflowed; // Terminate connection in case of overflow
    byte *data;
    int max_size;
    int cur_size;
} netbuf_t;

typedef struct {
    netadr_t remote_address;
    int qport;

    struct {
        float time;                     // Time last packet was received
        int seq;                        // Sequence number of last received packet
        byte rel_flag;                  // Reliability flag of last received reliable message (0/1)
        int remote_acked_seq;           // Last acknowledgement from server
        int remote_acked_rel_flag;      // Last acknowledged reliability flag from server
    } last_recv;

    struct {
        float time;                     // Time last packet was sent
        int seq;                        // Sequence number of last sent packet
        int last_rel_seq;               // Sequence number of last sent reliable message
        byte rel_flag;                  // Reliability flag of last sent reliable message (0/1)
    } last_sent;

    // Reliable staging and holding areas
    netbuf_t message; // Writing buffer to send to server
    byte message_buf[MAX_MSG_LEN];

    int reliable_length;
    byte reliable_buf[MAX_MSG_LEN];     // Unacknowledged reliable message

} netchan_t;

typedef struct {
    uint32_t qport;                         // Qport number for the current server
    int32_t challenge;                      // Challenge number for the current server
    int32_t user_id;                        // User id of the irc bot
    int32_t server_id;                      // Current server id
    char *game;                             // Current gamedir (e.g. id1))
    uint8_t player_num;                     // Player number of the irc bot
    char map[40];                           // Current map name
    char serverinfo[MAX_SERVERINFO_STRING]; // Serverinfo for the current server
    float connect_time;                     // Time last connection was mode
    float realtime;                         // Current time
} game_instance_t;

game_instance_t qw;
constate_t con_state;
netchan_t netchan;
    
extern netbuf_t net_message;
extern netadr_t net_from;

/*
 * qw_main.c functions
 */

void *qw_init(void *arg);
void qw_frame();

/*
 * qw_net.c functions
 */

void con_init(int port);
void con_clear(void);
void udp_transmit(int length, void *data, netadr_t to);
bool udp_process(void);
bool netadr_compare(netadr_t a, netadr_t b);

void net_oob_transmit(netadr_t adr, int length, char *data);
void net_oob_process(void);

void net_request_challenge(void);
void net_reconnect(void);
void net_disconnect(void);

void netchan_keepalive(void);
void netchan_transmit(netchan_t *chan, int length, byte *data);
bool netchan_process(netchan_t *chan);

void net_parse_command(void);
int net_console_execute(char *cmd_str);
void exec_serverdata(void);
void exec_stufftext(char *stuff_cmd);
void exec_sound(void);
void exec_rcon(char* cmd);
void exec_packet(void);
void exec_fullserverinfo(void);
void exec_updateuserinfo(void);
void exec_setinfo(infonode_t* node);
void exec_chat(char *fmt, ...);

/*
 * qw_utils.c functions
 */

int net_read_count;
bool net_read_err;

void net_begin_read(void);
int net_read_bytes(int bytes);
char *net_read_string(bool break_on_nl);
void net_write_integer(netbuf_t *nb, int c, int bytes);
void net_write_string(netbuf_t *nb, char *s);
void net_skip_message();
void net_skip_bytes(int bytes);

void buf_clear(netbuf_t *buf);
void buf_write_string(netbuf_t *buf, char *data);
void buf_init(netbuf_t *buf, byte *data, int length);
void *buf_allocate(netbuf_t *buf, int length);
void buf_write(netbuf_t *buf, void *data, int length);

void infostring_init(void);
infonode_t* infostring_add_node(infonode_t* pos, char* key, char* val);
void infostring_update_node(infonode_t* pos, char* key, char* val);
void infostring_print(infonode_t* pos, char* istr);
void infostring_from_string(infonode_t* pos, char *info);
void infostring_clear(infonode_t* pos, bool free_root);
bool infostring_check_input(char* key, char* value);

short byteswap_short(short number);
char *bin2hex(unsigned char *d);
int get_time(void);

/*
 * qw_parser.c functions
 */

char *parser_args(void);
char *parser_argv(int arg);
int parser_argc(void);
void parser_tokenize(char *text, bool macro_expand);
char *strnstr(char *haystack, int hlen, char *needle);

#endif	/* QW_COMMON_H */

