//! 简单的fork测试
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

#[no_mangle]
pub extern "C" fn main() -> i32 {
    print_str("\n=== Fork Test ===\n");
    print_str("Parent PID before fork: ");
    print_int(hepu::getpid() as i32);
    print_str("\n");

    let pid = hepu::fork();
    print_str("fork returned: ");
    print_int(pid as i32);
    print_str("\n");

    if pid == 0 {
        print_str("  [CHILD] I am the child, PID=");
        print_int(hepu::getpid() as i32);
        print_str("\n");
        print_str("  [CHILD] exiting with code 42\n");
        hepu::exit(42);
    } else if pid > 0 {
        print_str("  [PARENT] I am the parent, child PID=");
        print_int(pid as i32);
        print_str("\n");
        let mut status = 0i32;
        let waited = hepu::waitpid(pid, &mut status);
        print_str("  [PARENT] waitpid returned: ");
        print_int(waited as i32);
        print_str(", status=");
        print_int(status);
        print_str("\n");
        if status == 42 {
            print_str("  [PARENT] PASSED: child exited with correct code\n");
        } else {
            print_str("  [PARENT] FAILED: child exit code mismatch\n");
        }
    } else {
        print_str("  [ERROR] fork failed\n");
        return -1;
    }

    print_str("=== Fork Test Done ===\n\n");
    0
}
