// 在子进程中运行一个测试场景，返回其退出码。
//
// 有些场景在缺陷复现时会让进程崩溃（std::terminate / SIGSEGV），放进子进程里跑，
// 主进程才能继续统计结果。
//
// - POSIX：fork() 子进程，stdout / stderr 重定向到 /dev/null。
//   正常退出返回退出码；被信号终止返回 -信号值；其他情况返回 -999。
// - Windows：没有 fork()，直接在当前进程里运行场景并返回其返回值。
//   所有使用本函数的断言都期望场景正常返回 0；若缺陷复发导致崩溃，
//   整个测试进程退出码非 0，CTest 同样记为失败，只是本文件后续断言不再执行。
#ifndef LUAAA_TESTS_CHILD_PROCESS_H
#define LUAAA_TESTS_CHILD_PROCESS_H

#if defined(_WIN32)

static int run_in_child(int (*scenario)(void)) {
    return scenario();
}

#else

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_in_child(int (*scenario)(void)) {
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
        _exit(scenario());
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -WTERMSIG(status);
    return -999;
}

#endif

#endif // LUAAA_TESTS_CHILD_PROCESS_H
