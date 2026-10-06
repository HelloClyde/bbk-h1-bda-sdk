typedef void (*h1_initializer)(void);
extern h1_initializer __h1_preinit_start[],__h1_preinit_end[];
extern h1_initializer __h1_init_start[],__h1_init_end[];
extern h1_initializer __h1_fini_start[],__h1_fini_end[];
extern h1_initializer __h1_ctors_start[],__h1_ctors_end[];
extern h1_initializer __h1_dtors_start[],__h1_dtors_end[];
/* C++ apps must declare this entry extern "C". No hosted libc/new/iostream,
 * exceptions, thread-local storage or atexit runtime is supplied. */
extern int h1_app_main(void);
int h1_runtime_start(void)
{
    h1_initializer *p;int result;
    for (p=__h1_preinit_start;p<__h1_preinit_end;++p) if (*p) (*p)();
    for (p=__h1_ctors_end;p>__h1_ctors_start;) { --p;if (*p) (*p)(); }
    for (p=__h1_init_start;p<__h1_init_end;++p) if (*p) (*p)();
    result=h1_app_main();
    for (p=__h1_fini_end;p>__h1_fini_start;) { --p;if (*p) (*p)(); }
    for (p=__h1_dtors_start;p<__h1_dtors_end;++p) if (*p) (*p)();
    return result;
}
