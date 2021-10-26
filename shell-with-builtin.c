#include "sh.h"
#include <utmpx.h>

static struct watchedUser * head;
static pthread_t thread_no;
static int rc;

void sig_handler(int sig) {
  fprintf(stdout, "\n>> ");
  fflush(stdout);
}

struct watchedUser getUser(char *username) { // creates a linked list of users.
  head = (struct watchedUser*) malloc(sizeof(struct watchedUser));
  head->username = username;
  head->active = 1;
  head->next = NULL;
}

void addUser(char *username) { // adds a user to the linked list of users
  struct watchedUser *tmp = head;
  while(tmp->next) tmp=tmp->next;
  tmp->next = (struct watchedUser*) malloc(sizeof(struct watchedUser));
  tmp=tmp->next;
  tmp->active = 1;
  tmp->next = NULL;
}

void * doWatchUser() { // this is executed repeatedly on the second thread created (and is closed and joined on exit)
  struct watchedUser * tmp;
  struct utmpx *up;
  setutxent();
  while(1) {
    sleep(20);
    tmp = head;
    while (up = getutxent()) {
      if (up->ut_type == USER_PROCESS) {
        while(tmp) {
          if(strcmp(tmp->username,&(up->ut_user[0]))==0&&tmp->active==1) {
            printf("%s has logged on %s from %s\n", up->ut_user, up->ut_line, up ->ut_host);
            tmp->active = 0;
          } tmp = tmp->next;
        }
      }
    }
  }
}

int main(int argc, char ** argv, char ** envp) {
  char buf[MAXLINE];
  char * arg[MAXARGS]; // an array of tokens
  char ** arg2; // also an array of tokens
  char * ptr;
  char * pch;
  pid_t pid;
  int status, i, arg_no, arg2_no, background, noclobber;
  int redirection, append, pipef, rstdin, rstdout, rstderr;
  struct pathelement * dir_list, * tmp;
  char * cmd;
  char * cmd2;
  char builtin = 0;
  char watching = 0;
  int incopy = dup(STDIN_FILENO);
  int outcopy = dup(STDOUT_FILENO);
  int errcopy = dup(STDERR_FILENO);
  

  noclobber = 0; // initially default to 0

  signal(SIGINT, sig_handler);

  fprintf(stdout, ">> "); /* print prompt (printf requires %% to print %) */
  fflush(stdout);
  while (fgets(buf, MAXLINE, stdin) != NULL) {
    
    if (strlen(buf) == 1 && buf[strlen(buf) - 1] == '\n')
      goto nextprompt; // "empty" command line

    if (buf[strlen(buf) - 1] == '\n')
      buf[strlen(buf) - 1] = 0; /* replace newline with null */

    // no redirection or pipe
    redirection = append = pipef = rstdin = rstdout = rstderr = 0;
    // check for >, >&, >>, >>&, <, |, and |&
    if (strstr(buf, ">>&"))
      redirection = append = rstdout = rstderr = 1;
    else
    if (strstr(buf, ">>"))
      redirection = append = rstdout = 1;
    else
    if (strstr(buf, ">&"))
      redirection = rstdout = rstderr = 1;
    else
    if (strstr(buf, ">"))
      redirection = rstdout = 1;
    else
    if (strstr(buf, "<"))
      redirection = rstdin = 1;
    else
    if (strstr(buf, "|&"))
      pipef = rstdout = rstderr = 1;
    else
    if (strstr(buf, "|"))
      pipef = rstdout = 1;
    arg_no = 0;
    pch = strtok(buf, " ");
    while (pch != NULL && arg_no < MAXARGS) {
      arg[arg_no] = pch;
      arg_no++;
      pch = strtok(NULL, " ");
    }
    arg[arg_no] = (char * ) NULL;
    
    for(int i = 0; arg[i]; i++) {
      if(strchr(arg[i],'|') || strchr(arg[i],'>') || strchr(arg[i],'<')) {
        if(strchr(arg[i],'|')) if(!access(arg[i+1],X_OK)) arg[i+1] = which(arg[i+1], get_path());
        arg2 = &arg[i+1];
        arg2_no = arg_no - i - 1;
        arg_no = i;
        arg[i] = NULL;
        break;
      }
    }

    if (arg[0] == NULL) // "blank" command line
      goto nextprompt;

    background = 0; // not background process
    if (arg[arg_no - 1][0] == '&')
      background = 1; // to background this command
    int fid;
    int pwd = strcmp(arg[0],"pwd")?0:1;
    int echo = strcmp(arg[0],"echo")?0:1;
    int nc = strcmp(arg[0],"noclobber")?0:1;
    int ww = strcmp(arg[0],"which")?0:1;
    if (redirection && (pwd||echo||nc||ww)) { // opens files for redirection on built in commands
      if (!append && rstdout) // ">"
        if(noclobber) fid = open(arg2[arg2_no - 1], O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);
        else fid = open(arg2[arg2_no - 1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP);
      else if (append && rstdout) // ">>"
        if(noclobber) fid = open(arg2[arg2_no - 1], O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP);
        else fid = open(arg2[arg2_no - 1], O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP);
      close(1);
      dup(fid);
      if(rstderr) {
        close(2);
        dup(fid);
      }
      close(fid);
    }
    if (strcmp(arg[0], "exit") == 0) { // built-in command exit
      printf("Executing built-in [exit]\n");
      pthread_cancel(thread_no);
      pthread_join(thread_no, NULL);
      struct watchedUser * tmp;
      while (head) { // free list of usernames in head
        tmp = head;
        head = head -> next;
        free(tmp);
      }
      exit(0);
    } else
    if (strcmp(arg[0], "cd") == 0) { // built-in command cd
      printf("Executing built-in [cd]\n");
      char * pa;
      if (arg_no == 1) // cd
        pa = getenv("HOME");
      else
        pa = arg[1]; // cd arg[1]
      if (chdir(pa) < 0)
        printf("%s: No such file or directory.\n", pa);
    } else
    if (strcmp(arg[0], "pwd") == 0) { // built-in command pwd 
      printf("Executing built-in [pwd]\n");
      ptr = getcwd(NULL, 0);
      printf("%s\n", ptr); // no redirection
      free(ptr);
    } else
    if (strcmp(arg[0], "noclobber") == 0) { // built-in command noclobber
      printf("Executing built-in [noclobber]\n");
      noclobber = 1 - noclobber; // switch value
      printf("%d\n", noclobber);
    } else
    if (strcmp(arg[0], "echo") == 0) { // built-in command echo
      printf("Executing built-in [echo]\n");
      printf("%s\n", arg[1]);
    } else
    if (strcmp(arg[0], "which") == 0) { // built-in command which
      printf("Executing built-in [which]\n");
      if (arg[1] == NULL) { // "empty" which
        printf("which: Too few arguments.\n");
        goto nextprompt;
      } else {
        dir_list = get_path();
        printf("%s: %s\n",arg[1],which(arg[1], dir_list));
      }
      cmd = which(arg[1], dir_list);
      if (cmd) {
        //printf("%s\n", cmd);
        free(cmd);
      } else // argument not found
        printf("%s: Command not found\n", arg[1]);
      while (dir_list) { // free list of path values
        tmp = dir_list;
        dir_list = dir_list -> next;
        free(tmp -> element);
        free(tmp);
      }
    }
    if(redirection && (pwd||echo||nc||ww)) {
      fid = open("/dev/tty", O_WRONLY); // redirect stdout
      close(1); // back to terminal
      dup(outcopy);
      close(2);
      dup(errcopy);
    } else if(strcmp(arg[0],"watchuser")==0) {
      if(watching) addUser(arg[1]);
      else {
        if(arg[1]) getUser(arg[1]); else goto nextprompt;
        rc = pthread_create(&thread_no, NULL, doWatchUser, "A");
        //doWatchUser(arg[1]);
      }
    } 
    if(strcmp(arg[0],"noclobber")==0||strcmp(arg[0],"cd")==0) goto nextprompt;
    // add other built-in commands here
    else { // external command
      if ((pid = fork()) < 0) {
        printf("fork error");
      } else if (pid == 0) {
        /* child */
        // an array of aguments for execve()
        char * execargs[MAXARGS];
        glob_t paths;
        int csource, j;
        char ** p;
        if (arg[0][0] != '/' && strncmp(arg[0], "./", 2) != 0 && strncmp(arg[0], "../", 3) != 0) { // get absoulte path of command
          dir_list = get_path();
          cmd = which(arg[0], dir_list);
          for(int i = 0; arg[i]; i++) 
            if(strchr(arg[i],'|')) {
              cmd2 = which(arg[i+1],dir_list);
            }
          if (cmd)
            printf("Executing [%s]\n", cmd);
          else { // argument not found
            printf("%s: Command not found\n", arg[1]);
            goto nextprompt;
          }

          while (dir_list) { // free list of path values
            tmp = dir_list;
            dir_list = dir_list -> next;
            free(tmp -> element);
            free(tmp);
          }
          execargs[0] = malloc(strlen(cmd) + 1);
          strcpy(execargs[0], cmd); // copy "absolute path"
          free(cmd);
        } else {
          execargs[0] = malloc(strlen(arg[0]) + 1);
          strcpy(execargs[0], arg[0]); // copy "command"
        }

        j = 1;
        for (i = 1; i < arg_no; i++) { // check arguments
          if (strchr(arg[i], '*') != NULL) { // wildcard!
            csource = glob(arg[i], 0, NULL, & paths);
            if (csource == 0) {
              for (p = paths.gl_pathv;* p != NULL; ++p) {
                execargs[j] = malloc(strlen( * p) + 1);
                strcpy(execargs[j], * p);
                j++;
              }
              globfree( & paths);
            } else
            if (csource == GLOB_NOMATCH) {
              execargs[j] = malloc(strlen(arg[i]) + 1);
              strcpy(execargs[j], arg[i]);
              j++;
            }
          } else {
            execargs[j] = malloc(strlen(arg[i]) + 1);
            strcpy(execargs[j], arg[i]);
            j++;
          }
        }
        execargs[j] = NULL;

        if (background) { // get rid of argument "&"
          j--;
          free(execargs[j]);
          execargs[j] = NULL;
        }
        
        // redirection and piping
        int newpid; // to be used when forking
        int fid2; // to be used when opening files
        if (redirection) { // opens a file to redirect to (or from in the case of rstdin)
          if (!append && rstdout) // ">"
            if(noclobber==1) fid2 = open(arg2[arg2_no - 1], O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);
            else fid2 = open(arg2[arg2_no - 1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP);
          else if (append && rstdout) // ">>"
            if(noclobber==1) fid2 = open(arg2[arg2_no - 1], O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP);
            else fid2 = open(arg2[arg2_no - 1], O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP);
          else if (rstdin)
            fid2 = open(arg2[arg2_no - 1], O_RDONLY | O_CREAT, S_IRWXU);
        }
        int xxx;
        int st;
        int fd[2];
        struct pathelement * newpaths;
        pipe(fd); // creates pipe on fd
        if((xxx=fork())==0) { // forks for IPC
          if(redirection&&!rstdin) { // redirects stdout (and err) to file
            close(1);
            dup(fid2);
            if(rstderr) {
              close(2);
              dup(fid2);
            }
            close(fid2);
          }
          if(rstdin) { // redirects the new file to stdin
            close(0);;
            dup(fid2);
          }
          if(pipef) { // redirects stdout (and err) to pipe fd
            close(1);
            dup(fd[1]);
            if(rstderr) {
              close(2);
              dup(1);
            }
          }
          execve(execargs[0],execargs,NULL); // executes!
        } else {
          waitpid(xxx, &st, 0); // waits for state change of child
          if(pipef) { // executes second command, using newly generated (and freed) list of path values
            close(0);
            dup(fd[0]);
            arg2[0] = which(arg2[0],(newpaths = get_path()));
            struct pathelement * tmp;
            while (newpaths) { // free list of path values
              tmp = newpaths;
              newpaths = newpaths -> next;
              free(tmp -> element);
              free(tmp);
            }
            execve(arg2[0],arg2,NULL);
          }
          fid = open("/dev/tty", O_WRONLY); // redirect stdout
          close(1); // back to terminal
          dup(outcopy); // redirects everything back where it should go
          close(2);
          dup(errcopy);
          close(0);
          dup(incopy);
        }
        exit(127);
      }

      /* parent */
      if (!background) { // foreground process
        if ((pid = waitpid(pid, & status, 0)) < 0)
          printf("waitpid error");
      } else { // no waitpid if background
        background = 0;
      }
    }
    nextprompt:
      fprintf(stdout, ">> ");
    fflush(stdout);
  }
  exit(0);
}