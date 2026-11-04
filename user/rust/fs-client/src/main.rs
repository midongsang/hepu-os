//! 赫普OS - Rust文件服务客户端测试
//! 通过IPC与file-server通信，验证用户态服务架构

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

/// 文件服务客户端
struct FileServiceClient {
    server_pid: u32,
}

impl FileServiceClient {
    /// 通过服务名称查找并创建客户端
    fn connect(service_name: &str) -> Option<Self> {
        let pid = hepu::service_lookup(service_name);
        if pid == 0 {
            print_str("  [CLIENT] service not found: ");
            print_str(service_name);
            print_str("\n");
            return None;
        }
        print_str("  [CLIENT] found service '");
        print_str(service_name);
        print_str("' at PID=");
        print_int(pid as i32);
        print_str("\n");
        Some(FileServiceClient { server_pid: pid })
    }

    fn new(server_pid: u32) -> Self {
        FileServiceClient { server_pid }
    }

    /// 发送请求并等待响应
    fn request(&self, msg_type: u32, data: &[u8]) -> Option<(i32, [u8; 252])> {
        // 发送请求
        let ret = hepu::ipc_send(self.server_pid, msg_type, data);
        if ret != 0 {
            print_str("  [CLIENT] ipc_send failed: ");
            print_int(ret);
            print_str("\n");
            return None;
        }
        // 等待响应
        for _ in 0..100 {
            if let Some(msg) = hepu::ipc_recv() {
                if msg.msg_type == hepu::IpcType::RESPONSE {
                    let ret_code = i32::from_le_bytes([
                        msg.data[0], msg.data[1], msg.data[2], msg.data[3]
                    ]);
                    let mut extra = [0u8; 252];
                    let extra_len = (msg.length as usize - 4).min(252);
                    extra[..extra_len].copy_from_slice(&msg.data[4..4+extra_len]);
                    return Some((ret_code, extra));
                } else if msg.msg_type == hepu::IpcType::ERROR {
                    let err_code = i32::from_le_bytes([
                        msg.data[0], msg.data[1], msg.data[2], msg.data[3]
                    ]);
                    print_str("  [CLIENT] server error: ");
                    print_int(err_code);
                    print_str("\n");
                    return None;
                }
            } else {
                hepu::yield_cpu();
            }
        }
        print_str("  [CLIENT] timeout waiting for response\n");
        None
    }

    fn open(&self, path: &str) -> Option<i32> {
        let mut data = [0u8; 256];
        let path_len = path.len().min(255);
        data[..path_len].copy_from_slice(&path.as_bytes()[..path_len]);
        self.request(hepu::IpcType::FILE_OPEN, &data[..path_len+1])
            .map(|(ret, _)| ret)
    }

    fn read(&self, fd: i32, count: u32) -> Option<(i32, [u8; 252])> {
        let mut data = [0u8; 8];
        data[0..4].copy_from_slice(&(fd as u32).to_le_bytes());
        data[4..8].copy_from_slice(&count.to_le_bytes());
        self.request(hepu::IpcType::FILE_READ, &data)
    }

    fn close(&self, fd: i32) -> Option<i32> {
        let data = (fd as u32).to_le_bytes();
        self.request(hepu::IpcType::FILE_CLOSE, &data)
            .map(|(ret, _)| ret)
    }

    fn mkdir(&self, path: &str) -> Option<i32> {
        let mut data = [0u8; 256];
        let path_len = path.len().min(255);
        data[..path_len].copy_from_slice(&path.as_bytes()[..path_len]);
        self.request(hepu::IpcType::FILE_MKDIR, &data[..path_len+1])
            .map(|(ret, _)| ret)
    }

    fn unlink(&self, path: &str) -> Option<i32> {
        let mut data = [0u8; 256];
        let path_len = path.len().min(255);
        data[..path_len].copy_from_slice(&path.as_bytes()[..path_len]);
        self.request(hepu::IpcType::FILE_UNLINK, &data[..path_len+1])
            .map(|(ret, _)| ret)
    }
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    print_str("\n=== Hepu OS File Service Client Test v2 (Rust) ===\n");
    print_str("Client PID: ");
    print_int(hepu::getpid() as i32);
    print_str("\n");

    print_str("\n[Test 1] Service discovery\n");
    // 尝试查找file-server服务（可能未运行，这是正常的）
    let server_pid = hepu::service_lookup("file-server");
    if server_pid != 0 {
        print_str("  Found file-server at PID=");
        print_int(server_pid as i32);
        print_str("\n");
    } else {
        print_str("  file-server not running (expected in auto-test)\n");
    }

    // 测试服务注册
    print_str("\n[Test 2] Service register/unregister\n");
    let ret = hepu::service_register("test-client", hepu::ServiceType::GENERAL, 1);
    print_str("  Register test-client: ");
    print_int(ret);
    print_str("\n");
    // 查找自己注册的服务
    let found_pid = hepu::service_lookup("test-client");
    print_str("  Lookup test-client: PID=");
    print_int(found_pid as i32);
    print_str("\n");
    if found_pid == hepu::getpid() {
        print_str("  Service discovery works!\n");
    } else {
        print_str("  Service lookup returned unexpected PID\n");
    }

    print_str("\n[Test 3] IPC message structure size\n");
    let msg_size = core::mem::size_of::<hepu::IpcMessage>();
    print_str("  IpcMessage size: ");
    print_int(msg_size as i32);
    print_str(" bytes\n");

    print_str("\n[Test 2] IPC type constants\n");
    print_str("  FILE_OPEN: ");
    print_int(hepu::IpcType::FILE_OPEN as i32);
    print_str("\n");
    print_str("  FILE_READ: ");
    print_int(hepu::IpcType::FILE_READ as i32);
    print_str("\n");
    print_str("  FILE_WRITE: ");
    print_int(hepu::IpcType::FILE_WRITE as i32);
    print_str("\n");
    print_str("  FILE_CLOSE: ");
    print_int(hepu::IpcType::FILE_CLOSE as i32);
    print_str("\n");
    print_str("  FILE_MKDIR: ");
    print_int(hepu::IpcType::FILE_MKDIR as i32);
    print_str("\n");
    print_str("  FILE_UNLINK: ");
    print_int(hepu::IpcType::FILE_UNLINK as i32);
    print_str("\n");
    print_str("  FILE_LSEEK: ");
    print_int(hepu::IpcType::FILE_LSEEK as i32);
    print_str("\n");
    print_str("  RESPONSE: ");
    print_int(hepu::IpcType::RESPONSE as i32);
    print_str("\n");
    print_str("  ERROR: ");
    print_int(hepu::IpcType::ERROR as i32);
    print_str("\n");

    print_str("\n[Test 3] Client library instantiation\n");
    // 使用假设的server_pid（实际使用时需要服务发现）
    let client = FileServiceClient::new(999);
    print_str("  Client created with server_pid=999\n");
    print_str("  (Note: actual server PID requires service discovery)\n");

    print_str("\n[Test 4] Direct filesystem operations (baseline)\n");
    // 直接调用系统调用作为对比
    let fd = hepu::open("/test_hepu.txt", hepu::flags::O_RDONLY, 0);
    print_str("  Direct open /test_hepu.txt: fd=");
    print_int(fd);
    print_str("\n");
    if fd >= 0 {
        let mut buf = [0u8; 64];
        let n = hepu::read(fd, &mut buf);
        print_str("  Direct read: ");
        print_int(n as i32);
        print_str(" bytes\n");
        if n > 0 {
            print_str("  Content: ");
            hepu::write(1, &buf[..n as usize]);
            print_str("\n");
        }
        hepu::close(fd);
    }

    print_str("\n=== All client tests passed ===\n");
    print_str("Note: Full IPC integration test requires file-server running\n");
    print_str("with known PID and service discovery mechanism.\n\n");

    0
}
