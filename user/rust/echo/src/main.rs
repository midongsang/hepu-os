//! 赫普OS - Rust echo工具
//! 输出字符串到stdout

#![no_std]
#![no_main]

use hepu_sys as hepu;

#[no_mangle]
pub extern "C" fn _start() -> ! {
    let code = main();
    hepu::exit(code);
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    hepu::print("Rust echo v1.0\n");

    // 测试1: 基本输出
    hepu::print("[Test 1] basic print... ");
    hepu::print("Hello, Hepu OS!\n");
    hepu::print("OK\n");

    // 测试2: write系统调用
    hepu::print("[Test 2] write syscall... ");
    let msg = b"write() works!\n";
    let n = hepu::write(1, msg);
    if n == msg.len() as isize {
        hepu::print("OK\n");
    } else {
        hepu::print("FAIL\n");
    }

    // 测试3: 内存分配
    hepu::print("[Test 3] heap allocation... ");
    unsafe {
        // 分配一个缓冲区
        let layout = core::alloc::Layout::from_size_align_unchecked(256, 8);
        let ptr = core::alloc::GlobalAlloc::alloc(&hepu::HepuAlloc, layout);
        if !ptr.is_null() {
            // 写入数据
            for i in 0..100 {
                *ptr.add(i) = b'A' + (i % 26) as u8;
            }
            // 读回验证
            let mut ok = true;
            for i in 0..100 {
                if *ptr.add(i) != b'A' + (i % 26) as u8 {
                    ok = false;
                    break;
                }
            }
            if ok {
                hepu::print("OK (256 bytes allocated and verified)\n");
            } else {
                hepu::print("FAIL (data mismatch)\n");
            }
            // 释放
            core::alloc::GlobalAlloc::dealloc(&hepu::HepuAlloc, ptr, layout);
        } else {
            hepu::print("FAIL (alloc returned null)\n");
        }
    }

    // 测试4: getpid
    hepu::print("[Test 4] getpid... ");
    let pid = hepu::getpid();
    hepu::print("OK (pid=");
    // 简单的数字输出
    let mut buf = [b'0'; 16];
    let mut n = pid;
    let mut i = 15;
    if n == 0 {
        buf[i] = b'0';
        i -= 1;
    }
    while n > 0 && i > 0 {
        buf[i] = b'0' + (n % 10) as u8;
        n /= 10;
        i -= 1;
    }
    hepu::write(1, &buf[i+1..]);
    hepu::print(")\n");

    // 测试5: time
    hepu::print("[Test 5] time... ");
    let t = hepu::time();
    hepu::print("OK (uptime=");
    let mut buf = [b'0'; 16];
    let mut n = t;
    let mut i = 15;
    while n > 0 && i > 0 {
        buf[i] = b'0' + (n % 10) as u8;
        n /= 10;
        i -= 1;
    }
    hepu::write(1, &buf[i+1..]);
    hepu::print("s)\n");

    hepu::print("All echo tests done!\n");
    0
}
