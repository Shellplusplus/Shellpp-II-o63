#include "shellpp_ii_module.h"
#include "s441_firmware_abi.h"
#include "s441_native_ui.h"

typedef int (*async_queue_init_t)(void *, void *, void (*)(void *, void *));
typedef int (*async_queue_submit_t)(void *, void *);
#define REGISTER_DRIVER S441_REGISTER_DRIVER
#define APP_REGISTER S441_APP_REGISTER
#define LAUNCHER_ADD S441_LAUNCHER_ADD
#define APP_LOOKUP S441_APP_LOOKUP
#define ASYNC_QUEUE_INIT ((async_queue_init_t)0x0c31afd5u)
#define ASYNC_QUEUE_SUBMIT ((async_queue_submit_t)0x0c31b0cdu)
#define APP_INSTALL_LOOP S441_APP_INSTALL_LOOP
#define MAGIC 0x53505331u
#define STATUS_ABI 2u
#define BUILD_MARKER 0x53494937u
#define RESULT_QUEUED 2u
#define RESULT_COMPLETED 5u
#define RESULT_FAILED 15u
#define STATUS_SIZE 384u
#define CMD_INSTALL 0x53510002u
#define CMD_UNINSTALL 0x53510003u
#define CMD_NOTIFY_LOADED 0x53510004u
#define CMD_RESTORE_AFTER_BOOT 0x5351000au
#define APP_ID S441_APP_ID
#define NOTIFICATION_UID_LOW (((uint32_t)APP_ID << 16) | 1u)

struct file_operations { void *open; void *close; void *read; void *write; void *reserved[16]; };
static const char device_path[] = "/dev/shellpp";
static const char package_name[] = "com.shellpp.ii";
static const char notification_title[] = "Shell++ II";
static const char notification_body[] = "Native tools are ready";
static const char notification_icon[] =
    "/resource/app/notifications/default.bin";
static const char notification_small_icon[] =
    "/resource/app/notifications/default_small.bin";
/* Match the complete O63 system-notification descriptor shape. */
static struct s441_notification_descriptor loaded_notification = {
    .uid_low = NOTIFICATION_UID_LOW,
    .uid_high = 0u,
    /* O63 built-in templates leave +0x08 zero and store the user-visible
     * application/source name at +0x0C. Package identity is not read here. */
    .message_id = 0u,
    .string_0c = notification_title,
    .string_10 = notification_title,
    .string_14 = notification_body,
    .string_1c = notification_icon,
    .string_20 = notification_small_icon,
    .reserved_34 = 0x00999999u,
    .reserved_38 = 0u,
    .foreground_alert = 1u,
};
static volatile unsigned int pending_command, pending_stage, pending_state;
static volatile int last_error;
static volatile unsigned int driver_registered, request_busy, app_registered, launcher_published, loaded_notified;
static volatile unsigned int notification_requested;
static volatile int registration_result, launcher_result, queue_result;
static volatile int notification_result;
static volatile int toast_result;
static volatile unsigned int notification_dispatch_count;
static volatile unsigned int notification_message_address;
static volatile unsigned int notification_listener_enabled;
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
static int publish_loaded_notification(void);

static void *thumb_callback(const void *callback) {
    return (void *)(((uintptr_t)callback) | (uintptr_t)1u);
}

static int open_device(void *filep) { (void)filep; return 0; }
static int close_device(void *filep) { (void)filep; return 0; }
static void clear_status(void) { unsigned int i; for (i = 0; i < STATUS_SIZE; ++i) status[i] = 0; }
static int read_device(void *filep, char *buffer, unsigned int count) {
    const struct s441_ui_diagnostics *ui;
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
    *(int *)(void *)(status + 0x60) = notification_result;
    ui = s441_ui_diagnostics();
    if (ui) {
        *(unsigned int *)(void *)(status + 0x64) = ui->page_create_count;
        *(unsigned int *)(void *)(status + 0x68) = ui->click_count;
        *(unsigned int *)(void *)(status + 0x6c) = ui->last_action;
        *(unsigned int *)(void *)(status + 0x70) = ui->last_page;
        *(int *)(void *)(status + 0x74) = ui->last_result;
        *(unsigned int *)(void *)(status + 0x80) = ui->row_create_count;
        *(unsigned int *)(void *)(status + 0x84) =
            ui->row_create_failures;
    }
    *(unsigned int *)(void *)(status + 0x78) = notification_requested;
    *(unsigned int *)(void *)(status + 0x7c) = 0u;
    *(unsigned int *)(void *)(status + 0x88) =
        notification_dispatch_count;
    *(unsigned int *)(void *)(status + 0x8c) =
        notification_message_address;
    *(unsigned int *)(void *)(status + 0xa0) =
        notification_listener_enabled;
    *(int *)(void *)(status + 0xa4) = toast_result;
    if (ui) {
        *(unsigned int *)(void *)(status + 0x90) = ui->last_root;
        *(unsigned int *)(void *)(status + 0x94) = ui->last_title;
        *(unsigned int *)(void *)(status + 0x98) = ui->last_content;
        *(unsigned int *)(void *)(status + 0x9c) = ui->last_row;
        *(unsigned int *)(void *)(status + 0xa8) = ui->toast_create_count;
        *(unsigned int *)(void *)(status + 0xac) =
            ui->toast_create_failures;
        *(unsigned int *)(void *)(status + 0xb0) = ui->last_toast_root;
    }
    for (i = 0; i < STATUS_SIZE; ++i) buffer[i] = (char)status[i]; return (int)STATUS_SIZE;
}
static void finish_request(int error) { last_error = error; pending_state = error ? RESULT_FAILED : RESULT_COMPLETED; request_busy = 0u; }
static int string_equal(const char *left, const char *right) {
    unsigned int index = 0;
    if (left == 0 || right == 0) return 0;
    do { if (left[index] != right[index]) return 0; } while (left[index++] != '\0');
    return 1;
}
static int app_registration_state(void) {
    const struct s441_app_descriptor *registered =
        (const struct s441_app_descriptor *)APP_LOOKUP(APP_ID);
    if (registered == 0) return 0;
    return string_equal(registered->package_name, package_name) ? 1 : -1;
}
static int publish_loaded_notification(void) {
    void *message;
    if (!notification_requested || loaded_notified) return 0;
    loaded_notification.foreground_alert = 1u;
    loaded_notification.manager_state_51 = 0u;
    loaded_notification.manager_state_52 = 0u;
    s441_ui_prepare_loaded_notification(&loaded_notification);
    /* The notification app normally enables this listener during its own
     * page lifecycle. The installer can run before that page was ever opened,
     * so enable the idempotent O63 reminder consumer before publishing the
     * foreground event generated by notification_insert(). */
    S441_NOTIFICATION_REMINDER_LISTENER(1u);
    notification_listener_enabled = 1u;
    message = S441_NOTIFICATION_INSERT(&loaded_notification);
    notification_result = message ? 1 : -102;
    if (!message) {
        return -102;
    }
    notification_message_address = (unsigned int)(uintptr_t)message;
    ++notification_dispatch_count;
    toast_result = s441_ui_show_loaded_toast();
    if (toast_result != 0) {
        notification_result = -103;
        return -103;
    }
    notification_result = 1;
    loaded_notified = 1u;
    return 0;
}
/* LuaLVGL Timer callbacks run on miwear's AP/UI task.  NuttX VFS invokes a
 * character driver's write method synchronously in the calling task, so the
 * two firmware calls below execute in the same Native App context as Lua.
 * Do not use the Quick App watcher loop: it is unrelated to this App. */
static int execute_native_stage(unsigned int command, unsigned int stage) {
    int error = 0;
    if (command == CMD_NOTIFY_LOADED) {
        /* Lua intentionally sends NOTIFY before INSTALL 0/1/2. Preserve that
         * public sequence while waiting for the App and Launcher records. */
        notification_requested = 1u;
        /* A fresh installer Run is an explicit request for another foreground
         * reminder. Do not let a previous resident-module success suppress it. */
        loaded_notified = 0u;
        notification_result = 0;
        toast_result = 0;
        notification_message_address = 0u;
        if (launcher_published && !loaded_notified)
            error = publish_loaded_notification();
    } else if (command == CMD_INSTALL && stage == 1u) {
        if (!app_registered) {
            registration_result = APP_REGISTER(s441_ui_app_descriptor(),
                s441_ui_pages(), s441_ui_page_count());
            /* The return register is not a normalized ABI.  The registry
             * lookup is the only accepted success condition. */
            int state = app_registration_state();
            if (state == 0) error = -100;
            else if (state < 0) error = -101;
            else app_registered = 1u;
        }
    } else if (command == CMD_INSTALL && stage == 2u) {
        int state = app_registration_state();
        if (!app_registered || state == 0) error = -100;
        else if (state < 0) error = -101;
        else {
            if (!launcher_published) {
                LAUNCHER_ADD(APP_ID);
                launcher_result = 0;
                launcher_published = 1u;
            }
            /* Keep Launcher publication and notification insertion in the
             * same proven App/UI execution transaction. A NULL notification
             * result makes INSTALL 2 fail instead of reporting false success. */
            notification_requested = 1u;
            if (!loaded_notified) error = publish_loaded_notification();
        }
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
    queue_result = ASYNC_QUEUE_INIT(loop, app_queue,
        (void (*)(void *, void *))thumb_callback(
            (const void *)execute_on_app_loop));
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
    /* Restore and INSTALL 0 only update Supervisor state. Native/App UI
     * operations, including the loaded notification, use the common dispatch
     * path below. */
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
    if (APP_INSTALL_LOOP == 0) {
        /* LuaLVGL invokes this VFS write synchronously on the AP UI task on
         * O63. Keep the proven fallback for builds where the optional install
         * loop global is absent; when present, all work is submitted to it. */
        (void)execute_direct_request();
    }
    return 16;
}
static struct file_operations fops;
int module_initialize(struct shellpp_ii_mod_info *modinfo) {
    int result; if (modinfo == 0) return -22; modinfo->uninitializer = 0; modinfo->arg = 0; modinfo->exports = 0; modinfo->nexports = 0;
    fops.open = thumb_callback((const void *)open_device);
    fops.close = thumb_callback((const void *)close_device);
    fops.read = thumb_callback((const void *)read_device);
    fops.write = thumb_callback((const void *)write_device);
    result = REGISTER_DRIVER(device_path, &fops, 0666u); if (result < 0) { last_error = result; pending_state = RESULT_FAILED; return result; }
    driver_registered = 1u; pending_state = RESULT_COMPLETED; return 0;
}
