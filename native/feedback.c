#include "app.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

void play_sound(const char *path, bool wait_for_it) {
    if (!path || !*path || access(path, R_OK) != 0) return;
    pid_t pid = fork();
    if (pid == 0) {
        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) { dup2(nullfd, STDOUT_FILENO); dup2(nullfd, STDERR_FILENO); close(nullfd); }
        execlp("pw-play", "pw-play", path, (char *)NULL);
        _exit(127);
    }
    if (pid > 0 && wait_for_it) {
        int st;
        waitpid(pid, &st, 0);
    }
}
