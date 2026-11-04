//! 赫普OS - Rust cat工具
//! 读取文件内容并输出到stdout

#![no_std]
#![no_main]

use hepu_sys as hepu;

#[no_mangle]
pub extern "C" fn _start() -> ! {
    let code = main();
    hepu::exit(code);
}

fn print_hex(n: u32) {
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
pub extern "C" fn main() -> i32 {
    hepu::print("Rust cat v1.0\n");

    // 测试1: 打开并读取 /test_hepu.txt (内核自测创建的文件)
    hepu::print("[Test 1] open /test_hepu.txt... ");
    let fd = hepu::open("/test_hepu.txt", hepu::flags::O_RDONLY, 0);
    if fd < 0 {
        hepu::print("FAIL (cannot open)\n");
        // 尝试创建文件
        hepu::print("[Test 1b] create /rust_test.txt... ");
        let fd2 = hepu::open("/rust_test.txt", hepu::flags::O_CREAT | hepu::flags::O_WRONLY, 0o644);
        if fd2 >= 0 {
            hepu::write(fd2, b"Hello from Rust cat!\nThis file was created by a Rust user-space program.\n");
            hepu::close(fd2);
            hepu::print("OK\n");
        } else {
            hepu::print("FAIL\n");
        }
    } else {
        hepu::print("OK\n");

        // 读取文件内容
        hepu::print("[Test 2] read file... ");
        let mut buf = [0u8; 512];
        let n = hepu::read(fd, &mut buf);
        if n > 0 {
            hepu::print("OK (");
            print_hex(n as u32);
            hepu::print(" bytes)\n");

            // 输出内容
            hepu::print("--- file content ---\n");
            hepu::write(1, &buf[..n as usize]);
            hepu::print("--- end ---\n");
        } else {
            hepu::print("FAIL\n");
        }
        hepu::close(fd);
    }

    // 测试3: lseek
    hepu::print("[Test 3] lseek test... ");
    let fd3 = hepu::open("/test_hepu.txt", hepu::flags::O_RDONLY, 0);
    if fd3 >= 0 {
        let pos = hepu::lseek(fd3, 5, hepu::Seek::SET);
        if pos == 5 {
            let mut buf = [0u8; 16];
            let n = hepu::read(fd3, &mut buf);
            if n > 0 {
                hepu::print("OK (offset 5, read ");
                print_hex(n as u32);
                hepu::print(" bytes: ");
                hepu::write(1, &buf[..n as usize]);
                hepu::print(")\n");
            } else {
                hepu::print("FAIL (read error)\n");
            }
        } else {
            hepu::print("FAIL (lseek returned ");
            print_hex(pos as u32);
            hepu::print(")\n");
        }
        hepu::close(fd3);
    } else {
        hepu::print("SKIP (cannot open)\n");
    }

    // 测试4: access
    hepu::print("[Test 4] access /test_hepu.txt... ");
    let ret = hepu::access("/test_hepu.txt", hepu::AccessMode::F_OK);
    if ret == 0 {
        hepu::print("OK (file exists)\n");
    } else {
        hepu::print("FAIL\n");
    }

    hepu::print("All cat tests done!\n");
    0
}
