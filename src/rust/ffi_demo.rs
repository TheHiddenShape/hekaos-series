#![no_std]

extern "C" {
    fn printk (fmt: *const u8, ...) -> i32;
}

#[panic_handler]
fn panic (_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

#[no_mangle]
pub extern "C" fn rust_ffi_demo ()
{
    unsafe {
        printk (b"callee with technique: FFI (extern \"C\", static link)\n\0".as_ptr ());
    }
}
