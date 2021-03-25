# CS344-smallsh

A lightweight custom shell written in C and developed on CentOS Linux.

To compile:
gcc --std=gnu99 -o smallsh smallsh.c

Features:  
 * Executes ```exit```, ```cd```, ```status``` via code built into the shell
 * Provides process ID number variable expansion for $$ anywhere in command line input
 * Supports input and output redirection
 * Suports running commands as foreground and background processes
 * Executes custom signal handlers for SIGINT(ctrl + C) and SIGTSTP (ctrl + z)
