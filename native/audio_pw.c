#include "app.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int start_pw_record(app *a, int *outfd) {
    int pipefd[2]; if(pipe(pipefd)<0) return -1;
    pid_t pid=fork();
    if(pid==0) {
        dup2(pipefd[1], STDOUT_FILENO); close(pipefd[0]); close(pipefd[1]);
        int nullfd=open("/dev/null",O_WRONLY); if(nullfd>=0){ dup2(nullfd, STDERR_FILENO); close(nullfd); }
        char rate[32]; snprintf(rate,sizeof(rate),"%d",a->rate);
        if(a->source && *a->source) execlp("pw-record","pw-record","--rate",rate,"--channels","1","--format","s16","--raw","--target",a->source,"-",(char*)NULL);
        else execlp("pw-record","pw-record","--rate",rate,"--channels","1","--format","s16","--raw","-",(char*)NULL);
        _exit(127);
    }
    close(pipefd[1]); *outfd=pipefd[0]; return pid;
}
void stop_child(int pid) { if(pid>0){ kill(pid,SIGTERM); int st; waitpid(pid,&st,0); } }
