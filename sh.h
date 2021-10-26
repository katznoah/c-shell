#include "get_path.h"

#include <pthread.h>

#include <unistd.h>

#include <sys/types.h>

#include <sys/stat.h>

#include <fcntl.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <signal.h>

#include <glob.h>

#include <sys/wait.h>

struct watchedUser {
  char *username;
  char active;
  struct watchedUser *next;
};

int pid;
char *which(char *command, struct pathelement *pathlist);
void list(char *dir);
void printenv(char **envp);

#define PROMPTMAX 64
#define MAXARGS   16
#define MAXLINE   128
