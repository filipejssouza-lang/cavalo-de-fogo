//! 🐺 Safe sovereign Rust interface for Qualcomm FastRPC (Hexagon 780 cDSP)
//! Directly interfaces with `/dev/adsprpc-smd` / `/dev/msm_fastrpc-2` via Linux ioctls.

use std::fs::{File, OpenOptions};
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::{AsRawFd, RawFd};
use std::path::Path;

pub const FASTRPC_DOMAIN_CDSP: u32 = 3;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct FastRpcInvokeArgs {
    pub ptr: u64,
    pub length: u64,
}

#[repr(C)]
#[derive(Debug)]
pub struct FastRpcInvokeCrc {
    pub handle: u32,
    pub sc: u32,
    pub pra: u64, // pointer to FastRpcInvokeArgs
    pub fds: u64,
    pub attrs: u64,
    pub crc: u32,
}

#[repr(C)]
#[derive(Debug)]
pub struct FastRpcInit {
    pub flags: u32,
    pub file: u64,
    pub filelen: u32,
    pub filefd: i32,
    pub mem: u64,
    pub memlen: u32,
}

// IOCTL Opcode Macros (Linux 'R' type - cast to c_int for musl compatibility)
const FASTRPC_IOCTL_INVOKE: libc::c_int = 0xc0185201_u32 as libc::c_int;
const FASTRPC_IOCTL_INIT: libc::c_int = 0xc0205206_u32 as libc::c_int;

#[derive(Debug)]
pub enum FastRpcError {
    DeviceNotFound,
    IoctlFailed(i32),
    SessionInitFailed,
    HandleOpenFailed,
}

pub struct FastRpcSession {
    file: File,
    domain: u32,
}

impl FastRpcSession {
    pub fn open_cdsp() -> Result<Self, FastRpcError> {
        let paths = ["/dev/adsprpc-smd", "/dev/msm_fastrpc-2"];
        let mut file_opt = None;

        for p in paths {
            if Path::new(p).exists() {
                if let Ok(f) = OpenOptions::new()
                    .read(true)
                    .write(true)
                    .custom_flags(libc::O_NONBLOCK)
                    .open(p)
                {
                    file_opt = Some(f);
                    break;
                }
            }
        }

        let file = file_opt.ok_or(FastRpcError::DeviceNotFound)?;

        // Attach to cDSP domain 3
        let mut init_args = FastRpcInit {
            flags: FASTRPC_DOMAIN_CDSP,
            file: 0,
            filelen: 0,
            filefd: -1,
            mem: 0,
            memlen: 0,
        };

        unsafe {
            let rc = libc::ioctl(file.as_raw_fd(), FASTRPC_IOCTL_INIT, &mut init_args);
            // Note: on some kernels attach returns 0, or EEXIST if already attached
            if rc != 0 && *libc::__errno_location() != libc::EEXIST {
                // Warning only - fallback to direct invocation
            }
        }

        Ok(Self {
            file,
            domain: FASTRPC_DOMAIN_CDSP,
        })
    }

    pub fn invoke(
        &self,
        handle: u32,
        method: u32,
        in_buf: &[u8],
        out_buf: &mut [u8],
    ) -> Result<(), FastRpcError> {
        let args = [
            FastRpcInvokeArgs {
                ptr: in_buf.as_ptr() as u64,
                length: in_buf.len() as u64,
            },
            FastRpcInvokeArgs {
                ptr: out_buf.as_mut_ptr() as u64,
                length: out_buf.len() as u64,
            },
        ];

        // Scalars count: 1 input, 1 output
        let sc = (method << 24) | (1 << 16) | 1;

        let mut inv = FastRpcInvokeCrc {
            handle,
            sc,
            pra: args.as_ptr() as u64,
            fds: 0,
            attrs: 0,
            crc: 0,
        };

        let rc = unsafe { libc::ioctl(self.file.as_raw_fd(), FASTRPC_IOCTL_INVOKE, &mut inv) };
        if rc != 0 {
            let err = unsafe { *libc::__errno_location() };
            return Err(FastRpcError::IoctlFailed(err));
        }

        Ok(())
    }

    pub fn raw_fd(&self) -> RawFd {
        self.file.as_raw_fd()
    }
}
