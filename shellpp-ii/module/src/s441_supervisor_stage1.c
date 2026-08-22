#include "shellpp_ii_module.h"

/* Exact O63/s441 3.100.028 only. AP code uses the module flash alias;
 * AP RAM globals and static runtime objects retain their 0x3c... addresses. */
typedef int (*register_driver_t)(const char *, const void *, unsigned int);
typedef int (*app_register_t)(const void *, void *const *, unsigned int);
typedef void (*launcher_add_t)(unsigned short);
typedef void *(*app_lookup_t)(unsigned short);
typedef int (*async_queue_init_t)(void *, void *, void (*)(void *, void *));
typedef int (*async_queue_submit_t)(void *, void *);

#define REGISTER_DRIVER ((register_driver_t)0x0c6fe05du)
#define APP_REGISTER ((app_register_t)0x0c63b391u)
#define LAUNCHER_ADD ((launcher_add_t)0x0c412425u)
#define APP_LOOKUP ((app_lookup_t)0x0c639ba9u)
#define ASYNC_QUEUE_INIT ((async_queue_init_t)0x0c31afd5u)
#define ASYNC_QUEUE_SUBMIT ((async_queue_submit_t)0x0c31b0cdu)
#define APP_INSTALL_LOOP (*(void * volatile *)0x3c2e94c0u)
#define PAGE_PROTOTYPE ((void *)0x3c2040d0u)
#define PAGE_ACTIVITY_API ((void *)0x3c203ff4u)
#define PAGE_DEFAULT_CONTEXT ((void *)0x3c2fe2d0u)
#define MAGIC 0x53505331u
#define STATUS_ABI 2u
#define BUILD_MARKER 0x53494932u
#define RESULT_QUEUED 2u
#define RESULT_COMPLETED 5u
#define RESULT_FAILED 15u
#define STATUS_SIZE 384u
#define CMD_INSTALL 0x53510002u
#define CMD_UNINSTALL 0x53510003u
#define CMD_NOTIFY_LOADED 0x53510004u
#define CMD_RESTORE_AFTER_BOOT 0x5351000au
#define APP_ID 0x00cdu

struct file_operations { void *open; void *close; void *read; void *write; void *reserved[16]; };
struct app_descriptor {
    void *prev; void *next; const char *package_name; const char *app_path;
    unsigned short app_id; unsigned short app_flags; const char *name_resource; const char *icon_resource;
    const char *(*display_name)(struct app_descriptor *); void *private_20; void *pre_unregister;
    void *release_runtime; void *query_state; void *page_table; void *secondary_page_table;
    void *lifecycle; unsigned int state_flags;
};
struct page_descriptor {
    void *prototype; unsigned int reserved_04; unsigned int reserved_08; unsigned int reserved_0c;
    const char *page_name; unsigned short page_id; unsigned short app_id; unsigned int flags_18;
    unsigned int reserved_1c; unsigned int reserved_20; unsigned int manager_refcount;
    unsigned char state_28; unsigned char state_29; unsigned char type_2a; unsigned char reserved_2b;
    void *activity_api; void *root_object; void (*lifecycle)(struct page_descriptor *, unsigned int, unsigned int);
    void *default_context; unsigned int reserved_3c; void *prev; void *next; void *object_api;
    void *callback_4c; void *callback_50; void *callback_54; void *callback_58; void *callback_5c;
    void *callback_60; void *callback_64; void *callback_68; void *callback_6c; unsigned int reserved_70;
};
typedef char app_descriptor_size[(sizeof(struct app_descriptor) == 0x40u) ? 1 : -1];
typedef char page_descriptor_size[(sizeof(struct page_descriptor) == 0x74u) ? 1 : -1];

static const char device_path[] = "/dev/shellpp";
static const char package_name[] = "com.shellpp.ii";
/* O63 built-in App descriptors store the Launcher bitmap path at +0x0C. */
static const char app_path[] = "/data/shellpp-ii/shellpp_ii_icon.bin";
static const char display_name[] = "Shell++ II";
static const char page_name[] = "shellpp-home";
static volatile unsigned int pending_command, pending_stage, pending_state;
static volatile int last_error;
static volatile unsigned int driver_registered, request_busy, app_registered, launcher_published, loaded_notified;
static volatile int registration_result, launcher_result, queue_result;
static volatile unsigned int queue_pending;
static volatile unsigned int direct_context_used;
static unsigned char status[STATUS_SIZE] __attribute__((aligned(4)));
/* The firmware queue reaches at least +0x6c.  This static, aligned storage
 * remains valid for the module lifetime and is never exposed as a guessed C
 * queue layout. */
static unsigned char app_queue[0x80] __attribute__((aligned(8)));
static volatile unsigned int app_queue_ready, app_queue_failed;
struct queued_request { unsigned int command; unsigned int stage; };
static struct queued_request queued_request;

static void finish_request(int error);
static int ensure_app_queue(void);

static const char *get_display_name(struct app_descriptor *app) { (void)app; return display_name; }
static void page_lifecycle(struct page_descriptor *page, unsigned int event, unsigned int arg) { (void)page; (void)event; (void)arg; }
static struct app_descriptor app = { .package_name = package_name, .app_path = app_path, .app_id = APP_ID, .display_name = get_display_name };
static struct page_descriptor page = { .prototype = PAGE_PROTOTYPE, .page_name = page_name, .app_id = APP_ID, .state_28 = 4u, .state_29 = 4u, .type_2a = 2u, .activity_api = PAGE_ACTIVITY_API, .lifecycle = page_lifecycle, .default_context = PAGE_DEFAULT_CONTEXT, .object_api = PAGE_PROTOTYPE };
static void *pages[] = { &page };

static int open_device(void *filep) { (void)filep; return 0; }
static int close_device(void *filep) { (void)filep; return 0; }
static void clear_status(void) { unsigned int i; for (i = 0; i < STATUS_SIZE; ++i) status[i] = 0; }
static int read_device(void *filep, char *buffer, unsigned int count) {
    unsigned int i; int dispatch_result; (void)filep; if (buffer == 0 || count < STATUS_SIZE) return -22;
    /* The App-install loop is created asynchronously during AP startup. A
     * missing loop is retryable; preserve the already-owned request and
     * retry once per Lua status poll instead of returning a false failure. */
    if (queue_pending && request_busy) {
        dispatch_result = ensure_app_queue();
        if (dispatch_result == 0) {
            queue_result = ASYNC_QUEUE_SUBMIT(app_queue, &queued_request);
            queue_pending = 0u;
            if (queue_result != 0) finish_request(queue_result);
        } else if (dispatch_result < 0) {
            queue_pending = 0u;
            finish_request(dispatch_result);
        }
    }
    clear_status();
    *(unsigned int *)(void *)(status + 0x00) = MAGIC; *(unsigned int *)(void *)(status + 0x04) = STATUS_ABI;
    *(unsigned int *)(void *)(status + 0x14) = pending_command; *(unsigned int *)(void *)(status + 0x18) = pending_state;
    *(int *)(void *)(status + 0x20) = last_error; *(unsigned int *)(void *)(status + 0x24) = pending_stage;
    *(unsigned int *)(void *)(status + 0x28) = pending_stage; *(unsigned int *)(void *)(status + 0x2c) = BUILD_MARKER;
    *(unsigned int *)(void *)(status + 0x30) = driver_registered; *(unsigned int *)(void *)(status + 0x34) = APP_ID;
    *(unsigned int *)(void *)(status + 0x38) = app_registered; *(unsigned int *)(void *)(status + 0x3c) = launcher_published;
    *(unsigned int *)(void *)(status + 0x40) = loaded_notified;
    *(int *)(void *)(status + 0x44) = registration_result;
    *(int *)(void *)(status + 0x48) = launcher_result;
    *(int *)(void *)(status + 0x4c) = queue_result;
    *(unsigned int *)(void *)(status + 0x50) = app_queue_ready;
    *(unsigned int *)(void *)(status + 0x54) = queue_pending;
    *(unsigned int *)(void *)(status + 0x58) = app_queue_failed;
    *(unsigned int *)(void *)(status + 0x5c) = direct_context_used;
    for (i = 0; i < STATUS_SIZE; ++i) buffer[i] = (char)status[i]; return (int)STATUS_SIZE;
}
static void finish_request(int error) { last_error = error; pending_state = error ? RESULT_FAILED : RESULT_COMPLETED; request_busy = 0u; }
static int string_equal(const char *left, const char *right) {
    unsigned int index = 0;
    if (left == 0 || right == 0) return 0;
    do { if (left[index] != right[index]) return 0; } while (left[index++] != '\0');
    return 1;
}
static int app_is_registered(void) {
    const struct app_descriptor *registered = (const struct app_descriptor *)APP_LOOKUP(APP_ID);
    return registered != 0 && string_equal(registered->package_name, package_name);
}
/* LuaLVGL Timer callbacks run on miwear's AP/UI task.  NuttX VFS invokes a
 * character driver's write method synchronously in the calling task, so the
 * two firmware calls below execute in the same Native App context as Lua.
 * Do not use the Quick App watcher loop: it is unrelated to this App. */
static int execute_native_stage(unsigned int command, unsigned int stage) {
    int error = 0;
    if (command == CMD_INSTALL && stage == 1u) {
        if (!app_registered) {
            registration_result = APP_REGISTER(&app, pages, 1u);
            /* The return register is not a normalized ABI.  The registry
             * lookup is the only accepted success condition. */
            if (!app_is_registered()) error = -100; else app_registered = 1u;
        }
    } else if (command == CMD_INSTALL && stage == 2u) {
        if (!app_registered || !app_is_registered()) error = -100;
        else if (!launcher_published) { LAUNCHER_ADD(APP_ID); launcher_result = 0; launcher_published = 1u; }
    } else if (command == CMD_UNINSTALL) {
        /* Only page_unregister is known.  No App-registry removal ABI has
         * been recovered, so detaching this page would leave an App/Launcher
         * record with dangling state.  Refuse instead of lying about removal. */
        error = app_registered ? -95 : 0;
    } else error = -22;
    return error;
}
static void execute_on_app_loop(void *queue, void *payload) {
    struct queued_request *request = (struct queued_request *)payload;
    int result;
    (void)queue;
    result = execute_native_stage(request->command, request->stage);
    finish_request(result);
}
static int ensure_app_queue(void) {
    void *loop;
    if (app_queue_ready) return 0;
    if (app_queue_failed) return -19;
    loop = APP_INSTALL_LOOP;
    if (loop == 0) return 1;
    /* init(loop, queue_storage, callback): the first argument is the
     * firmware-owned App-install uv_loop_t. */
    queue_result = ASYNC_QUEUE_INIT(loop, app_queue, execute_on_app_loop);
    if (queue_result != 0) { app_queue_failed = 1u; return queue_result; }
    app_queue_ready = 1u;
    return 0;
}
static int execute_direct_request(void) {
    int result = execute_native_stage(queued_request.command, queued_request.stage);
    direct_context_used = 1u;
    queue_pending = 0u;
    finish_request(result);
    return result;
}
static int write_device(void *filep, const char *buffer, unsigned int count) {
    const unsigned int *words; (void)filep; if (buffer == 0 || count < 16u) return -22;
    words = (const unsigned int *)(const void *)buffer; if (words[0] != MAGIC) return -22; if (request_busy) return -16;
    pending_command = words[1]; pending_stage = words[2];
    /* These phases only update Supervisor state and must not wait for the
     * firmware App-install loop. Native registration begins at INSTALL 1. */
    if (words[1] == CMD_NOTIFY_LOADED) {
        loaded_notified = 1u; last_error = 0; pending_state = RESULT_COMPLETED; return 16;
    }
    if (words[1] == CMD_RESTORE_AFTER_BOOT) {
        last_error = 0; pending_state = RESULT_COMPLETED; return 16;
    }
    if (words[1] == CMD_INSTALL && words[2] == 0u) {
        last_error = 0; pending_state = RESULT_COMPLETED; return 16;
    }
    queued_request.command = words[1]; queued_request.stage = words[2];
    pending_state = RESULT_QUEUED; request_busy = 1u;
    queue_pending = 1u;
    /* This installer runs on AP task 49. On this O63 build the Quick App
     * watcher is absent, so its loop global remains NULL. The VFS write is
     * synchronous in that same AP task; execute there instead of waiting for
     * a loop that this firmware instance never creates. */
    if (APP_INSTALL_LOOP == 0) (void)execute_direct_request();
    return 16;
}
static struct file_operations fops = { .open = (void *)open_device, .close = (void *)close_device, .read = (void *)read_device, .write = (void *)write_device, .reserved = {0} };
int module_initialize(struct shellpp_ii_mod_info *modinfo) {
    int result; if (modinfo == 0) return -22; modinfo->uninitializer = 0; modinfo->arg = 0; modinfo->exports = 0; modinfo->nexports = 0;
    result = REGISTER_DRIVER(device_path, &fops, 0666u); if (result < 0) { last_error = result; pending_state = RESULT_FAILED; return result; }
    driver_registered = 1u; pending_state = RESULT_COMPLETED; return 0;
}
