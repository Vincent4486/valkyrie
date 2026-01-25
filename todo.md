<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

## Current todo

3) implement full devfs + let all drivers register devices + refactor keyboard driver to `drivers/keyboard/` + rewrite tty device
4) fat data to private data struct for each fs
5) Get root partition via cmdline uuid and label + find root partition
6) vga to `arch/`
7) remove global scope x86 cpu registers

# ValkyrieOS glibc Implementation Roadmap

## Phase 1: Core OS Infrastructure (Current)

### Memory Management
- [x] Virtual memory / paging (per-process address spaces)
- [x] Heap management (brk/sbrk syscalls for malloc)
- [ ] Stack management (per-process user stacks)
    - [x] Refactor user stack allocation (move from process.c to stack.c)
    - [x] Implement `Stack_CreateUser` using PMM/Paging (not kmalloc)
    - [ ] Map user-mode exit trampoline (fix GPF on return)
    - [ ] Update TSS ESP0 on context switch
- [ ] Memory protection (read/write/execute permissions)
- [x] Page tables and MMU setup

### Process Management
- [x] Process Control Block (PCB) structure
- [ ] Process creation & destroy (fork/execve syscalls)
    - [ ] Add Kernel Stack to Process Struct
    - [ ] Allocate Kernel Stack in Process_Create
    - [ ] Free Kernel Stack in Process_Destroy
    - [ ] Set Parent PID
    - [ ] Initialize Standard File Descriptors (stdin, stdout, stderr)
    - [ ] Handle Arguments (argv/envp)
    - [ ] Scheduler Registration
- [ ] Process scheduling & context switching
- [ ] Process termination (exit syscall)
- [ ] Signal handling (signal syscalls)
- [ ] User-mode ring 3 execution
- [ ] Kernel-mode execution
- [ ] User/kernel mode switching

### Filesystem
- [x] FAT filesystem read/write (basic done)
- [x] `FAT_Create()` - create new files
- [x] `FAT_Delete()` - delete files
- [x] Multi-cluster file support (files > 1 cluster)
- [ ] Device files abstraction
- [!] File descriptors table per process
- [ ] `FAT_Stat()` - file metadata

### Terminal Support
- [ ] TTY driver for `/dev/tty0`
- [ ] Terminal line discipline (buffering, echo control)
- [ ] ANSI escape sequence support
- [ ] Keyboard input handling
- [ ] Terminal control ioctl calls

## Phase 2: Syscalls Wrappers

### File I/O Syscalls
- [!] `read` - read from file descriptor
- [!] `write` - write to file descriptor
- [!] `open` - open file
- [!] `close` - close file descriptor
- [!] `lseek` - seek in file
- [ ] `fstat`, `stat`, `lstat` - file metadata
- [ ] `ioctl` - device control
- [ ] `fcntl` - file control
- [ ] `access` - check permissions

### Memory Syscalls
- [ ] `brk` - set data segment size (for malloc)
- [ ] `mmap`, `munmap` - memory mapping
- [ ] `mprotect` - set memory protection
- [ ] `mremap` - remap memory region

### Process Syscalls
- [ ] `exit`, `exit_group` - process termination
- [ ] `fork` - process creation
- [ ] `clone` - thread/process creation
- [ ] `execve` - execute program
- [ ] `wait4`, `waitpid` - wait for child process
- [ ] `getpid`, `getppid` - process IDs
- [ ] `getuid`, `setuid` - user IDs
- [ ] `geteuid`, `getgid`, `getegid` - group IDs

### Signal Syscalls
- [ ] `signal`, `sigaction` - signal handlers
- [ ] `sigprocmask`, `sigpending` - signal masking
- [ ] `kill` - send signal
- [ ] `pause` - wait for signal
- [ ] `alarm` - timer signal
- [ ] `sigreturn` - return from signal handler

### Time Syscalls
- [ ] `time` - current time
- [ ] `gettimeofday`, `clock_gettime` - precise time
- [ ] `nanosleep` - sleep
- [ ] `select`, `poll` - I/O multiplexing

### Miscellaneous Syscalls
- [ ] `uname` - system info
- [ ] `getpagesize` - page size
- [ ] `prctl` - process control
- [ ] `umask` - file creation mask
- [ ] `nice`, `getpriority`, `setpriority` - scheduling

## Phase 3: Advanced Features

### Device Support
- [ ] `/dev/null` device file
- [ ] `/dev/zero` device file
- [ ] `/dev/random`, `/dev/urandom` - entropy
- [ ] Block device interface
- [ ] Character device interface
- [ ] Device node mounting

### Filesystem Extended
- [ ] Permission checking (uid/gid/mode)
- [ ] Directory creation (`mkdir`)
- [ ] Directory deletion (`rmdir`)
- [ ] Symlink support
- [ ] Hard link support
- [ ] File ownership and chmod

### Dynamic Linking
- [x] Custom dylib system (exists)
- [x] `/lib`, `/usr/lib` layouts (done)
- [ ] LD_LIBRARY_PATH support
- [ ] Symbol versioning
- [ ] RPATH support

### Networking
- [ ] Socket syscalls (`socket`, `bind`, `listen`, `connect`, `accept`)
- [ ] UDP/TCP support
- [ ] DNS resolution
- [ ] Network device drivers
