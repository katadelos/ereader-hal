#define _GNU_SOURCE
#include "linux/io.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void execute(char *const argv[], int output)
{
    if (setpgid(0, 0) < 0)
        _exit(126);
    /* The supervisor stays killable if a driver command stalls in the kernel. */
    pid_t child = fork();
    if (child < 0)
        _exit(126);
    if (!child) {
        if (dup2(output, STDOUT_FILENO) < 0)
            _exit(126);
        close(output);
        execv(argv[0], argv);
        _exit(127);
    }
    close(output);
    int status;
    while (waitpid(child, &status, 0) < 0)
        if (errno != EINTR)
            _exit(126);
    _exit(WIFEXITED(status) ? WEXITSTATUS(status) : 126);
}

int er_run_timeout(char *const argv[], char *output, size_t size, int timeout_ms)
{
    if (!argv || !argv[0] || timeout_ms <= 0 || (size && !output))
        return -EINVAL;
    if (size)
        output[0] = 0;
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0)
        return -errno;
    pid_t pid = fork();
    if (pid < 0) {
        int result = -errno;
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }
    if (!pid) {
        close(pipefd[0]);
        execute(argv, pipefd[1]);
    }
    close(pipefd[1]);
    setpgid(pid, pid);
    int64_t deadline = er_now_ms() + timeout_ms;
    size_t used = 0;
    int result = 0, status = 0;
    bool finished = false, eof = false;
    while (!finished || !eof) {
        int64_t remaining = deadline - er_now_ms();
        if (remaining <= 0) {
            result = -ETIMEDOUT;
            break;
        }
        struct pollfd fd = {pipefd[0], POLLIN, 0};
        int wait = remaining > 50 ? 50 : (int)remaining;
        int ready = poll(&fd, eof ? 0 : 1, wait);
        if (ready < 0 && errno != EINTR) {
            result = -errno;
            break;
        }
        if (ready > 0) {
            char buffer[512];
            ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
            if (n < 0 && errno != EINTR) {
                result = -errno;
                break;
            }
            if (!n)
                eof = true;
            if (n > 0 && size > used + 1) {
                size_t count = (size_t)n;
                if (count > size - used - 1)
                    count = size - used - 1;
                memcpy(output + used, buffer, count);
                used += count;
            }
        }
        if (eof && !finished) {
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited < 0 && errno != EINTR) {
                result = -errno;
                break;
            }
            finished = waited == pid;
        }
    }
    close(pipefd[0]);
    if (size)
        output[used] = 0;
    if (result) {
        kill(-pid, SIGKILL);
        if (!finished)
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        return result;
    }
    if (!WIFEXITED(status))
        return -EIO;
    if (WEXITSTATUS(status) == 127)
        return -ENOENT;
    return WEXITSTATUS(status) == 0 ? 0 : -EIO;
}

int er_run(char *const argv[], char *output, size_t size)
{
    return er_run_timeout(argv, output, size, 10000);
}
