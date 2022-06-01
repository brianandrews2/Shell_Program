#define _POSIX_SOURCE
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>

//Citation 1:  printing exit of background process derived from the following url:  https://stackoverflow.com/questions/2595503/determine-pid-of-terminated-process

volatile sig_atomic_t foreground_mode = 0;  // toggles foreground-only mode if 1
volatile sig_atomic_t mode_changed = 0;     // toggles when foreground-only mode has changed to prevent program from trying to read that command

/**
 * SIGTSTP (^Z) signal handler function.  Toggles foreground-only mode ON and OFF.
 */
void handle_SIGTSTP() {
  if (foreground_mode == 0) {
    foreground_mode = 1;
    mode_changed = 1;
    write(1, "\nEntering foreground-only mode (& is now ignored)\n", 51);
    fflush(stdout);
  }
  else {
    foreground_mode = 0;
    mode_changed = 1;
    write(1, "\nExiting foreground-only mode\n", 30);
    fflush(stdout);
  }
}


/*
 * Smallsh shell mirrors a shell with built in functions and the ability to handle non-built in functions.
 */
void main() {

  // processes list
  int processes_len = 100;
  int processes[processes_len];
  memset(processes, 0, sizeof(processes));

  int exit_status = 0;

  // signal handler for control-z SIGTSTP
  struct sigaction SIGTSTP_action = {0};
  SIGTSTP_action.sa_handler = handle_SIGTSTP;
  sigfillset(&SIGTSTP_action.sa_mask);
  SIGTSTP_action.sa_flags = 0;
  sigaction(SIGTSTP, &SIGTSTP_action, NULL);

  // signal handler for control-c SIGINT
  struct sigaction SIGINT_action = {0};
  SIGINT_action.sa_handler = SIG_IGN;
  sigaction(SIGINT, &SIGINT_action, NULL);

  // START SHELL
  while(1) {

    processes_len = 100;


    printf(": ");
    fflush(stdout);

    if (mode_changed == 1){
      mode_changed = 0;
      continue;
    }

    // initialize toggles
    int run_in_background = 0;  // toggles command to run in background/foreground
    int source_found = 0;       // toggles if source file is found to open
    int target_found = 0;       // toggles if target file is found to open

    // initialize buffers
    char initial_input[2048]; // stores user input before parsing
    char input[2048];         // stores parsed user input including expanded $$
    char *args[512];          // stores each argument
    char target[2048];        // stores target file
    char source[2048];        // stores source file

    // initialize buffer index trackers
    int initial_i = 0;
    int input_i = 0;
    int arg_i = 0;

    // receive user input
    fgets(initial_input, 2048, stdin);

    // clear buffers after each iteration
    memset(target, 0, sizeof(target));
    memset(source, 0, sizeof(source));
    memset(args, 0, sizeof(args));

    if (mode_changed == 1) {
      mode_changed = 0;
      continue;
    }

    // ignore this input if no command was entered or # for comment was entered
    if (initial_input[0] == '\n' || initial_input[0] == '#') {
      continue;
    }

    // $$ EXPANSION, check each char for $$ expansion and copy from initial_input[] to input[] with expanded values
    while(1) {
      // copy is complete
      if (initial_input[initial_i] == '\n') {
        input[input_i] = '\0';
        break;
      }
      // $ char found
      else if (initial_input[initial_i] == '$') {
        if (initial_input[initial_i+1] != '\n') {
          // second $ found, expand
          if (initial_input[initial_i+1] == '$') {  // check if next char is also $
            int pid = getpid();
            initial_i += 2;
            int pid_len = snprintf(input + input_i, 100, "%d", pid);  // write pid to input[]
            input_i += pid_len;
            continue;
          }
          // second $ NOT found, don't expand
          else {
            input[input_i] = initial_input[initial_i];
            input_i++;
            initial_i++;
          }
        }
        // next char is \n so it can't be $
        else {
          input[input_i] = initial_input[initial_i];
          input_i++;
          initial_i++;
        }
      }
      // $ not found
      else {
        input[input_i] = initial_input[initial_i];
        input_i++;
        initial_i++;
      }
    }

    // POPULATE ARGS[], inspect each token and add to args[]
    int args_found = 0;
    char *token = strtok(input, " ");

    // dont check tokens if ^Z was the input
    if (mode_changed == 1) {
      mode_changed = 0;
      continue;
    }

    while (token != NULL) {

      // check token for <, > and & that will not be added to args[]

      // < found, don't add to args[]
      if (!strcmp(token, "<")) {
        token = strtok(NULL, " ");  // check next token for file name
        strcpy(source, token);
        source_found = 1;
        token = strtok(NULL, " ");  // skip to next token without adding to args[]
      }

      // > found, don't add to args[]
      else if (!strcmp(token, ">")) {
        token = strtok(NULL, " ");  // check next token for file name
        strcpy(target, token);
        target_found = 1;
        token = strtok(NULL, " ");  // skip to next token without adding to args[]
      }

      // & found
      else if (!strcmp(token, "&")) {
        char ampersand[] = "&";
        token = strtok(NULL, " ");
        // & was last token of the command, don't add it to args[], run in background
        if (token == NULL) {
          run_in_background = 1;
        }
        // & was not the last token of the command, add to args[], don't run in background
        else {
          args[arg_i] = ampersand;
          args_found++;
          arg_i++;
        }
      }

      // no <, >, or & add token to args[]
      else {
        args[arg_i] = token;
        args_found++;
        arg_i++;
        token = strtok(NULL, " ");
      }
    }

    // CHECK ARGS FOR BUILT IN COMMANDS

    // exit
    if (!strcmp(args[0], "exit")) {

      //kill remaining processes
      for (int i = 0; i < processes_len; i++) {
        if (processes[i] != 0) {
          kill(processes[i], SIGTERM);
        }
      }
      exit(0);
    }

    // cd
    if (!strcmp(args[0], "cd")) {
      char dir_buffer[100];
      if (args_found == 1) {
        chdir(getenv("HOME"));
      }
      else {
        int n = chdir(args[1]);
        if (n == -1) {
          fprintf(stderr, "Invalid directory provided.\n");
          fflush(stdout);
        }
      }
      continue;
    }

    // status
    if (!strcmp(args[0], "status")) {
      printf("exit value %d\n", exit_status);
      fflush(stdout);
      continue;
    }

    // EXECUTE NON-BUILT IN COMMAND
    int child_status;
    pid_t spawn_pid = fork();
    switch(spawn_pid) {
      // error
      case -1:
        perror("Failed to create child process\n");
        fflush(stdout);
        exit(1);

      // child process
      case 0:
        // update signal handler for SIGINT to cancel child process
        SIGINT_action.sa_handler = SIG_DFL;
        sigaction(SIGINT, &SIGINT_action, NULL);

        // source file
        if (source_found == 1) {
          int source_fd;

          // background process, set stdin to /dev/null
          if (run_in_background == 1) {
            source_fd = open("/dev/null", 0);
          }

          // not background process, open file
          else {
            source_fd = open(source, O_RDONLY);
            if (source_fd == -1) {
              perror("Source failed to open\n");
              fflush(stdout);
              exit(1);
            }
          }
          int source_result = dup2(source_fd, 0);
          if (source_result == -1) {
            perror("dup2 source failed\n");
            fflush(stdout);
            exit(1);
          }
          fcntl(source_fd, F_SETFD, FD_CLOEXEC);
        }

        // target file
        if (target_found == 1) {
          int target_fd;

          // background process, set stdout to /dev/null
          if (run_in_background == 1) {
          target_fd = open("/dev/null", 0);
          }

          // not background process, open file
          else {
            target_fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (target_fd == -1) {
              perror("Target failed to open\n");
              fflush(stdout);
              exit(1);
            }
          }
          int target_result = dup2(target_fd, 1);
          if (target_result == -1) {
            perror("dup2 target failed\n");
            fflush(stdout);
            exit(1);
          }
          fcntl(target_fd, F_SETFD, FD_CLOEXEC);
        }

        // execute
        execvp(args[0], args);
        perror("Failed to execute child process\n");
        fflush(stdout);
        exit(1);

      // parent process
      default:

        // run in background
        if (run_in_background == 1 && foreground_mode == 0) {
          printf("Background pid: %d\n", spawn_pid);
          fflush(stdout);

          // add background process to process array
          for (int i = 0; i < processes_len; i++) {
            if (processes[i] == 0) {
              processes[i] = spawn_pid;
              break;
            }
          }

          // call child function
          spawn_pid = waitpid(spawn_pid, &child_status, WNOHANG);
          if (WIFEXITED(child_status)) {
            exit_status = WEXITSTATUS(child_status);
          }
          else {
            exit_status = WTERMSIG(child_status);
          }
        }

        // run in foreground
        else {

          // call child function
          spawn_pid = waitpid(spawn_pid, &child_status, 0);
          if (WIFEXITED(child_status)) {
            exit_status = WEXITSTATUS(child_status);
          }
          else {
            exit_status = WTERMSIG(child_status);
            printf("\nbackground pid %d is done.  terminated with signal %d\n", spawn_pid, exit_status);
            fflush(stdout);
          }
        }


        // cycle through each background process to check for completion and remove from processes list
        while ((spawn_pid = waitpid(-1, &child_status, WNOHANG)) > 0) {
          for (int i = 0; i < processes_len; i++) {
            if (spawn_pid == processes[i]) {
              printf("background pid %d is done:  exit value %d\n", spawn_pid, child_status);
              fflush(stdout);
              processes[i] = 0;
            }
          }
        }
    }
  }
}
