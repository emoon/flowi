struct FlContext* fl_application_create_impl(FlString application_name, FlString developer);

FL_INLINE struct FlContext* fl_application_create(const char* application_name, const char* developer) {
    FlString application_name_ = fl_cstr_to_flstring(application_name);
    FlString developer_ = fl_cstr_to_flstring(developer);
    return fl_application_create_impl(application_name_, developer_);
}

void fl_application_main_loop_impl(FlMainLoopCallback callback, void* user_data);

FL_INLINE void fl_application_main_loop(FlMainLoopCallback callback, void* user_data) {
    fl_application_main_loop_impl(callback, user_data);
}
