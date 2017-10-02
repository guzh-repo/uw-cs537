#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
//#define DEBUG printf
#define DEBUG if (1) {} else printf

typedef int bool;
const int FALSE = 0;
const int TRUE = 1;

typedef struct cmd_struct {
    char *cmd;
    int argc;
    char **argv; // first argument is same as cmd i.e. point to same object
    int fd_in;
    int fd_out;
    bool isbg;
} cmd_struct;

bool streq(const char *s1, const char *s2) {
    if (strcmp(s1,s2) == 0)
        return TRUE;
    else
        return FALSE;
}

const char* get_home_dir() {
    return getenv("HOME");
}

#define BG_MAP_SIZE 8192 // QUESTION hope it's enough...
pid_t *bg_pids = NULL;
char **bg_cmds = NULL;
int bg_size = 0;
int bg_capacity = 1;

/* the cmd get copied, remember to free it */
void bg_map_add(pid_t pid, const char *cmd) {
    if (bg_size >= bg_capacity - 1) { // expand
        bg_capacity *= 2;
        bg_pids = realloc(bg_pids, sizeof(pid_t*) * bg_capacity);
        bg_cmds = realloc(bg_cmds, sizeof(char*) * bg_capacity);
    }
    bg_pids[bg_size] = pid;
    bg_cmds[bg_size] = strdup(cmd);
    bg_size ++;
}

const char* bg_map_find(pid_t pid) {
    int i;
    for (i=0;i<bg_size;i++) {
        if (bg_pids[i]==pid)
            return bg_cmds[i];
    }
    return NULL;
}

void bg_map_del(pid_t pid) {
    int i;
    for (i=0;i<bg_size;i++) {
        if (bg_pids[i]==pid) {
            //DEBUG("\x1B[32m" "Removed pid:%d name:%s replacename:%s" "\x1B[0m\n", pid, bg_cmds[i], bg_cmds[bg_size-1]);
            free(bg_cmds[i]);
            bg_cmds[i] = bg_cmds[bg_size-1];
            bg_pids[i] = bg_pids[bg_size-1];
            bg_size--;
            return;
        }
    }
}

void bg_map_free() {
    int i;
    for (i=0;i<bg_size;i++) {
        free(bg_cmds[i]);
    }
    free(bg_pids);
    free(bg_cmds);
}



/* check command grammar and file availability
 * returns TRUE if everythings ok
 * otherwise prints to stderr and returns FALSE
 * free_cmd_struct should be called if TRUE returned
 */
bool parse_cmd(char *cmd, cmd_struct *r) {
    char *tokens[300];
    int idx = 0, i=0, j=0;
    int len = strlen(cmd);

    // tokenize
    for (i=0;i<len;i++)
        if (cmd[i] == '\r' || cmd[i]=='\n')
            cmd[i] ='\0';
    i=0;
    while(i<len) {
        while(cmd[i] == ' ') i++;
        if (cmd[i]=='\0') break;
        j=i;
        while(cmd[j]!=' '&&cmd[j]!='\0')j++;
        tokens[idx] = (char*)malloc(sizeof(char)*(j-i)+2);
        int k;
        for (k = 0;i+k<j;k++) {
            tokens[idx][k] = cmd[i+k];
        }
        tokens[idx][k]='\0';
        i=j;
        idx++;
    }

    if (idx == 0) return FALSE; // empty command line

    int idx_first_special = -1;
    for (i=0;i<idx;i++) {
        if (streq(tokens[i],">") ||streq(tokens[i],"<") ||streq(tokens[i],"&")) {
            idx_first_special = i;
            break;
        }
    }
    if (idx_first_special == 0) {
        fprintf(stderr, "missing program to run\n");
        for (i=0;i<idx;i++) free(tokens[i]);
        return FALSE;
    }
    if (idx_first_special<0) idx_first_special=idx;

    r->cmd = tokens[0];
    r->argc = idx_first_special;
    r->argv = malloc(sizeof(char*)*(r->argc+1));
    for (i=0;i<r->argc;i++) {
        r->argv[i]=tokens[i];
    }
    r->argv[r->argc]=NULL;
    r->fd_in = -1;
    r->fd_out=-1;
    r->isbg=FALSE;

    while(idx_first_special < idx) {
        if (streq(tokens[idx_first_special],">")) {
            if (idx_first_special+1>=idx) {
                fprintf(stderr, "missing output file\n");
                goto fail_return;
            }
            r->fd_out = open(tokens[idx_first_special+1],O_WRONLY|O_CREAT|O_TRUNC,0644);
            if (r->fd_out<0){
                perror("cannot open output file");
                goto fail_return;
            }
            free(tokens[idx_first_special]);
            free(tokens[idx_first_special+1]);
            tokens[idx_first_special] = NULL;
            tokens[idx_first_special+1] = NULL;
            idx_first_special +=2;
        } else if (streq(tokens[idx_first_special],"<")) {
            if (idx_first_special+1>=idx) {
                fprintf(stderr, "missing input file\n");
                goto fail_return;
            }
            r->fd_in = open(tokens[idx_first_special+1],O_RDONLY);
            if (r->fd_in<0){
                perror("cannot open input file");
                goto fail_return;
            }
            free(tokens[idx_first_special]);
            free(tokens[idx_first_special+1]);
            tokens[idx_first_special] = NULL;
            tokens[idx_first_special+1] = NULL;
            idx_first_special +=2;
        } else if (streq(tokens[idx_first_special],"&")) {
            r->isbg = TRUE;
            free(tokens[idx_first_special]);
            tokens[idx_first_special] = NULL;
            idx_first_special +=1;
        } else {
            fprintf(stderr, "redundant argument after special char\n");
            goto fail_return;
        }
        if (idx_first_special >= idx) break;
    }

    return TRUE;

fail_return:
    for (i=0;i<idx;i++) free(tokens[i]);
    free(r->argv);
    if (r->fd_in>=0)close(r->fd_in);
    if (r->fd_out>=0)close(r->fd_out);
    return FALSE;
}

// free internal members but not struct itself
void free_cmd_struct(cmd_struct *r) {
    int i;
    for (i=0;i<r->argc;i++)free(r->argv[i]);
    free(r->argv);
    if (r->fd_in>=0)close(r->fd_in);
    if (r->fd_out>=0)close(r->fd_out);
}

/* check argv and stdin,
 * if a file is given and readable, returns it
 * if the file not readable, halt
 * if stdin is not attached to tty, return stdin
 * otherwise return null
 */
FILE* batch_mode_file(int argc, char *argv[]) {
    if (argc >= 2) {
        FILE *f = fopen(argv[1], "r");
        if (f==NULL) {
            perror("Cannot open file");
            exit(1);
        }
        return f;
    } else if (!isatty(STDIN_FILENO)) {
        return stdin;
    }
    return NULL;
}

/* wait() finished background processes */
void check_bg() {
    while(1) {
        int stat;
        pid_t pid = waitpid(-1, &stat, WNOHANG);
        if (pid > 0) {
            int ret = WEXITSTATUS(stat);
            const char* cmd = bg_map_find(pid);
            if (cmd != NULL) {
                fprintf(stderr, "[%s (%d) completed with status %d]\n", cmd, pid, ret);
                bg_map_del(pid);
            } else {
                fprintf(stderr, "[NAME_UNKNOWN (%d) completed with status %d]\n", pid, ret);
            }
        } else {
            return;
        }
    }
}

void exit_clean() {
    bg_map_free();
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
}

int main(int argc, char *argv[])
{
    atexit(exit_clean);
    if (argc > 2) {
        fprintf(stderr, "Redundant argument\n");
        exit(1);
    }
    FILE *f = batch_mode_file(argc, argv);
    bool interactive_mode = FALSE;
    if (f == NULL) {
        f = stdin;
        interactive_mode = TRUE;
    }

    // main loop
    while(TRUE) {
        char cmdbuf[300];
        char *read_return;
        if (interactive_mode) {
            check_bg();
            printf("sqysh$ "); fflush(stdout);
            read_return = fgets(cmdbuf, 300, stdin);
            if (read_return == NULL) { //EOF
                exit(0);
            }
            check_bg();
        } else {
            check_bg();
            read_return = fgets(cmdbuf, 300, f);
            if (read_return == NULL) { //EOF
                fclose(f);
                exit(0);
            }
        }

        cmd_struct cmd;
        DEBUG("\x1B[31m" "(%d)$ %s" "\x1B[0m", getpid(), cmdbuf); fflush(stdout);
        bool success = parse_cmd(cmdbuf, &cmd);
        if (success) {
            if (streq(cmd.cmd,"cd")) {
                if (cmd.argc>2) {
                    fprintf(stderr, "cd: too many arguments\n");
                    free_cmd_struct(&cmd);
                    continue;
                }
                const char* path;
                if (cmd.argc == 1) {
                    path = get_home_dir();
                    if (path == NULL) {
                        fprintf(stderr, "cd: %s\n", strerror(errno));
                    }
                } else {
                    path = cmd.argv[1];
                }
                if (path!=NULL) {
                    if (chdir(path)) {
                        fprintf(stderr, "cd: %s: %s\n", path, strerror(errno));
                    }
                }
            } else if (streq(cmd.cmd,"pwd")) {
                if (cmd.argc>1) {
                    fprintf(stderr, "pwd: too many arguments\n");
                    free_cmd_struct(&cmd);
                    continue;
                }
                char buf[1024];
                if (getcwd(buf,1024)==NULL) {
                    fprintf(stderr, "pwd: %s\n", strerror(errno));
                } else {
                    printf("%s\n", buf);
                }
            } else if (streq(cmd.cmd,"exit")) {
                if (cmd.argc>1) {
                    fprintf(stderr, "exit: too many arguments\n");
                    free_cmd_struct(&cmd);
                    continue;
                }
                free_cmd_struct(&cmd);
                if (f != stdin) fclose(f);
                exit(0);
            } else {
                pid_t pid = fork();
                if (pid > 0) { // parent
                    int stat;
                    if (cmd.isbg) {
                        DEBUG("\x1B[32m" "bg_cmd: %s(%d)" "\x1B[0m\n", cmd.cmd, pid);
                        bg_map_add(pid, cmd.cmd);
                    } else {
                        pid_t success = waitpid(pid, &stat, 0);
                        if (success != pid) {
                            fprintf(stderr, "%s: %s\n", cmd.cmd, strerror(errno));
                        }
                    }
                } else if (pid == 0) { // child
                    if (cmd.fd_in >= 0) {
                        if (dup2(cmd.fd_in, STDIN_FILENO) < 0) {
                            fprintf(stderr, "%s: %s\n", cmd.cmd, strerror(errno));
                            goto error;
                        }
                    }
                    if (cmd.fd_out >= 0) {
                        if (dup2(cmd.fd_out, STDOUT_FILENO) < 0) {
                            fprintf(stderr, "%s: %s\n", cmd.cmd, strerror(errno));
                            goto error;
                        }
                    }
                    if (execvp(cmd.cmd, cmd.argv)<0) {
                        fprintf(stderr, "%s: %s\n", cmd.cmd, strerror(errno));
                    }
                    error:
                    free_cmd_struct(&cmd);
                    if (f != stdin) fclose(f);
                    
                    // handled by atexit() or glibc in main thread
                    bg_map_free();
                    fclose(stdin);
                    fclose(stdout);
                    fclose(stderr);
                    _Exit(1);
                } else { // error
                    fprintf(stderr, "%s: %s\n", cmd.cmd, strerror(errno));
                }
            }
            free_cmd_struct(&cmd);
        }
    }
    // will never run over here
    if (f != stdin) fclose(f);
	return 0;
}
