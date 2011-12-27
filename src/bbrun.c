/*
 * Copyright (C) 2011 Bumblebee Project
 * Author: Jaron Viëtor AKA "Thulinma" <jaron@vietors.com>
 *
 * This file is part of Bumblebee.
 *
 * Bumblebee is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Bumblebee is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Bumblebee. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Run command functions for Bumblebee
 */

#include "bbrun.h"
#include "bblogger.h"
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>

int handler_set = 0;

/// Socket list structure for use in main_loop.

struct pidlist {
  pid_t PID;
  struct pidlist *prev;
  struct pidlist *next;
};

struct pidlist * pidlist_start = 0; ///Begin of the linked-list of PIDs, if any.

/// Adds a pid_t to the linked list of PIDs.
/// Creates the list if it is still null.

static void pidlist_add(pid_t newpid) {
  struct pidlist *curr = malloc(sizeof (struct pidlist));
  curr->PID = newpid;
  curr->prev = 0;
  // the PID is inserted BEFORE the first PID, this should not matter
  curr->next = pidlist_start ? pidlist_start : 0;
  pidlist_start = curr;
}

/// Removes a pid_t from the linked list of PIDs.
/// Makes list null if empty.
static void pidlist_remove(pid_t rempid) {
  struct pidlist *curr;
  struct pidlist *next_iter;
  for (curr = pidlist_start; curr; curr = next_iter) {
    next_iter = curr->next;
    if (curr->PID == rempid) {
      if (curr->next) {
        curr->next = curr->prev;
      }
      if (curr->prev) {
        curr->prev = curr->next;
      } else {
        pidlist_start = curr->next;
      }
      free(curr);
    }
  }
}//pidlist_remove

/// Finds a pid_t in the linked list of PIDs.
/// Returns 0 if not found, 1 otherwise.

static int pidlist_find(pid_t findpid) {
  struct pidlist *curr;
  for (curr = pidlist_start; curr; curr = curr->next) {
    if (curr->PID == findpid) {
      return 1;
    }
  }
  return 0;
}//pidlist_find

static void childsig_handler(int signum) {
  if (signum != SIGCHLD) {
    return;
  }
  pid_t ret = wait(0);
  bb_log(LOG_DEBUG, "Process with PID %i terminated.\n", ret);
  pidlist_remove(ret);
}//childsig_handler

static void check_handler(void) {
  // Set handler for this child process if not already
  if (handler_set == 0) {
    struct sigaction new_action;
    new_action.sa_handler = childsig_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGCHLD, &new_action, NULL);
    handler_set = 1;
  }
}//check_handler

/**
 * Forks and runs the given application.
 * More suitable for configurable arguments to pass
 *
 * @param argv The arguments values, the first one is the application path or name
 * @return The new process PID
 */
pid_t bb_run_fork(char** argv) {
  check_handler();
  // Fork and attempt to run given application
  pid_t ret = fork();
  if (ret == 0) {
    // Fork went ok, child process replace
    bb_run_exec(argv);
  } else {
    if (ret > 0) {
      // Fork went ok, parent process continues
      bb_log(LOG_INFO, "Process %s started, PID %i.\n", argv[0], ret);
      pidlist_add(ret);
    } else {
      // Fork failed
      bb_log(LOG_ERR, "Process %s could not be started. fork() failed.\n", argv[0]);
      return 0;
    }
  }
  return ret;
}

/**
 * Forks and runs the given application, using an LD_LIBRARY_PATH.
 * More suitable for configurable arguments to pass
 *
 * @param argv The arguments values, the first one is the application path or name
 * @return The new process PID
 */
pid_t bb_run_fork_ld(char** argv, char * ldpath) {
  check_handler();
  // Fork and attempt to run given application
  pid_t ret = fork();
  if (ret == 0) {
    // Fork went ok, set environment
    setenv("LD_LIBRARY_PATH", ldpath, 1);
    bb_run_exec(argv);
  } else {
    if (ret > 0) {
      // Fork went ok, parent process continues
      bb_log(LOG_INFO, "Process %s started, PID %i.\n", argv[0], ret);
      pidlist_add(ret);
    } else {
      // Fork failed
      bb_log(LOG_ERR, "Process %s could not be started. fork() failed.\n", argv[0]);
      return 0;
    }
  }
  return ret;
}

/**
 * Forks and runs the given application, waits for process to finish.
 *
 * @param argv The arguments values, the first one is the application path or name
 */
void bb_run_fork_wait(char** argv) {
  check_handler();
  // Fork and attempt to run given application
  pid_t ret = fork();
  if (ret == 0) {
    // Fork went ok, child process replace
    bb_run_exec(argv);
  } else {
    if (ret > 0) {
      // Fork went ok, parent process continues
      bb_log(LOG_INFO, "Process %s started, PID %i.\n", argv[0], ret);
      pidlist_add(ret);
      //sleep until process finishes
      while (bb_is_running(ret)) {
        usleep(1000000);
      }
    } else {
      // Fork failed
      bb_log(LOG_ERR, "Process %s could not be started. fork() failed.\n", argv[0]);
      return;
    }
  }
  return;
}

/// Returns 1 if a process is currently running, 0 otherwise.

int bb_is_running(pid_t proc) {
  return pidlist_find(proc);
}

/// Stops the running process, if any.
void bb_stop(pid_t proc) {
  if (bb_is_running(proc)) {
    kill(proc, SIGTERM);
  }
}

/// Stops the running process, if any.
/// Does not return until successful.
/// Is always successful, eventually.
void bb_stop_wait(pid_t proc) {
  int i = 0;
  while (bb_is_running(proc)) {
    ++i;
    //the first 10 attempts, use SIGTERM
    if (i < 10) {
      kill(proc, SIGTERM);
    } else {
      //after that, use SIGKILL
      kill(proc, SIGKILL);
    }
    usleep(1000000); //sleep up to a second, waiting for process
  }
}

/// Stops all the running processes, if any.
void bb_stop_all(void) {
  struct pidlist *next = pidlist_start;
  struct pidlist *curr;
  // loop through all items and kill the first item until it's empty
  while (next) {
    curr = next;
    next = pidlist_start->next;
    bb_stop_wait(curr->PID);
    /* If the program could not be killed, the memory is still in use. Just
     * accept the memory loss for now to avoid an invalid memory access if the
     * program exited while we're free()'ing it here */
  }
}

/// Attempts to run the given application, replacing the current process

void bb_run_exec(char ** argv) {
  execvp(argv[0], argv);
  bb_log(LOG_ERR, "Error running \"%s\": %s\n", argv[0], strerror(errno));
  exit(42);
}
