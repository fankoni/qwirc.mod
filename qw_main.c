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

#include "qw_common.h"

/*
==============
qw_init
Starts the QWCL loop
==============
 */
void *qw_init(void *arg) {
    // Set up QuakeWorld UDP connection
    pthread_mutex_lock(&qw_mutex);
    con_init(qw_server_port);
    pthread_mutex_unlock(&qw_mutex);

    // Set up QuakeWorld player infostring
    infostring_init();

    // Start connecting to the server
    qw.connect_time = -999;
    net_request_challenge();

    // This will force resource usage calculation on next qw_frame() iteration
    res_last_calc = -60000;

    sleep(1); // Wait for the server
    while (1) {
        get_time();
        qw_frame();
    }

    return 0;
}

/*
==============
qw_frame
Main QuakeWorld loop, executed QW_FPS times per second
==============
 */
void qw_frame() {
    netbuf_t buf;
    usleep((1.0f / QW_FPS) * 1000 * 1000);

    // Keep the connection live. we won't get data unless we also send some..
    if (con_state == active)
        netchan_keepalive();

    while (udp_process()) {
        // Out-of-band message
        if (*(int *) net_message.data == -1) {
            net_oob_process();
            continue;
        }

        // Ignore very small packets
        if (net_message.cur_size < 8)
            continue;

        // Packet from the server
        if (!netchan_process(&netchan))
            continue; // Rejected packet

        net_parse_command();
        buf_clear(&net_message);
    }

    // Check for reliable retransmit
    if (con_state <= connected || netchan.message.cur_size) {
        if (netchan.message.cur_size || qw.realtime - netchan.last_sent.time > 1000) {
            byte data[128];
            buf_init(&buf, data, sizeof (data));
            netchan_transmit(&netchan, buf.cur_size, buf.data);
            buf_clear(&netchan.message);
        }
    }

    // Get a new challenge if needed
    if (con_state == disconnected)
        net_request_challenge();

    // Timeout after 30 seconds of silence
    if (qw.realtime - netchan.last_recv.time > 30000 && con_state >= connected) {
        irc_print("Connection timed out. Exiting thread.\n", color_statusmessage);
        pthread_mutex_lock(&qw_mutex);
        qw_running = false;
        pthread_mutex_unlock(&qw_mutex);
    }

    // Check if thread termination was requested. Possible reasons are numerous.
    pthread_mutex_lock(&qw_mutex);
    if (!qw_running && con_state >= connected) {
        pthread_mutex_unlock(&qw_mutex);
        irc_print("Disconnected.\n", color_statusmessage);
        exec_chat("Bye bye!");
        net_disconnect();
        con_clear();
        pthread_exit(0);
    } else if (con_state == active && irc_msg_buf[0]) {
        // Send chat messages to server
        exec_chat(irc_msg_buf);
        irc_msg_buf[0] = 0;
    }

    // Calculate resource usage every 60 seconds
    if (qw.realtime - res_last_calc > 60000) {
        if (!getrusage(RUSAGE_THREAD, &qw_rusage))
            qw_maxrss = qw_rusage.ru_maxrss;
        res_last_calc = qw.realtime;
    }

    pthread_mutex_unlock(&qw_mutex);
}
