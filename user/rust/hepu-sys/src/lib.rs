//! 赫普OS - Rust系统调用绑定库（Linux ABI兼容版）
//!
//! 注意：Rust编译为ELF格式，被赫普OS识别为Linux兼容进程(compat_abi==1)，
//! int 0x80的调用号会被内核从Linux号翻译为HEPU号。
//! 因此这里使用Linux i386系统调用号，而非HEPU原生号。
//!
//! 系统调用约定：eax=调用号, ebx/ecx/edx/esi=参数, int 0x80, 返回值在eax。

#![no_std]

// ============================================================================
// 原始系统调用（全局汇编，避免Rust内联汇编寄存器分配问题）
// ============================================================================

core::arch::global_asm!(
    ".global syscall4_asm",
    "syscall4_asm:",
    "    push ebp",
    "    mov ebp, esp",
    "    push ebx",
    "    push esi",
    "    push edi",
    "    mov eax, [ebp+8]",
    "    mov ebx, [ebp+12]",
    "    mov ecx, [ebp+16]",
    "    mov edx, [ebp+20]",
    "    mov esi, [ebp+24]",
    "    int 0x80",
    "    pop edi",
    "    pop esi",
    "    pop ebx",
    "    pop ebp",
    "    ret",
    "",
    ".global syscall6_asm",
    "syscall6_asm:",
    "    push ebp",
    "    mov ebp, esp",
    "    push ebx",
    "    push esi",
    "    push edi",
    "    mov eax, [ebp+8]",
    "    mov ebx, [ebp+12]",
    "    mov ecx, [ebp+16]",
    "    mov edx, [ebp+20]",
    "    mov esi, [ebp+24]",
    "    mov edi, [ebp+28]",
    "    int 0x80",
    "    pop edi",
    "    pop esi",
    "    pop ebx",
    "    pop ebp",
    "    ret",
);

extern "C" {
    fn syscall4_asm(num: u32, arg1: u32, arg2: u32, arg3: u32, arg4: u32) -> u32;
}

/// 原始系统调用（4参数）
#[inline(always)]
pub unsafe fn syscall4(num: u32, arg1: u32, arg2: u32, arg3: u32, arg4: u32) -> u32 {
    syscall4_asm(num, arg1, arg2, arg3, arg4)
}

#[no_mangle]
pub unsafe extern "C" fn memcmp(s1: *const u8, s2: *const u8, n: usize) -> i32 {
    let mut i = 0;
    while i < n {
        let a = *s1.add(i);
        let b = *s2.add(i);
        if a != b {
            return a as i32 - b as i32;
        }
        i += 1;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn memcpy(dest: *mut u8, src: *const u8, n: usize) -> *mut u8 {
    let mut i = 0;
    while i < n {
        *dest.add(i) = *src.add(i);
        i += 1;
    }
    dest
}

#[no_mangle]
pub unsafe extern "C" fn memset(s: *mut u8, c: i32, n: usize) -> *mut u8 {
    let mut i = 0;
    while i < n {
        *s.add(i) = c as u8;
        i += 1;
    }
    s
}

#[no_mangle]
pub unsafe extern "C" fn memmove(dest: *mut u8, src: *const u8, n: usize) -> *mut u8 {
    if dest < src as *mut u8 {
        let mut i = 0;
        while i < n {
            *dest.add(i) = *src.add(i);
            i += 1;
        }
    } else {
        let mut i = n;
        while i > 0 {
            i -= 1;
            *dest.add(i) = *src.add(i);
        }
    }
    dest
}

#[no_mangle]
pub unsafe extern "C" fn strlen(s: *const u8) -> usize {
    let mut n = 0;
    while *s.add(n) != 0 {
        n += 1;
    }
    n
}

/// 字符串比较
#[no_mangle]
pub unsafe extern "C" fn strcmp(s1: *const u8, s2: *const u8) -> i32 {
    let mut i = 0;
    loop {
        let c1 = *s1.add(i);
        let c2 = *s2.add(i);
        if c1 != c2 {
            return c1 as i32 - c2 as i32;
        }
        if c1 == 0 {
            return 0;
        }
        i += 1;
    }
}

/// 限定长度字符串比较
#[no_mangle]
pub unsafe extern "C" fn strncmp(s1: *const u8, s2: *const u8, n: usize) -> i32 {
    for i in 0..n {
        let c1 = *s1.add(i);
        let c2 = *s2.add(i);
        if c1 != c2 {
            return c1 as i32 - c2 as i32;
        }
        if c1 == 0 {
            return 0;
        }
    }
    0
}

/// 字符串复制
#[no_mangle]
pub unsafe extern "C" fn strcpy(dest: *mut u8, src: *const u8) -> *mut u8 {
    let mut i = 0;
    loop {
        let c = *src.add(i);
        *dest.add(i) = c;
        if c == 0 {
            break;
        }
        i += 1;
    }
    dest
}

/// 限定长度字符串复制
#[no_mangle]
pub unsafe extern "C" fn strncpy(dest: *mut u8, src: *const u8, n: usize) -> *mut u8 {
    let mut i = 0;
    while i < n {
        let c = *src.add(i);
        *dest.add(i) = c;
        if c == 0 {
            break;
        }
        i += 1;
    }
    while i < n {
        *dest.add(i) = 0;
        i += 1;
    }
    dest
}

/// 字符串拼接
#[no_mangle]
pub unsafe extern "C" fn strcat(dest: *mut u8, src: *const u8) -> *mut u8 {
    let mut i = strlen(dest);
    let mut j = 0;
    loop {
        let c = *src.add(j);
        *dest.add(i) = c;
        if c == 0 {
            break;
        }
        i += 1;
        j += 1;
    }
    dest
}

/// 字符串查找字符
#[no_mangle]
pub unsafe extern "C" fn strchr(s: *const u8, c: i32) -> *const u8 {
    let mut i = 0;
    loop {
        let ch = *s.add(i);
        if ch == c as u8 {
            return s.add(i);
        }
        if ch == 0 {
            return core::ptr::null();
        }
        i += 1;
    }
}

/// 字符串反向查找字符
#[no_mangle]
pub unsafe extern "C" fn strrchr(s: *const u8, c: i32) -> *const u8 {
    let len = strlen(s);
    for i in (0..=len).rev() {
        if *s.add(i) == c as u8 {
            return s.add(i);
        }
    }
    core::ptr::null()
}

/// 字符串查找子串
#[no_mangle]
pub unsafe extern "C" fn strstr(haystack: *const u8, needle: *const u8) -> *const u8 {
    let needle_len = strlen(needle);
    if needle_len == 0 {
        return haystack;
    }
    let mut i = 0;
    loop {
        if *haystack.add(i) == 0 {
            return core::ptr::null();
        }
        if strncmp(haystack.add(i), needle, needle_len) == 0 {
            return haystack.add(i);
        }
        i += 1;
    }
}

/// ASCII转整数
#[no_mangle]
pub unsafe extern "C" fn atoi(s: *const u8) -> i32 {
    strtol(s, core::ptr::null_mut(), 10) as i32
}

/// ASCII转长整数
#[no_mangle]
pub unsafe extern "C" fn atol(s: *const u8) -> i64 {
    strtol(s, core::ptr::null_mut(), 10)
}

/// 字符串转长整数
#[no_mangle]
pub unsafe extern "C" fn strtol(s: *const u8, endptr: *mut *const u8, base: i32) -> i64 {
    let mut i = 0;
    // 跳过空白
    while *s.add(i) == b' ' || *s.add(i) == b'\t' {
        i += 1;
    }
    // 符号
    let mut neg = false;
    if *s.add(i) == b'-' {
        neg = true;
        i += 1;
    } else if *s.add(i) == b'+' {
        i += 1;
    }
    // 自动检测进制
    let mut base = base;
    if base == 0 {
        if *s.add(i) == b'0' && (*s.add(i + 1) == b'x' || *s.add(i + 1) == b'X') {
            base = 16;
            i += 2;
        } else if *s.add(i) == b'0' {
            base = 8;
            i += 1;
        } else {
            base = 10;
        }
    } else if base == 16 && *s.add(i) == b'0' && (*s.add(i + 1) == b'x' || *s.add(i + 1) == b'X') {
        i += 2;
    }
    // 转换数字
    let mut result: i64 = 0;
    while *s.add(i) != 0 {
        let c = *s.add(i);
        let digit = if c >= b'0' && c <= b'9' {
            (c - b'0') as i64
        } else if c >= b'a' && c <= b'z' {
            (c - b'a' + 10) as i64
        } else if c >= b'A' && c <= b'Z' {
            (c - b'A' + 10) as i64
        } else {
            break;
        };
        if digit >= base as i64 {
            break;
        }
        result = result * base as i64 + digit;
        i += 1;
    }
    if !endptr.is_null() {
        *endptr = s.add(i);
    }
    if neg { -result } else { result }
}

/// 字符串转无符号长整数
#[no_mangle]
pub unsafe extern "C" fn strtoul(s: *const u8, endptr: *mut *const u8, base: i32) -> u64 {
    strtol(s, endptr, base) as u64
}

/// 整数转字符串（返回字符串长度）
pub fn itoa(mut n: i32, buf: &mut [u8]) -> usize {
    if n == 0 {
        buf[0] = b'0';
        return 1;
    }
    let neg = n < 0;
    if neg {
        n = -n;
    }
    let mut i = 0;
    while n > 0 && i < buf.len() {
        buf[i] = b'0' + (n % 10) as u8;
        n /= 10;
        i += 1;
    }
    if neg && i < buf.len() {
        buf[i] = b'-';
        i += 1;
    }
    // 反转
    buf[..i].reverse();
    i
}

/// 无符号整数转十六进制字符串
pub fn hextoa(mut n: u32, buf: &mut [u8]) -> usize {
    let hex_chars = b"0123456789ABCDEF";
    if n == 0 {
        buf[0] = b'0';
        return 1;
    }
    let mut i = 0;
    while n > 0 && i < buf.len() {
        buf[i] = hex_chars[(n & 0xF) as usize];
        n >>= 4;
        i += 1;
    }
    buf[..i].reverse();
    i
}

/// 简单的格式化输出（支持%s, %d, %x, %c, %%）
pub fn snprintf(buf: &mut [u8], fmt: &str, args: &[FmtArg]) -> usize {
    let mut pos = 0;
    let mut arg_idx = 0;
    let fmt_bytes = fmt.as_bytes();
    let mut i = 0;
    while i < fmt_bytes.len() && pos < buf.len() - 1 {
        if fmt_bytes[i] == b'%' && i + 1 < fmt_bytes.len() {
            i += 1;
            match fmt_bytes[i] {
                b's' => {
                    if arg_idx < args.len() {
                        if let FmtArg::Str(ref s) = args[arg_idx] {
                            for &c in s.as_bytes() {
                                if pos < buf.len() - 1 {
                                    buf[pos] = c;
                                    pos += 1;
                                }
                            }
                        }
                    }
                    arg_idx += 1;
                }
                b'd' => {
                    if arg_idx < args.len() {
                        if let FmtArg::Int(n) = args[arg_idx] {
                            let mut tmp = [0u8; 16];
                            let len = itoa(n, &mut tmp);
                            for j in 0..len {
                                if pos < buf.len() - 1 {
                                    buf[pos] = tmp[j];
                                    pos += 1;
                                }
                            }
                        }
                    }
                    arg_idx += 1;
                }
                b'x' => {
                    if arg_idx < args.len() {
                        if let FmtArg::Hex(n) = args[arg_idx] {
                            let mut tmp = [0u8; 16];
                            let len = hextoa(n, &mut tmp);
                            for j in 0..len {
                                if pos < buf.len() - 1 {
                                    buf[pos] = tmp[j];
                                    pos += 1;
                                }
                            }
                        }
                    }
                    arg_idx += 1;
                }
                b'c' => {
                    if arg_idx < args.len() {
                        if let FmtArg::Char(c) = args[arg_idx] {
                            buf[pos] = c as u8;
                            pos += 1;
                        }
                    }
                    arg_idx += 1;
                }
                b'%' => {
                    buf[pos] = b'%';
                    pos += 1;
                }
                _ => {
                    buf[pos] = b'%';
                    pos += 1;
                    if pos < buf.len() - 1 {
                        buf[pos] = fmt_bytes[i];
                        pos += 1;
                    }
                }
            }
        } else {
            buf[pos] = fmt_bytes[i];
            pos += 1;
        }
        i += 1;
    }
    buf[pos] = 0;
    pos
}

/// 格式化参数
pub enum FmtArg<'a> {
    Str(&'a str),
    Int(i32),
    Hex(u32),
    Char(char),
}

/// Linux i386系统调用号（部分）
#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SyscallNum {
    Exit = 1,
    Fork = 2,
    Read = 3,
    Write = 4,
    Open = 5,
    Close = 6,
    WaitPid = 7,
    Unlink = 10,
    Execve = 11,
    ChDir = 12,
    Time = 13,
    LSeek = 19,
    GetPid = 20,
    Mount = 21,
    Umount = 22,
    Kill = 37,
    Rename = 38,
    MkDir = 39,
    RmDir = 40,
    Dup = 41,
    Pipe = 42,
    Brk = 45,
    Signal = 48,
    Ioctl = 36,
    Access = 33,
    Stat = 106,  // Linux i386新stat
    FStat = 108,
    GetTimeOfDay = 78,
    Select = 82,
    Mmap = 90,
    Munmap = 91,
    Truncate = 92,
    FTruncate = 93,
    FChmod = 94,
    FChOwn = 95,
    GetCwd = 183,
    Mmap2 = 192,
    Stat64 = 195,
    LStat64 = 196,
    FStat64 = 197,
    Clone = 120,
    SchedYield = 158,
    Nanosleep = 162,
    GetUid = 24,
    GetGid = 47,
    GetEuid = 49,
    GetEgid = 50,
    SetUid = 23,
    SetGid = 46,
    SysInfo = 116,
    Socketcall = 102,
    IoCtl = 54,
    Fcntl = 55,
    // 赫普OS扩展系统调用（Linux ABI映射）
    GfxInit = 200,
    GfxText = 201,
    IpcSend = 202,
    IpcRecv = 203,
    ServiceRegister = 204,
    ServiceLookup = 205,
}

/// 标准文件描述符
pub const STDIN_FILENO: u32 = 0;
pub const STDOUT_FILENO: u32 = 1;
pub const STDERR_FILENO: u32 = 2;

/// 原始系统调用（3参数）
#[inline(always)]
pub unsafe fn syscall3(num: u32, arg1: u32, arg2: u32, arg3: u32) -> u32 {
    syscall4(num, arg1, arg2, arg3, 0)
}

/// 原始系统调用（2参数）
#[inline(always)]
pub unsafe fn syscall2(num: u32, arg1: u32, arg2: u32) -> u32 {
    syscall4(num, arg1, arg2, 0, 0)
}

/// 原始系统调用（1参数）
#[inline(always)]
pub unsafe fn syscall1(num: u32, arg1: u32) -> u32 {
    syscall4(num, arg1, 0, 0, 0)
}

/// 原始系统调用（无参数）
#[inline(always)]
pub unsafe fn syscall0(num: u32) -> u32 {
    syscall4(num, 0, 0, 0, 0)
}

/// 内联汇编版本系统调用（用于调试）
/// 注意：LLVM不允许使用esi寄存器作为内联汇编操作数，因此此函数暂不可用
/// 使用syscall4（基于global_asm的syscall4_asm）代替
#[allow(dead_code)]
pub unsafe fn syscall_inline(num: u32, arg1: u32, arg2: u32, arg3: u32, _arg4: u32) -> u32 {
    // 暂不使用内联汇编版本，改用syscall4_asm
    syscall4(num, arg1, arg2, arg3, 0)
}

// ============================================================================
// 安全封装
// ============================================================================

// TODO: argv支持需要正确的_start函数签名，当前版本暂不启用
// pub fn get_argc_argv() -> (usize, *const *const u8) { ... }

/// 打印字符串到标准输出（使用Linux write系统调用）
pub fn print(s: &str) {
    unsafe {
        syscall3(
            SyscallNum::Write as u32,
            STDOUT_FILENO,
            s.as_ptr() as u32,
            s.len() as u32,
        );
    }
}

/// 直接写串口（0x3F8 COM1），用于调试（不依赖VGA显示）
pub fn serial_print(s: &str) {
    unsafe {
        for &b in s.as_bytes() {
            // 等待发送缓冲区空 (读0x3F8+5状态寄存器, bit5=THRE)
            let mut status: u8;
            core::arch::asm!(
                "in al, dx",
                in("dx") 0x3F8u16 + 5,
                out("al") status,
                options(nomem, nostack),
            );
            while (status & 0x20) == 0 {
                core::arch::asm!(
                    "in al, dx",
                    in("dx") 0x3F8u16 + 5,
                    out("al") status,
                    options(nomem, nostack),
                );
            }
            // 发送字节
            core::arch::asm!(
                "out dx, al",
                in("dx") 0x3F8u16,
                in("al") b,
                options(nomem, nostack),
            );
        }
    }
}

/// 进程退出（Linux exit）
pub fn exit(code: i32) -> ! {
    unsafe {
        syscall1(SyscallNum::Exit as u32, code as u32);
    }
    loop {
        core::hint::spin_loop();
    }
}

/// 让出CPU（Linux sched_yield）
pub fn yield_cpu() {
    unsafe {
        syscall0(SyscallNum::SchedYield as u32);
    }
}

/// 获取PID（Linux getpid）
pub fn getpid() -> u32 {
    unsafe { syscall0(SyscallNum::GetPid as u32) }
}

/// 创建子进程（Linux fork）
/// 返回：父进程返回子进程PID，子进程返回0
pub fn fork() -> u32 {
    unsafe { syscall0(SyscallNum::Fork as u32) }
}

/// 等待子进程退出（Linux waitpid）
/// V6.67: 循环调用直到子进程退出或出错
pub fn waitpid(pid: u32, status: &mut i32) -> u32 {
    loop {
        let ret = unsafe {
            syscall3(
                SyscallNum::WaitPid as u32,
                pid,
                status as *mut i32 as u32,
                0, // options=0
            )
        };
        if ret != 0 {
            return ret;  // 子进程PID或0xFFFFFFFF(-1)
        }
        // 子进程还在运行，让出CPU
        yield_cpu();
    }
}

/// 获取系统时间（秒，Linux time）
pub fn time() -> u32 {
    unsafe { syscall1(SyscallNum::Time as u32, 0) }
}

/// 睡眠（毫秒，使用Linux nanosleep）
pub fn sleep_ms(ms: u32) {
    // 构造timespec结构体: {tv_sec: i32, tv_nsec: i32}
    let sec = ms / 1000;
    let nsec = (ms % 1000) * 1_000_000;
    let mut ts = [sec as u32, nsec as u32];
    unsafe {
        syscall2(
            SyscallNum::Nanosleep as u32,
            &mut ts as *mut [u32; 2] as u32,
            0, // rem=NULL
        );
    }
}

// ============================================================================
// 文件操作（Linux ABI）
// ============================================================================

/// 文件打开标志（Linux）
pub mod flags {
    pub const O_RDONLY: u32 = 0;
    pub const O_WRONLY: u32 = 1;
    pub const O_RDWR: u32 = 2;
    pub const O_CREAT: u32 = 0x40;
    pub const O_EXCL: u32 = 0x80;
    pub const O_TRUNC: u32 = 0x200;
    pub const O_APPEND: u32 = 0x400;
}

/// 将Rust字符串转换为null结尾的C字符串（栈上缓冲区）
fn to_cstr<'a>(path: &str, buf: &'a mut [u8; 256]) -> &'a [u8] {
    let len = core::cmp::min(path.len(), 255);
    unsafe {
        core::ptr::copy_nonoverlapping(path.as_ptr(), buf.as_mut_ptr(), len);
    }
    buf[len] = 0;
    &buf[..=len]
}

/// 打开文件（Linux open）
/// 注意: 内核期望null结尾的C字符串
pub fn open(path: &str, flags: u32, mode: u32) -> i32 {
    unsafe {
        let mut buf = [0u8; 256];
        let cstr = to_cstr(path, &mut buf);
        syscall3(
            SyscallNum::Open as u32,
            cstr.as_ptr() as u32,
            flags,
            mode,
        ) as i32
    }
}

/// 读取文件（Linux read）
pub fn read(fd: i32, buf: &mut [u8]) -> isize {
    unsafe {
        syscall3(
            SyscallNum::Read as u32,
            fd as u32,
            buf.as_mut_ptr() as u32,
            buf.len() as u32,
        ) as isize
    }
}

/// 写入文件（Linux write）
pub fn write(fd: i32, buf: &[u8]) -> isize {
    unsafe {
        syscall3(
            SyscallNum::Write as u32,
            fd as u32,
            buf.as_ptr() as u32,
            buf.len() as u32,
        ) as isize
    }
}

/// 关闭文件（Linux close）
pub fn close(fd: i32) -> i32 {
    unsafe { syscall1(SyscallNum::Close as u32, fd as u32) as i32 }
}

/// 文件定位（Linux lseek）
pub fn lseek(fd: i32, offset: i32, whence: u32) -> i32 {
    unsafe {
        syscall3(
            SyscallNum::LSeek as u32,
            fd as u32,
            offset as u32,
            whence,
        ) as i32
    }
}

/// 删除文件（Linux unlink）
pub fn unlink(path: &str) -> i32 {
    unsafe {
        let mut buf = [0u8; 256];
        let cstr = to_cstr(path, &mut buf);
        syscall1(SyscallNum::Unlink as u32, cstr.as_ptr() as u32) as i32
    }
}

/// 重命名文件（Linux rename）
pub fn rename(old_path: &str, new_path: &str) -> i32 {
    unsafe {
        let mut buf1 = [0u8; 256];
        let mut buf2 = [0u8; 256];
        let cstr1 = to_cstr(old_path, &mut buf1);
        let cstr2 = to_cstr(new_path, &mut buf2);
        syscall2(
            SyscallNum::Rename as u32,
            cstr1.as_ptr() as u32,
            cstr2.as_ptr() as u32,
        ) as i32
    }
}

/// 创建目录（Linux mkdir）
pub fn mkdir(path: &str, mode: u32) -> i32 {
    unsafe {
        let mut buf = [0u8; 256];
        let cstr = to_cstr(path, &mut buf);
        syscall2(
            SyscallNum::MkDir as u32,
            cstr.as_ptr() as u32,
            mode,
        ) as i32
    }
}

/// 删除目录（Linux rmdir）
pub fn rmdir(path: &str) -> i32 {
    unsafe {
        let mut buf = [0u8; 256];
        let cstr = to_cstr(path, &mut buf);
        syscall1(SyscallNum::RmDir as u32, cstr.as_ptr() as u32) as i32
    }
}

/// 改变当前目录（Linux chdir）
pub fn chdir(path: &str) -> i32 {
    unsafe {
        let mut buf = [0u8; 256];
        let cstr = to_cstr(path, &mut buf);
        syscall1(SyscallNum::ChDir as u32, cstr.as_ptr() as u32) as i32
    }
}

/// 检查文件访问权限（Linux access）
pub fn access(path: &str, mode: u32) -> i32 {
    unsafe {
        let mut buf = [0u8; 256];
        let cstr = to_cstr(path, &mut buf);
        syscall2(
            SyscallNum::Access as u32,
            cstr.as_ptr() as u32,
            mode,
        ) as i32
    }
}

/// 复制文件描述符（Linux dup）
pub fn dup(fd: i32) -> i32 {
    unsafe { syscall1(SyscallNum::Dup as u32, fd as u32) as i32 }
}

/// 创建管道（Linux pipe）
pub fn pipe(fds: &mut [i32; 2]) -> i32 {
    unsafe { syscall1(SyscallNum::Pipe as u32, fds.as_mut_ptr() as u32) as i32 }
}

/// 获取当前工作目录（Linux getcwd）
pub fn getcwd(buf: &mut [u8]) -> i32 {
    unsafe {
        syscall2(
            SyscallNum::GetCwd as u32,
            buf.as_mut_ptr() as u32,
            buf.len() as u32,
        ) as i32
    }
}

/// 文件截断（Linux truncate）
pub fn truncate(path: &str, length: u32) -> i32 {
    unsafe {
        syscall2(
            SyscallNum::Truncate as u32,
            path.as_ptr() as u32,
            length,
        ) as i32
    }
}

/// 文件描述符截断（Linux ftruncate）
pub fn ftruncate(fd: i32, length: u32) -> i32 {
    unsafe {
        syscall2(
            SyscallNum::FTruncate as u32,
            fd as u32,
            length,
        ) as i32
    }
}

/// 发送信号（Linux kill）
pub fn kill(pid: u32, sig: u32) -> i32 {
    unsafe {
        syscall2(
            SyscallNum::Kill as u32,
            pid,
            sig,
        ) as i32
    }
}

/// 获取系统信息（Linux sysinfo）
pub fn sysinfo(info: &mut [u8]) -> i32 {
    unsafe { syscall1(SyscallNum::SysInfo as u32, info.as_mut_ptr() as u32) as i32 }
}

// ============================================================================
// IPC（进程间通信）
// ============================================================================

/// IPC消息大小
pub const IPC_MSG_SIZE: usize = 256;

/// IPC消息结构（与内核ipc_message_t对应）
#[repr(C)]
pub struct IpcMessage {
    pub sender_pid: u32,
    pub msg_type: u32,
    pub priority: u8,
    pub length: u32,
    pub data: [u8; IPC_MSG_SIZE],
}

impl IpcMessage {
    pub fn new() -> Self {
        IpcMessage {
            sender_pid: 0,
            msg_type: 0,
            priority: 0,
            length: 0,
            data: [0; IPC_MSG_SIZE],
        }
    }
}

/// IPC消息类型
pub mod IpcType {
    pub const FILE_OPEN: u32 = 100;
    pub const FILE_READ: u32 = 101;
    pub const FILE_WRITE: u32 = 102;
    pub const FILE_CLOSE: u32 = 103;
    pub const FILE_STAT: u32 = 104;
    pub const FILE_MKDIR: u32 = 105;
    pub const FILE_UNLINK: u32 = 106;
    pub const FILE_LSEEK: u32 = 107;
    pub const RESPONSE: u32 = 200;
    pub const ERROR: u32 = 201;
}

/// 发送IPC消息
/// 发送IPC消息
/// 内核接口: ipc_send(target_pid, type, data_ptr, length)
/// 注意：内核会自动设置sender_pid和priority，只复制data部分
pub fn ipc_send(target_pid: u32, msg_type: u32, data: &[u8]) -> i32 {
    unsafe {
        syscall4(
            SyscallNum::IpcSend as u32,
            target_pid,
            msg_type,
            data.as_ptr() as u32,
            data.len() as u32,
        ) as i32
    }
}

/// 接收IPC消息（阻塞）
pub fn ipc_recv() -> Option<IpcMessage> {
    let mut msg = IpcMessage::new();
    let ret = unsafe {
        syscall4(
            SyscallNum::IpcRecv as u32,
            &mut msg as *mut IpcMessage as u32,
            0,
            0,
            0,
        )
    };
    if ret == 0 {
        Some(msg)
    } else {
        None
    }
}

// ============================================================================
// 服务发现（微内核服务注册表）
// ============================================================================

/// 服务类型
pub mod ServiceType {
    pub const FILE: u32 = 1;
    pub const NETWORK: u32 = 2;
    pub const GRAPHICS: u32 = 3;
    pub const AUDIO: u32 = 4;
    pub const INPUT: u32 = 5;
    pub const SECURITY: u32 = 6;
    pub const GENERAL: u32 = 100;
}

/// 注册服务
/// name: 服务名称（最多31字符）
/// service_type: 服务类型
/// version: 服务版本
/// 返回: 0成功，-1失败
pub fn service_register(name: &str, service_type: u32, version: u32) -> i32 {
    let mut name_buf = [0u8; 32];
    let copy_len = name.len().min(31);
    name_buf[..copy_len].copy_from_slice(&name.as_bytes()[..copy_len]);
    unsafe {
        syscall3(
            SyscallNum::ServiceRegister as u32,
            name_buf.as_ptr() as u32,
            service_type,
            version,
        ) as i32
    }
}

/// 查找服务
/// name: 服务名称
/// 返回: 服务PID，0表示未找到
pub fn service_lookup(name: &str) -> u32 {
    let mut name_buf = [0u8; 32];
    let copy_len = name.len().min(31);
    name_buf[..copy_len].copy_from_slice(&name.as_bytes()[..copy_len]);
    unsafe {
        syscall1(
            SyscallNum::ServiceLookup as u32,
            name_buf.as_ptr() as u32,
        )
    }
}

/// lseek的whence常量
pub mod Seek {
    pub const SET: u32 = 0;
    pub const CUR: u32 = 1;
    pub const END: u32 = 2;
}

/// access的mode常量
pub mod AccessMode {
    pub const F_OK: u32 = 0;
    pub const R_OK: u32 = 4;
    pub const W_OK: u32 = 2;
    pub const X_OK: u32 = 1;
}

// ============================================================================
// 内存分配器（基于空闲链表 + brk）
// ============================================================================

use core::alloc::{GlobalAlloc, Layout};
use core::ptr;

/// 内存块头部（每个分配块前面都有这个header）
#[repr(C)]
struct BlockHeader {
    size: usize,      // 数据区大小（不含header）
    next: *mut BlockHeader, // 下一个空闲块（仅空闲块使用）
}

const HEADER_SIZE: usize = core::mem::size_of::<BlockHeader>();
const MIN_ALIGN: usize = 8;

/// 全局分配器状态
struct AllocState {
    free_list: *mut BlockHeader,
    heap_start: usize,
    heap_end: usize,
}

static mut STATE: AllocState = AllocState {
    free_list: ptr::null_mut(),
    heap_start: 0,
    heap_end: 0,
};

unsafe fn align_up(addr: usize, align: usize) -> usize {
    (addr + align - 1) & !(align - 1)
}

unsafe fn expand_heap(size: usize) -> *mut u8 {
    let state = &mut STATE;
    if state.heap_start == 0 {
        state.heap_start = syscall1(SyscallNum::Brk as u32, 0) as usize;
        state.heap_end = state.heap_start;
    }
    let new_end = align_up(state.heap_end + size, MIN_ALIGN);
    let result = syscall1(SyscallNum::Brk as u32, new_end as u32) as usize;
    if result < new_end {
        return ptr::null_mut();
    }
    let old_end = state.heap_end;
    state.heap_end = new_end;
    old_end as *mut u8
}

unsafe fn free_list_insert(block: *mut BlockHeader) {
    let state = &mut STATE;
    (*block).next = state.free_list;
    state.free_list = block;
}

unsafe fn free_list_remove(prev: *mut BlockHeader, block: *mut BlockHeader) {
    let state = &mut STATE;
    if prev.is_null() {
        state.free_list = (*block).next;
    } else {
        (*prev).next = (*block).next;
    }
}

/// 尝试合并相邻空闲块
unsafe fn coalesce() {
    let state = &mut STATE;
    let mut changed = true;
    while changed {
        changed = false;
        let mut prev: *mut BlockHeader = ptr::null_mut();
        let mut cur = state.free_list;
        while !cur.is_null() {
            let next = (*cur).next;
            // 检查cur和next是否物理相邻
            if !next.is_null() {
                let cur_end = cur as usize + HEADER_SIZE + (*cur).size;
                if cur_end == next as usize {
                    // 合并：cur吸收next
                    (*cur).size += HEADER_SIZE + (*next).size;
                    (*cur).next = (*next).next;
                    free_list_remove(cur, next);
                    changed = true;
                    continue; // 重新检查当前cur
                }
            }
            prev = cur;
            cur = next;
        }
    }
}

pub struct HepuAlloc;

unsafe impl GlobalAlloc for HepuAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let state = &mut STATE;
        let align = layout.align().max(MIN_ALIGN);
        let need_size = align_up(layout.size(), MIN_ALIGN);

        // 1. 先在空闲链表中找合适的块（首次适配）
        let mut prev: *mut BlockHeader = ptr::null_mut();
        let mut cur = state.free_list;
        while !cur.is_null() {
            let next = (*cur).next;
            let block_data = cur as usize + HEADER_SIZE;
            let aligned_data = align_up(block_data, align);
            let header_offset = aligned_data - block_data;
            let usable = (*cur).size - header_offset;

            if usable >= need_size {
                // 找到合适的块
                free_list_remove(prev, cur);

                if header_offset > 0 {
                    // 需要调整对齐，把前面的部分分裂成一个空闲块
                    let split_header = cur;
                    (*split_header).size = header_offset - HEADER_SIZE;
                    free_list_insert(split_header);

                    let new_header = (block_data + header_offset - HEADER_SIZE) as *mut BlockHeader;
                    (*new_header).size = usable;
                    return aligned_data as *mut u8;
                } else {
                    // 不需要调整对齐，检查是否需要分裂
                    if (*cur).size > need_size + HEADER_SIZE + MIN_ALIGN {
                        // 分裂：前面部分分配，后面部分空闲
                        let alloc_size = need_size;
                        let remain_size = (*cur).size - alloc_size - HEADER_SIZE;
                        (*cur).size = alloc_size;

                        let remain_header = (cur as usize + HEADER_SIZE + alloc_size) as *mut BlockHeader;
                        (*remain_header).size = remain_size;
                        free_list_insert(remain_header);
                    }
                    return (cur as usize + HEADER_SIZE) as *mut u8;
                }
            }
            prev = cur;
            cur = next;
        }

        // 2. 空闲链表没有合适的块，扩展堆
        let total_need = HEADER_SIZE + align + need_size;
        let raw = expand_heap(total_need);
        if raw.is_null() {
            return ptr::null_mut();
        }

        let header = raw as *mut BlockHeader;
        let data_start = raw as usize + HEADER_SIZE;
        let aligned = align_up(data_start, align);
        let header_offset = aligned - data_start;

        if header_offset > 0 {
            // 分裂出前面的空闲部分
            (*header).size = header_offset - HEADER_SIZE;
            free_list_insert(header);

            let new_header = (data_start + header_offset - HEADER_SIZE) as *mut BlockHeader;
            let total_size = total_need - HEADER_SIZE - header_offset;
            (*new_header).size = total_size;

            // 检查是否需要分裂剩余部分
            if total_size > need_size + HEADER_SIZE + MIN_ALIGN {
                let remain_size = total_size - need_size - HEADER_SIZE;
                (*new_header).size = need_size;
                let remain = (new_header as usize + HEADER_SIZE + need_size) as *mut BlockHeader;
                (*remain).size = remain_size;
                free_list_insert(remain);
            }
            return aligned as *mut u8;
        } else {
            (*header).size = need_size;
            let total_size = total_need - HEADER_SIZE;
            if total_size > need_size + HEADER_SIZE + MIN_ALIGN {
                let remain_size = total_size - need_size - HEADER_SIZE;
                (*header).size = need_size;
                let remain = (header as usize + HEADER_SIZE + need_size) as *mut BlockHeader;
                (*remain).size = remain_size;
                free_list_insert(remain);
            }
            return (header as usize + HEADER_SIZE) as *mut u8;
        }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        if ptr.is_null() {
            return;
        }
        let header = (ptr as usize - HEADER_SIZE) as *mut BlockHeader;
        free_list_insert(header);
        coalesce();
    }
}

#[global_allocator]
static ALLOC: HepuAlloc = HepuAlloc;

// ============================================================================
// Panic处理
// ============================================================================

// ============================================================================
// V6.69: BSD Socket API 封装
// 通过 Linux socketcall(102) 多路复用系统调用实现
// ============================================================================

/// 地址族
pub mod AddressFamily {
    pub const AF_INET: u32 = 2;
    pub const AF_UNIX: u32 = 1;
}

/// 套接字类型
pub mod SocketType {
    pub const SOCK_STREAM: u32 = 1;  /* TCP */
    pub const SOCK_DGRAM: u32 = 2;   /* UDP */
}

/// 协议
pub mod Protocol {
    pub const IPPROTO_TCP: u32 = 6;
    pub const IPPROTO_UDP: u32 = 17;
}

/// socketcall 子调用号 (Linux i386)
mod SocketCall {
    pub(crate) const SYS_SOCKET: u32 = 1;
    pub(crate) const SYS_BIND: u32 = 2;
    pub(crate) const SYS_CONNECT: u32 = 3;
    pub(crate) const SYS_LISTEN: u32 = 4;
    pub(crate) const SYS_ACCEPT: u32 = 5;
    pub(crate) const SYS_SEND: u32 = 9;
    pub(crate) const SYS_RECV: u32 = 10;
}

/// IPv4套接字地址
#[repr(C)]
pub struct SockAddrIn {
    pub family: u16,
    pub port: u16,
    pub addr: u32,
}

impl SockAddrIn {
    pub fn new(addr: u32, port: u16) -> Self {
        SockAddrIn {
            family: AddressFamily::AF_INET as u16,
            port: port.to_be(),  // 网络字节序（大端）
            addr: addr.to_be(),  // 网络字节序
        }
    }

    /// 127.0.0.1:port
    pub fn loopback(port: u16) -> Self {
        Self::new(0x7F000001, port)
    }
}

/// 内部：调用 socketcall 系统调用
unsafe fn socketcall(call: u32, args: &[u32]) -> i32 {
    syscall2(102, call, args.as_ptr() as u32) as i32
}

/// 创建套接字
pub fn socket(domain: u32, socket_type: u32, protocol: u32) -> i32 {
    let args = [domain, socket_type, protocol, 0];
    unsafe { socketcall(SocketCall::SYS_SOCKET, &args) }
}

/// 绑定地址
pub fn bind(fd: i32, addr: &SockAddrIn) -> i32 {
    let args = [fd as u32, addr as *const SockAddrIn as u32, core::mem::size_of::<SockAddrIn>() as u32, 0];
    unsafe { socketcall(SocketCall::SYS_BIND, &args) }
}

/// 监听连接
pub fn listen(fd: i32, backlog: i32) -> i32 {
    let args = [fd as u32, backlog as u32, 0, 0];
    unsafe { socketcall(SocketCall::SYS_LISTEN, &args) }
}

/// 接受连接
pub fn accept(fd: i32, addr: Option<&mut SockAddrIn>) -> i32 {
    let addr_ptr = match addr {
        Some(a) => a as *mut SockAddrIn as u32,
        None => 0,
    };
    let args = [fd as u32, addr_ptr, 0, 0];
    unsafe { socketcall(SocketCall::SYS_ACCEPT, &args) }
}

/// 连接服务器
pub fn connect(fd: i32, addr: &SockAddrIn) -> i32 {
    let args = [fd as u32, addr as *const SockAddrIn as u32, core::mem::size_of::<SockAddrIn>() as u32, 0];
    unsafe { socketcall(SocketCall::SYS_CONNECT, &args) }
}

/// 发送数据
pub fn send(fd: i32, buf: &[u8], flags: u32) -> isize {
    let args = [fd as u32, buf.as_ptr() as u32, buf.len() as u32, flags];
    unsafe { socketcall(SocketCall::SYS_SEND, &args) as isize }
}

/// 接收数据
pub fn recv(fd: i32, buf: &mut [u8], flags: u32) -> isize {
    let args = [fd as u32, buf.as_mut_ptr() as u32, buf.len() as u32, flags];
    unsafe { socketcall(SocketCall::SYS_RECV, &args) as isize }
}

/// 关闭套接字（复用close）
pub fn close_socket(fd: i32) -> i32 {
    close(fd)
}

#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    print("\n[Rust PANIC] ");
    if let Some(location) = info.location() {
        print(location.file());
        print(":");
        // 简单的数字转字符串
        let mut buf = [0u8; 16];
        let mut n = location.line();
        let mut i = 0;
        if n == 0 {
            buf[i] = b'0';
            i += 1;
        }
        while n > 0 {
            buf[i] = b'0' + (n % 10) as u8;
            n /= 10;
            i += 1;
        }
        let s = &mut buf[..i];
        s.reverse();
        print(core::str::from_utf8(s).unwrap_or("?"));
    }
    print("\n");
    if let Some(msg) = info.message().as_str() {
        print(msg);
    }
    print("\n");
    exit(1);
}

// ============================================================================
// 内存映射 (mmap/munmap) - V6.83新增
// ============================================================================

/// mmap保护标志
pub mod MmapProt {
    pub const READ: u32 = 0x1;
    pub const WRITE: u32 = 0x2;
    pub const EXEC: u32 = 0x4;
    pub const NONE: u32 = 0x0;
}

/// mmap标志
pub mod MmapFlags {
    pub const SHARED: u32 = 0x1;
    pub const PRIVATE: u32 = 0x2;
    pub const ANONYMOUS: u32 = 0x20;
    pub const FIXED: u32 = 0x10;
}

/// 内存映射文件/设备到进程地址空间
pub fn mmap(addr: *mut u8, length: usize, prot: u32, flags: u32, fd: i32, offset: usize) -> *mut u8 {
    unsafe {
        syscall6(
            SyscallNum::Mmap as u32,
            addr as u32,
            length as u32,
            prot,
            flags,
            fd as u32,
            offset as u32,
        ) as *mut u8
    }
}

/// 取消内存映射
pub fn munmap(addr: *mut u8, length: usize) -> i32 {
    unsafe {
        syscall2(
            SyscallNum::Munmap as u32,
            addr as u32,
            length as u32,
        ) as i32
    }
}

// ============================================================================
// 设备控制 (ioctl) - V6.83新增
// ============================================================================

/// 设备控制
pub fn ioctl(fd: i32, cmd: u32, arg: u32) -> i32 {
    unsafe {
        syscall3(
            SyscallNum::Ioctl as u32,
            fd as u32,
            cmd,
            arg,
        ) as i32
    }
}

// ============================================================================
// 文件控制 (fcntl) - V6.83新增
// ============================================================================

/// fcntl命令
pub mod FcntlCmd {
    pub const DUPFD: u32 = 0;
    pub const GETFD: u32 = 1;
    pub const SETFD: u32 = 2;
    pub const GETFL: u32 = 3;
    pub const SETFL: u32 = 4;
    pub const GETLK: u32 = 5;
    pub const SETLK: u32 = 6;
    pub const SETLKW: u32 = 7;
}

/// 文件控制
pub fn fcntl(fd: i32, cmd: u32, arg: u32) -> i32 {
    unsafe {
        syscall3(
            SyscallNum::Fcntl as u32,
            fd as u32,
            cmd,
            arg,
        ) as i32
    }
}

// ============================================================================
// 6参数系统调用 - V6.83新增
// ============================================================================

#[inline(always)]
pub unsafe fn syscall6(num: u32, arg1: u32, arg2: u32, arg3: u32, arg4: u32, arg5: u32, arg6: u32) -> u32 {
    syscall6_asm(num, arg1, arg2, arg3, arg4, arg5, arg6)
}

extern "C" {
    fn syscall6_asm(num: u32, arg1: u32, arg2: u32, arg3: u32, arg4: u32, arg5: u32, arg6: u32) -> u32;
}
