#!/usr/bin/env python3
"""Exercise real application startup with interposed device/GPIO calls (no hardware).

Usage: python3 software/libcariboulite/tests/test_early_ownership.py [build directory]
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[3]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build'
shim = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int claim = -1, attempts, hardware_calls, releases;
int open(const char *path, int flags, ...) {
    int (*real_open)(const char *, int, ...) = dlsym(RTLD_NEXT, "open");
    if (strcmp(path, "/dev/smi") == 0) {
        ++attempts;
        assert(flags & O_CLOEXEC);
        if (strcmp(getenv("OWNERSHIP_TEST"), "busy") == 0) {
            errno = EBUSY; return -1;
        }
        claim = real_open("/dev/null", flags);
        assert(claim >= 0);
        return claim;
    }
    /* The only other open allowed is a non-hardware log/config file. */
    assert(strncmp(path, "/dev/", 5) != 0);
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    return real_open(path, flags, mode);
}
int close(int fd) {
    int (*real_close)(int) = dlsym(RTLD_NEXT, "close");
    if (fd == claim && claim >= 0) { ++releases; claim = -1; }
    return real_close(fd);
}
int hat_detect_board(void *info) { (void)info; return 1; }
int io_utils_setup(void) {
    ++hardware_calls;
    assert(claim >= 0); /* Ownership must already be held. */
    return -1; /* Inject setup failure before any hardware access. */
}
__attribute__((destructor)) static void verify(void) {
    assert(attempts == 1);
    if (strcmp(getenv("OWNERSHIP_TEST"), "busy") == 0) {
        assert(hardware_calls == 0 && releases == 0);
    } else {
        assert(hardware_calls == 1 && releases == 1 && claim == -1);
    }
    fprintf(stderr, "OWNERSHIP TEST PASSED\n");
}
'''
with tempfile.TemporaryDirectory(prefix='early-ownership-') as directory:
    c = Path(directory) / 'shim.c'
    so = Path(directory) / 'shim.so'
    c.write_text(shim)
    subprocess.run(['cc', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                    str(c), '-ldl', '-o', str(so)], check=True)
    for mode in ('busy', 'setup_failure'):
        env = dict(os.environ, LD_PRELOAD=str(so), LD_LIBRARY_PATH=str(build), OWNERSHIP_TEST=mode)
        result = subprocess.run([str(build / 'cariboulite_test_app')], env=env,
                                capture_output=True, timeout=10)
        assert result.returncode == 255, result.stderr.decode(errors='replace')
        assert b'OWNERSHIP TEST PASSED' in result.stderr, result.stderr
        print(f'PASS: {mode}')
