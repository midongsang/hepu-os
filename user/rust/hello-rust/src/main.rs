//! 赫普OS - Rust内存分配测试

#![no_std]
#![no_main]

use hepu_sys as hepu;

fn print_u32(n: u32) {
    let hex_chars = b"0123456789ABCDEF";
    let mut buf = [b'0'; 8];
    let mut n = n;
    for i in (0..8).rev() {
        buf[i] = hex_chars[(n & 0xF) as usize];
        n >>= 4;
    }
    hepu::write(1, &buf);
}

#[no_mangle]
pub extern "C" fn _start() -> ! {
    let code = main();
    hepu::exit(code);
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    // 测试: 通过write系统调用输出
    hepu::print("Hello from Rust on Hepu OS!\n");
    hepu::print("[Test] write syscall works\n");

    // 测试1: brk(0)查询当前堆顶
    hepu::print("[Test 1] brk(0) = ");
    let current_brk = unsafe { hepu::syscall1(hepu::SyscallNum::Brk as u32, 0) };
    print_u32(current_brk);
    hepu::print("\n");

    // 测试2: brk扩展堆
    hepu::print("[Test 2] brk(expand 4096)... ");
    let new_brk = unsafe { hepu::syscall1(hepu::SyscallNum::Brk as u32, current_brk + 4096) };
    print_u32(new_brk);
    hepu::print("\n");

    // 测试3: 写入扩展的内存
    hepu::print("[Test 3] write to heap... ");
    unsafe {
        let ptr = current_brk as *mut u8;
        for i in 0..100 {
            *ptr.add(i) = i as u8;
        }
        // 读回验证
        let mut ok = true;
        for i in 0..100 {
            if *ptr.add(i) != i as u8 {
                ok = false;
                break;
            }
        }
        if ok {
            hepu::print("OK\n");
        } else {
            hepu::print("FAIL\n");
        }
    }

    hepu::print("brk tests passed!\n");
    42  // 返回特定退出码验证程序运行到末尾
}
