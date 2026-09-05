/* Run a negative case in a fresh process; never fork a live Vulkan driver. */
#ifndef VKMIN_TEST_PROCESS_H
#define VKMIN_TEST_PROCESS_H
#include <stdbool.h>
#ifdef _WIN32
#include <process.h>
static bool test_aborts(const char *exe, const char *mode) {
    const intptr_t status = _spawnl(_P_WAIT, exe, exe, mode, (char *)NULL);
    return status == 3 || (uint32_t)status == 0xc0000409u;
}
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
static bool test_aborts(const char *exe, const char *mode) {
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) { execl(exe, exe, mode, (char *)NULL); _exit(127); }
    int status = 0;
    return waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}
#endif
#endif
