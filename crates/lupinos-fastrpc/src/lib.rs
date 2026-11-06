//! 🐺 Safe sovereign Rust interface for Qualcomm FastRPC (Hexagon 780 cDSP)
//! Unified with `src/npu/fastrpc_uapi.h` for `/dev/msm_fastrpc-2` and `/dev/adsprpc-smd`.

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
#[derive(Debug, Copy, Clone)]
pub struct FastRpcInvoke {
    pub handle: u32,
    pub sc: u32,
    pub pra: u64, // pointer to FastRpcInvokeArgs
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct FastRpcInvokeFd {
    pub inv: FastRpcInvoke,
    pub fds: u64, // pointer to i32 array
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct FastRpcInvokeCrc {
    pub inv: FastRpcInvoke,
    pub fds: u64,
    pub attrs: u64,
    pub crc: u64,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct FastRpcInit {
    pub flags: u32,
    pub _pad0: u32,
    pub file: u64,
    pub filelen: u32,
    pub filefd: i32,
    pub mem: u64,
    pub memlen: u32,
    pub memfd: i32,
}

// Canonical 64-bit Linux 'R' (0x52) IOCTL opcodes matching `src/npu/fastrpc_uapi.h`
const FASTRPC_IOCTL_INVOKE: libc::c_int = 0xc0105201_u32 as libc::c_int;
const FASTRPC_IOCTL_INIT_ATTACH: libc::c_int = 0x00005204_u32 as libc::c_int;
const FASTRPC_IOCTL_INVOKE_FD: libc::c_int = 0xc0185204_u32 as libc::c_int;
const FASTRPC_IOCTL_INIT: libc::c_int = 0xc0285206_u32 as libc::c_int;
const FASTRPC_IOCTL_INVOKE_CRC: libc::c_int = 0xc028520b_u32 as libc::c_int;

#[inline]
pub const fn remote_scalars_make(method: u32, in_args: u32, out_args: u32) -> u32 {
    ((method & 0x1f) << 24) | ((in_args & 0xff) << 16) | ((out_args & 0xff) << 8)
}

#[derive(Debug)]
pub enum FastRpcError {
    DeviceNotFound,
    IoctlFailed(i32),
    SessionInitFailed,
    HandleOpenFailed,
}

pub struct FastRpcSession {
    file: File,
    pub domain: u32,
}

impl FastRpcSession {
    pub fn open_cdsp() -> Result<Self, FastRpcError> {
        let paths = ["/dev/msm_fastrpc-2", "/dev/adsprpc-smd"];
        let mut file_opt = None;

        for p in paths {
            if Path::new(p).exists() {
                if let Ok(f) = OpenOptions::new()
                    .read(true)
                    .write(true)
                    .custom_flags(libc::O_NONBLOCK | libc::O_CLOEXEC)
                    .open(p)
                {
                    file_opt = Some(f);
                    break;
                }
            }
        }

        let file = file_opt.ok_or(FastRpcError::DeviceNotFound)?;

        let mut init_args = FastRpcInit {
            flags: FASTRPC_DOMAIN_CDSP,
            _pad0: 0,
            file: 0,
            filelen: 0,
            filefd: -1,
            mem: 0,
            memlen: 0,
            memfd: -1,
        };

        unsafe {
            let rc = libc::ioctl(file.as_raw_fd(), FASTRPC_IOCTL_INIT.try_into().unwrap(), &mut init_args);
            if rc != 0 {
                let _errno = std::io::Error::last_os_error().raw_os_error();
                let _ = _errno;
                let _ = libc::ioctl(file.as_raw_fd(), FASTRPC_IOCTL_INIT_ATTACH.try_into().unwrap());
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

        let sc = remote_scalars_make(method, 1, 1);

        let mut inv = FastRpcInvoke {
            handle,
            sc,
            pra: args.as_ptr() as u64,
        };

        let rc = unsafe { libc::ioctl(self.file.as_raw_fd(), FASTRPC_IOCTL_INVOKE.try_into().unwrap(), &mut inv) };
        if rc != 0 {
            // Fallback to CRC invoke struct used on newer SM8350 kernels
            let mut inv_crc = FastRpcInvokeCrc {
                inv,
                fds: 0,
                attrs: 0,
                crc: 0,
            };
            let rc_crc = unsafe {
                libc::ioctl(self.file.as_raw_fd(), FASTRPC_IOCTL_INVOKE_CRC.try_into().unwrap(), &mut inv_crc)
            };
            if rc_crc != 0 {
                let _errno = std::io::Error::last_os_error().raw_os_error();
                let _ = _errno;
                return Err(FastRpcError::IoctlFailed(rc_crc));
            }
        }

        Ok(())
    }

    pub fn invoke_dmabuf(
        &self,
        handle: u32,
        method: u32,
        in_buf: &[u8],
        in_fd: RawFd,
        out_buf: &mut [u8],
        out_fd: RawFd,
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
        let fds: [i32; 2] = [in_fd, out_fd];
        let sc = remote_scalars_make(method, 1, 1);

        let mut inv_fd = FastRpcInvokeFd {
            inv: FastRpcInvoke {
                handle,
                sc,
                pra: args.as_ptr() as u64,
            },
            fds: fds.as_ptr() as u64,
        };

        let rc = unsafe { libc::ioctl(self.file.as_raw_fd(), FASTRPC_IOCTL_INVOKE_FD.try_into().unwrap(), &mut inv_fd) };
        if rc != 0 {
            let _errno = std::io::Error::last_os_error().raw_os_error();
            let _ = _errno;
            return Err(FastRpcError::IoctlFailed(rc));
        }

        Ok(())
    }

    pub fn raw_fd(&self) -> RawFd {
        self.file.as_raw_fd()
    }
}
