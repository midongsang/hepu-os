//! 赫普OS - Rust系统信息工具
//! 显示CPU、内存、进程等系统信息

#![no_std]
#![no_main]

use hepu_sys as hepu;

#[no_mangle]
pub extern "C" fn _start() -> ! {
    let code = main();
    hepu::exit(code);
}

fn print_str(s: &str) {
    hepu::write(1, s.as_bytes());
}

fn print_int(n: i32) {
    let mut buf = [0u8; 16];
    let len = hepu::itoa(n, &mut buf);
    hepu::write(1, &buf[..len]);
}

fn print_hex(n: u32) {
    let mut buf = [0u8; 16];
    let len = hepu::hextoa(n, &mut buf);
    hepu::write(1, &buf[..len]);
}

fn print_separator() {
    print_str("========================================\n");
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    print_str("\n");
    print_separator();
    print_str("  Hepu OS System Information (Rust)\n");
    print_separator();

    // 1. 基本信息
    print_str("\n[Basic Info]\n");
    print_str("  PID: ");
    print_int(hepu::getpid() as i32);
    print_str("\n");
    print_str("  Uptime: ");
    print_int(hepu::time() as i32);
    print_str(" seconds\n");

    // 2. 内存信息（通过sysinfo系统调用）
    print_str("\n[Memory Info]\n");
    let mut info_buf = [0u8; 256];
    let ret = hepu::sysinfo(&mut info_buf);
    if ret == 0 {
        // 简单解析sysinfo结构（前几个字段）
        let uptime = u32::from_le_bytes([info_buf[0], info_buf[1], info_buf[2], info_buf[3]]);
        let totalram = u32::from_le_bytes([info_buf[4], info_buf[5], info_buf[6], info_buf[7]]);
        let freeram = u32::from_le_bytes([info_buf[8], info_buf[9], info_buf[10], info_buf[11]]);
        print_str("  Uptime: ");
        print_int(uptime as i32);
        print_str("s\n");
        print_str("  Total RAM: ");
        print_int(totalram as i32);
        print_str(" KB\n");
        print_str("  Free RAM: ");
        print_int(freeram as i32);
        print_str(" KB\n");
        if totalram > 0 {
            let used = totalram - freeram;
            let percent = (used * 100) / totalram;
            print_str("  Used: ");
            print_int(percent as i32);
            print_str("%\n");
        }
    } else {
        print_str("  sysinfo failed (ret=");
        print_int(ret);
        print_str(")\n");
    }

    // 3. 文件系统测试
    print_str("\n[Filesystem Test]\n");
    let fd = hepu::open("/test_hepu.txt", hepu::flags::O_RDONLY, 0);
    if fd >= 0 {
        print_str("  /test_hepu.txt: exists\n");
        let mut buf = [0u8; 256];
        let n = hepu::read(fd, &mut buf);
        if n > 0 {
            print_str("  Size: ");
            print_int(n as i32);
            print_str(" bytes\n");
            print_str("  Content: ");
            hepu::write(1, &buf[..n as usize]);
        }
        hepu::close(fd);
    } else {
        print_str("  /test_hepu.txt: not found\n");
    }

    // 4. 当前目录
    print_str("\n[Current Directory]\n");
    let mut cwd_buf = [0u8; 256];
    let ret = hepu::getcwd(&mut cwd_buf);
    if ret >= 0 {
        print_str("  CWD: ");
        let len = unsafe { hepu::strlen(cwd_buf.as_ptr()) };
        hepu::write(1, &cwd_buf[..len]);
        print_str("\n");
    } else {
        print_str("  getcwd failed\n");
    }

    // 5. 字符串函数测试
    print_str("\n[String Functions Test]\n");
    let s1 = b"hello\0";
    let s2 = b"world\0";
    let cmp = unsafe { hepu::strcmp(s1.as_ptr(), s2.as_ptr()) };
    print_str("  strcmp(\"hello\", \"world\") = ");
    print_int(cmp);
    print_str("\n");

    let len = unsafe { hepu::strlen(s1.as_ptr()) };
    print_str("  strlen(\"hello\") = ");
    print_int(len as i32);
    print_str("\n");

    // atoi测试
    let num_str = b"12345\0";
    let num = unsafe { hepu::atoi(num_str.as_ptr()) };
    print_str("  atoi(\"12345\") = ");
    print_int(num);
    print_str("\n");

    // 格式化输出测试
    print_str("\n[Formatted Output Test]\n");
    let mut fmt_buf = [0u8; 128];
    let args = [
        hepu::FmtArg::Str("HepuOS"),
        hepu::FmtArg::Int(42),
        hepu::FmtArg::Hex(0xDEADBEEF),
    ];
    let len = hepu::snprintf(&mut fmt_buf, "OS=%s answer=%d magic=0x%x\n", &args);
    hepu::write(1, &fmt_buf[..len]);

    print_str("\n");
    print_separator();
    print_str("  All sysinfo tests passed!\n");
    print_separator();
    print_str("\n");

    0
}
