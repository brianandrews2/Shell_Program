Description:  A shell that executes commands similar to Bash.  Built in commands are handled by the shell.  Any other commands are executed using exec() function.  Signal handlers prevent ^C and ^Z normal functionality.  ^Z toggles foreground-only mode which prevents executed commands from running in the background.  To exit, type "exit" as the command.

Compile command for smallsh:
gcc -std=c99 -o smallsh smallsh.c
