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

/*
 * Network message writing and reading
*/

/*
=============
net_write_integer
Writes an integer to network message buffer, first converting it to big-endian.
=============
*/
void net_write_integer(netbuf_t *sb, int c, int bytes) {
    byte *buf;
    int i;

    buf = buf_allocate(sb, bytes);

    for (i = 0; i < bytes; i++)
        buf[i] = (c >> (8 * i)) & 0xFF;

    i++;
    buf[i] = c >> (8 * (i - 1));
}

/*
=============
net_write_string
Writes a string to network message buffer.
=============
*/
void net_write_string(netbuf_t *sb, char *s) {
    if (!s || !*s)
        buf_write(sb, "", 1);
    else
        buf_write(sb, s, strlen(s) + 1);
}

/*
=============
net_begin_read
Sets everything up for reading a new network message
=============
*/
void net_begin_read(void) {
    net_read_count = 0;
    net_read_err = false;
}

/*
=============
net_read_bytes
Reads 1-4 bytes from the network message buffer and converts to little-endian.
=============
*/
int net_read_bytes(int bytes) {
    uint32_t buf = 0;
    int8_t i;

    // Only up to 4 bytes at a time is supported.
    if (bytes > 4 || bytes < 1)
        return -1;

    // Check if we are trying to read more than the remaining message size
    if ((net_read_count + bytes) > net_message.cur_size) {
        net_read_err = true;
        return -1;
    }

    // Read and convert to little-endian by bit-shifting accordingly
    for (i = 0; i < bytes; i++)
        buf += (net_message.data[net_read_count + i] << (8 * i));

    net_read_count += bytes;

    return buf;
}

/*
=============
net_read_string
Reads a string from the network message buffer. Either continues or breaks
when encountering a newline, according to the value of break_on_nl
=============
*/
char* net_read_string(bool break_on_nl) {
    static char str[MAX_STRING_CHARS];
    int loc = 0, c;

    do {
        // Read one character at a time
        c = net_read_bytes(1);
        // Break on bad read or end of string
        if (c == -1 || c == 0 || (break_on_nl && c == '\n'))
            break;
        // Copy character to buffer
        str[loc] = c;
        loc++;
    } while (loc < sizeof (str) - 1);

    // Mark end of string as 0
    str[loc] = 0;
    return str;
}

/*
=============
net_skip_bytes
Skips unneeded bytes in the network message buffer 
=============
*/
void net_skip_bytes(int bytes) {
    int i;
    for (i = 0; (net_read_count + 1 <= net_message.cur_size) && (i < bytes); i++) {
        net_read_count++;
    }
}

/*
==============
net_skip_message
Skips an entire network message
==============
*/
void net_skip_message() {
    for (; net_read_count + 1 >= net_message.cur_size; net_read_count++);
}

/*
 * Buffer functions:
 * Buffer memory handling.
*/

/*
=============
buf_init
Inits a buffer for writing
=============
*/
void buf_init(netbuf_t *buf, byte *data, int length) {
    memset(buf, 0, sizeof (*buf));
    buf->data = data;
    buf->max_size = length;
}

/*
=============
buf_clear
Clears a buffer
=============
*/
void buf_clear(netbuf_t *buf) {
    buf->cur_size = 0;
    buf->overflowed = false;
}

/*
=============
buf_allocate
Allocates space from buffer
=============
*/
void *buf_allocate(netbuf_t *buf, int length) {
    void *data;

    if (buf->cur_size + length > buf->max_size) {
        printf("Error: buf_allocate() overflow! Clearing buffer.");
        buf_clear(buf);
        buf->overflowed = true;
    }

    data = buf->data + buf->cur_size;
    buf->cur_size += length;

    return data;
}

/*
=============
buf_write
Writes data to buffer
=============
*/
void buf_write(netbuf_t *buf, void *data, int length) {
    memcpy(buf_allocate(buf, length), data, length);
}

/*
=============
buf_write_string
Writes a string to buffer
=============
*/
void buf_write_string(netbuf_t *buf, char *data) {
    int len;

    len = strlen(data) + 1;

    if (!buf->cur_size || buf->data[buf->cur_size - 1])
        memcpy((byte *) buf_allocate(buf, len), data, len); // No trailing 0
    else
        memcpy((byte *) buf_allocate(buf, len - 1) - 1, data, len); // Write over trailing 0
}

/*
 * Infostring functions
*/

/*
=================
infostring_init
Set up a simple player infostring according to tcl variables' values.
Also initializes serverinfo root node.
=================
*/
void infostring_init(void) {
    infonode_t* cur_node;
    char tmp_str[64]; // Used to convert int values to strings
    userinfo_root = (infonode_t*) qw_eggdrop_malloc(sizeof (infonode_t));
    userinfo_root->next = NULL;
    pthread_mutex_lock(&qw_mutex);

    snprintf(tmp_str, sizeof(tmp_str), "%d", qw_rate);
    cur_node = infostring_add_node(userinfo_root, "rate", tmp_str);

    cur_node = infostring_add_node(cur_node, "name", qw_name);

    snprintf(tmp_str, sizeof(tmp_str), "%d", qw_msgmode);
    cur_node = infostring_add_node(cur_node, "msg", tmp_str);

    snprintf(tmp_str, sizeof(tmp_str), "%d", qw_topcolor);
    cur_node = infostring_add_node(cur_node, "topcolor", tmp_str);

    snprintf(tmp_str, sizeof(tmp_str), "%d", qw_bottomcolor);
    cur_node = infostring_add_node(cur_node, "bottomcolor", tmp_str);

    cur_node = infostring_add_node(cur_node, "spectator", "1");

    if (qw_password[0])
        infostring_add_node(cur_node, "password", qw_password);

    pthread_mutex_unlock(&qw_mutex);

    serverinfo_root = (infonode_t*) qw_eggdrop_malloc(sizeof (infonode_t));
    serverinfo_root->next = NULL;
}

/*
=================
infostring_add_node
Creates a new infonode
=================
*/
infonode_t* infostring_add_node(infonode_t* pos, char* key, char* val) {
    // Check for illegal arguments
    if (!pos || !infostring_check_input(key, val)) {
        printf("Error: infostring_add_node returned NULL!");
        return NULL;
    }

    // Go to tail if next node isn't empty
    while (pos->next) {
        pos = (infonode_t*) pos->next;
    }

    // Allocate new node
    pos->next = (struct infonode_t*) qw_eggdrop_malloc(sizeof (infonode_t));
    pos = (infonode_t*) pos->next;
    strncpy(pos->key, key, strlen(key) + 1);
    strncpy(pos->value, val, strlen(val) + 1);
    pos->next = NULL;

    return pos;
}

/*
=================
infostring_check_input
Checks a key-value pair for errors
=================
*/
bool infostring_check_input(char* key, char* value) {
    if (strnstr(key, strlen(key), "\\") != NULL || strnstr(value, strlen(value), "\\") != NULL) {
        printf("Error: Can't use infostring keys or values with a \\\n");
        return false;
    }
    if (strnstr(key, strlen(key), "\"") != NULL || strnstr(value, strlen(value), "\"") != NULL) {
        printf("Error: Can't use infostring keys or values with a \"\n");
        return false;
    }
    if (strlen(key) > 63 || strlen(value) > 63) {
        printf("Error: Infostring keys and values must be < 64 characters.\n");
        return false;
    }

    return true;
}

/*
=================
infostring_update_node
Updates a value that corresponds to a given key
=================
*/
void infostring_update_node(infonode_t* node, char* key, char* val) {
    bool found = false;

    if (!node) {
        printf("Error: Invalid node as argument for infostring_update_node()!\n");
        return;
    }
    if (!infostring_check_input(key, val))
        return;

    // Did we get the correct node as argument?
    if (node->key) {
        if (!strcmp(node->key, key)) {
            strncpy(node->value, val, strlen(val) + 1);
            return;
        }
    }

    // Check if the node exists
    while (node->next && !found) {
        node = (infonode_t*) node->next;
        if (!strcmp(node->key, key)) {
            strncpy(node->value, val, strlen(val) + 1);
            found = true;
            break;
        }
    }

    // Create new node if necessary
    if (!found)
        infostring_add_node(node, key, val);
}

/*
=================
infostring_clear
Clears all infonodes that are connected to a given node
=================
*/
void infostring_clear(infonode_t* root, bool free_root) {
    infonode_t *tmp;

    if (!root) {
        printf("Error: Invalid node as argument for infostring_clear()!\n");
        return;
    }

    // Check if we have more than one node
    if (root->next) {
        tmp = (infonode_t*) root->next;
        // Store the address of the node after the next node, then delete next node
        while (tmp->next) {
            root->next = tmp->next;
            qw_eggdrop_free(tmp);
            tmp = (infonode_t*) root->next;
        }
        qw_eggdrop_free(tmp);

        root->next = NULL;
    }
    
    // Free the root node if requested. Normally only on disconnect.
    if (free_root) {
        qw_eggdrop_free(root);
        root = NULL;
    }
}

/*
=================
infostring_from_string
Creates infonodes from a infostring
=================
*/
void infostring_from_string(infonode_t* cur_node, char* info) {
    char new_key[64];
    char new_val[64];
    char* cur_char;
    unsigned char str_len = 0;

    if (!cur_node) {
        printf("Error: Invalid node as argument for infostring_from_string()!\n");
        return;
    }

    while (*info) {
        // Skip key-value separator
        if (*info == '\\')
            info++;

        // Reset read characters
        strncpy(new_key, "", sizeof(new_key));
        strncpy(new_val, "", sizeof(new_val));

        // Parse key
        str_len = 0;
        cur_char = new_key;
        while (*info != '\\') {
            if (!*info || str_len > 63)
                break;
            *cur_char++ = *info++;
            str_len++;
        }

        // Skip key-value separator
        if (*info == '\\')
            info++;

        // Parse value
        str_len = 0;
        cur_char = new_val;
        while (*info != '\\' && *info) {
            if (!*info || str_len > 63)
                break;
            *cur_char++ = *info++;
            str_len++;
        }

        // Create new infostring node
        cur_node = infostring_add_node(cur_node, new_key, new_val);
    }
}

/*
=================
infostring_print
Prints infonode key-value pairs to a string
=================
*/
void infostring_print(infonode_t* root, char* istr) {
    if (!root) {
        printf("Error: Invalid node as argument for infostring_to_string()!\n");
        return;
    }

    // Root node is supposed to have no key or value, so skip that.
    root = (infonode_t*) root->next;

    while (root) {
        // Write key-value pair
        strncat(istr, "\\", 1);
        strncat(istr, root->key, sizeof(root->key));
        strncat(istr, "\\", 1);
        strncat(istr, root->value, sizeof(root->value));

        root = (infonode_t*) root->next;
    }
}


/*
 * Miscellaneous
*/

/*
=============
bin2hex
By Steve Reid <steve@edmweb.com>
=============
*/
char *bin2hex(unsigned char *d) {
    static char ret[SHA_DIGEST_LENGTH * 2 + 1];
    int i;
    for (i = 0; i < SHA_DIGEST_LENGTH * 2; i += 2, d++)
        snprintf(ret + i, SHA_DIGEST_LENGTH * 2 + 1 - i, "%02X", *d);
    return ret;
}

/*
=============
get_time
Calculate current time
=============
*/
int get_time(void) {
    struct timeval tp;
    struct timezone tzp;
    static int begin_time;

    gettimeofday(&tp, &tzp);

    if (!begin_time) {
        begin_time = tp.tv_sec;
        return tp.tv_usec / 1000;
    }

    qw.realtime = (tp.tv_sec - begin_time)*1000 + tp.tv_usec / 1000;
    return qw.realtime;
}

/*
==============
byteswap_short
Converts little-endian presentation of short to big-endian.
==============
*/
short byteswap_short(short number) {

    // Value 255 is used with bitwise operation & to ensure all
    // non-1 bits are set to zero.
    unsigned char leftmost = 255, rightmost = 255;

    leftmost &= number; // First byte to the smallest address
    rightmost &= (number >> 8); // Second byte to the largest address

    return (leftmost << 8) +rightmost; // Return swapped bytes as short
}

