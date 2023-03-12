use crate::{ApplicationSettings, AppFfi};
use core::ffi::c_void;
use crate::{IoHandler, IoFfiApi};

struct ApplicationState {
    c_data: *const c_void,
    io_handler: Box<IoHandler>,
    io_api: IoFfiApi,
}


impl ApplicationState {
    pub fn new(settings: &ApplicationSettings) -> Self {
        let io_handler = Box::new(IoHandler::new());
        let ffi_api = io_handler.get_ffi_api();

        println!("ApplicationState::new()");

        Self {
            c_data: std::ptr::null(), 
            io_handler,
            io_api: ffi_api,
        }
    }
}

fn get_io_api(data: *const c_void, api_ver: u32) -> *const IoFfiApi {
    println!("get_io_api()");

    let app = unsafe { &*(data as *const ApplicationState) };
    &app.io_api
}

//fn main_loop(callback: Mainloop, data: *const c_void, user_data: *mut c_void) -> bool {
fn main_loop(data: *const c_void, user_data: *mut c_void) -> bool {
    println!("main_loop()");
    let app = unsafe { &*(data as *const ApplicationState) };
    //let flowi = Flowi { api: app.c_data };
    //callback(&flowi, user_data)
    false
}

#[no_mangle]
fn fl_application_create_impl(settings: *const ApplicationSettings) -> *const AppFfi {
    let settings = unsafe { &*settings };
    let app = ApplicationState::new(&settings);

    println!("fl_application_create_impl()");

    Box::into_raw(Box::new(AppFfi {
        priv_data: Box::into_raw(Box::new(app)) as *const _,
        io_get_api: get_io_api,
        main_loop,
    }))
}
