This module is only tested to work with eggdrop 1.8 and QuakeWorld KTX mod. You will also need libssl-dev package (or equivalent) to compile with encryption support.

INSTALLATION:

(1) Compile the module and put the qwirc.so file in your eggdrop modules directory.

(2) Copy the qwirc.tcl file to your eggdrop scripts directory.

(3) Add the line "loadmodule qwirc" to your eggdrop configuration file.

(4) Add the line "source scripts/qwirc.tcl" to your eggdrop configuration file.

(5) Configure the TCL variables in scripts/qwirc.tcl file.

(6) Launch the bot.

(7) To activate this module on a channel, you need to have chanmode +qwirc enabled. Use the eggdrop command .chanset.

(8) To use all available commands, you need to have userflag +Q on the QuakeWorld channel. Use the eggdrop command .chattr.


USAGE:

!qconnect - Connects to the QuakeWorld server

!qdisconnect - Disconnects from the QuakeWorld server

!qmap - Prints the name of the current map

!qhelp - Prints available commands

!qsay - Sends chat messages to the QuakeWorld server

!qrcon - Sends rcon messages to the QuakeWorld server (if qw_rcon_password is set)

