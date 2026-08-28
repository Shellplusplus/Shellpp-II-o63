#include "s441_native_ui.h"

#include "s441_native_fs.h"

#define S441_PAGE_HOME 0u
#define S441_PAGE_FILES 1u
#define S441_PAGE_CPU 2u
#define S441_PAGE_MEMORY 3u
#define S441_PAGE_RESTART 4u
#define S441_PAGE_ABOUT 5u
#define S441_PAGE_COUNT 6u

#define S441_ALIGN_TOP_MID 2u
#define S441_ALIGN_CENTER 9u
/* O63 native page builders pass 14 when stacking content and native rows. */
#define S441_ALIGN_OUT_BOTTOM_MID 14u
#define S441_TEXT_ALIGN_LEFT 0u
#define S441_TEXT_ALIGN_CENTER 1u
#define S441_STYLE_SELECTOR_DEFAULT 0u
#define S441_EVENT_CLICKED 7u
#define S441_OBJECT_FLAG_HIDDEN 0x01u
#define S441_OBJECT_FLAG_SCROLLABLE 0x10u
#define S441_PAGE_TITLE_MODE_HOME 0u
#define S441_PAGE_TITLE_MODE_STANDARD 1u
#define S441_ACTION_COUNT S441_FS_DIR_PAGE_ENTRIES
#define S441_CONTENT_WIDTH 380
#define S441_CONTENT_HEIGHT 260
#define S441_CONTENT_TOP_GAP 8
#define S441_FIRST_ROW_GAP 0
#define S441_NEXT_ROW_GAP 8
#define S441_NATIVE_ROW_TYPE_MENU 3u
#define S441_ACTION_WIDTH 360
#define S441_ACTION_HEIGHT 92
#define S441_INFO_LABEL_WIDTH 320
#define S441_INFO_LABEL_HEIGHT 132
#define S441_INFO_LABEL_TOP 0
#define S441_INFO_ROW_GAP 8
#define S441_FILE_NAME_VISIBLE_BYTES 15u
#define S441_FILE_TEXT_MAX_LINES 3u
/* O63's built-in explanatory page passes the packed RGB value 0x00666666. */
#define S441_TEXT_COLOR 0x00ffffffu

#define S441_ACTION_HOME_FILES 1u
#define S441_ACTION_HOME_MONITOR 2u
#define S441_ACTION_HOME_RESTART 3u
#define S441_ACTION_HOME_ABOUT 4u
#define S441_ACTION_FILE_PRIMARY 10u
#define S441_ACTION_FILE_SECONDARY 11u
#define S441_ACTION_FILE_PAGE 12u
#define S441_ACTION_MONITOR_REFRESH 20u
#define S441_ACTION_MEMORY_REFRESH 21u
#define S441_ACTION_RESTART_CONFIRM 30u
#define S441_ACTION_NOTIFICATION_OPEN 40u

struct s441_action_view {
    void *row;
    s441_lvx_event_callback_t callback;
};

enum s441_file_mode {
    S441_FILE_MODE_DIRECTORY = 0,
    S441_FILE_MODE_TEXT = 1,
};

struct s441_page_view {
    void *root;
    void *title_bar;
    void *content_root;
    struct s441_action_view actions[S441_ACTION_COUNT];
    void *body_panel;
    void *body;
};

static const char package_name[] = "com.shellpp.ii";
static const char app_path[] = "/data/shellpp-ii/shellpp_ii_icon.bin";
static const char display_name[] = "Shell++ II";
static const char home_page_name[] = "shellpp-home";
static const char files_page_name[] = "shellpp-files";
static const char cpu_page_name[] = "shellpp-cpu";
static const char memory_page_name[] = "shellpp-memory";
static const char restart_page_name[] = "shellpp-restart";
static const char about_page_name[] = "shellpp-about";
static const char about_text[] =
    "Shell++ II\nWatch S4 41mm\nVela 3.100.028";
static const char file_read_error[] = "Unable to read this path.";
static const char directory_read_error[] = "Unable to open this directory.";
static const char directory_empty[] = "Directory is empty.";
static const char reboot_tag[] = "shellpp_ii";
static const char empty_text[] = "";
static struct s441_page_view page_views[S441_PAGE_COUNT];
static struct s441_page_view notification_view;
static struct s441_action_view loaded_toast_action;
static void *loaded_toast_root;
static struct s441_fs_directory_page directory_page;
static char current_path[S441_FS_PATH_CAP] = "/";
static char selected_path[S441_FS_PATH_CAP];
static char path_buffer[S441_FS_PATH_CAP];
static char file_text[96];
/* LVX labels retain their text pointers, so file-page render strings must
 * remain valid after the event callback returns. */
static char file_row_primary[S441_FS_DIR_PAGE_ENTRIES][S441_FS_NAME_CAP];
static char cpu_text[40];
static char memory_text[80];
static char cpu_row_text[48];
static char memory_row_text[48];
static uint32_t text_offset;
static uint32_t text_length;
static uint8_t text_has_next;
static uint8_t file_mode;
static const char *file_notice;
static uint8_t reboot_armed;
static uint8_t callbacks_initialized;
static struct s441_ui_diagnostics ui_diagnostics;

static void home_create(struct s441_page_descriptor *page, void *root,
    void *start_data);
static void files_create(struct s441_page_descriptor *page, void *root,
    void *start_data);
static void cpu_create(struct s441_page_descriptor *page, void *root,
    void *start_data);
static void memory_create(struct s441_page_descriptor *page, void *root,
    void *start_data);
static void restart_create(struct s441_page_descriptor *page, void *root,
    void *start_data);
static void about_create(struct s441_page_descriptor *page, void *root,
    void *start_data);
static void page_cleanup(struct s441_page_descriptor *page);
static void render_files(void);
static void copy_text(char *target, uint32_t capacity, const char *source);
static void format_metric(char *target, uint32_t capacity,
    const char *label, const char *value);
static void copy_compact_name(char *target, uint32_t capacity,
    const char *source);
static void prepare_file_rows(void);
static void ensure_file_actions(uint32_t count);
static void files_title_back_clicked(void *event);
static void notification_open(struct s441_notification_descriptor *message,
    void *root);
static void notification_close(struct s441_notification_descriptor *message,
    void *context);
static void loaded_toast_builder(void *toast_root, uint32_t destroying);
static const s441_lvx_event_callback_t file_callbacks[S441_ACTION_COUNT];

/* NuttX modlib resolves R_ARM_ABS32 references to local functions without
 * preserving the Thumb state bit on this image. Firmware-owned descriptors
 * and LVX event records later BLX these values, so set bit 0 after relocation
 * instead of relying on the ELF symbol's st_value. */
static uintptr_t thumb_callback_address(const void *callback) {
    return ((uintptr_t)callback) | (uintptr_t)1u;
}

static void record_action(uint32_t action, int32_t result) {
    ++ui_diagnostics.click_count;
    ui_diagnostics.last_action = action;
    ui_diagnostics.last_result = result;
}

static void record_page_create(uint32_t page_id) {
    ++ui_diagnostics.page_create_count;
    ui_diagnostics.last_page = page_id;
    ui_diagnostics.last_result = 0;
}

static struct s441_page_descriptor home_page = {
    .prototype = S441_PAGE_PROTOTYPE,
    .page_name = home_page_name,
    .page_id = S441_PAGE_HOME,
    .app_id = S441_APP_ID,
    /* Match the O63 built-in descriptor template. Registration supplies
     * zero defaults without overwriting the built-in state marker. */
    .state_28 = 0u,
    .state_29 = 5u,
    .type_2a = 0u,
    .activity_api = S441_PAGE_ACTIVITY_API,
};
static struct s441_page_descriptor files_page = {
    .prototype = S441_PAGE_PROTOTYPE,
    .page_name = files_page_name,
    .page_id = S441_PAGE_FILES,
    .app_id = S441_APP_ID,
    .state_28 = 0u,
    .state_29 = 5u,
    .type_2a = 0u,
    .activity_api = S441_PAGE_ACTIVITY_API,
};
static struct s441_page_descriptor cpu_page = {
    .prototype = S441_PAGE_PROTOTYPE,
    .page_name = cpu_page_name,
    .page_id = S441_PAGE_CPU,
    .app_id = S441_APP_ID,
    .state_28 = 0u,
    .state_29 = 5u,
    .type_2a = 0u,
    .activity_api = S441_PAGE_ACTIVITY_API,
};
static struct s441_page_descriptor memory_page = {
    .prototype = S441_PAGE_PROTOTYPE,
    .page_name = memory_page_name,
    .page_id = S441_PAGE_MEMORY,
    .app_id = S441_APP_ID,
    .state_28 = 0u,
    .state_29 = 5u,
    .type_2a = 0u,
    .activity_api = S441_PAGE_ACTIVITY_API,
};
static struct s441_page_descriptor restart_page = {
    .prototype = S441_PAGE_PROTOTYPE,
    .page_name = restart_page_name,
    .page_id = S441_PAGE_RESTART,
    .app_id = S441_APP_ID,
    .state_28 = 0u,
    .state_29 = 5u,
    .type_2a = 0u,
    .activity_api = S441_PAGE_ACTIVITY_API,
};
static struct s441_page_descriptor about_page = {
    .prototype = S441_PAGE_PROTOTYPE,
    .page_name = about_page_name,
    .page_id = S441_PAGE_ABOUT,
    .app_id = S441_APP_ID,
    .state_28 = 0u,
    .state_29 = 5u,
    .type_2a = 0u,
    .activity_api = S441_PAGE_ACTIVITY_API,
};
static void *const page_table[S441_PAGE_COUNT] = {
    &home_page,
    &files_page,
    &cpu_page,
    &memory_page,
    &restart_page,
    &about_page,
};

static const char *resolve_display_name(struct s441_app_descriptor *app) {
    (void)app;
    return display_name;
}

static struct s441_app_descriptor app_descriptor = {
    .package_name = package_name,
    .app_path = app_path,
    .app_id = S441_APP_ID,
};

static void initialize_firmware_callbacks(void) {
    uintptr_t cleanup;
    if (callbacks_initialized) return;
    cleanup = thumb_callback_address((const void *)page_cleanup);
    app_descriptor.resolve_display_name =
        (const char *(*)(struct s441_app_descriptor *))
        thumb_callback_address((const void *)resolve_display_name);
    home_page.create = (s441_page_create_t)
        thumb_callback_address((const void *)home_create);
    files_page.create = (s441_page_create_t)
        thumb_callback_address((const void *)files_create);
    cpu_page.create = (s441_page_create_t)
        thumb_callback_address((const void *)cpu_create);
    memory_page.create = (s441_page_create_t)
        thumb_callback_address((const void *)memory_create);
    restart_page.create = (s441_page_create_t)
        thumb_callback_address((const void *)restart_create);
    about_page.create = (s441_page_create_t)
        thumb_callback_address((const void *)about_create);
    home_page.cleanup = (s441_page_cleanup_t)cleanup;
    files_page.cleanup = (s441_page_cleanup_t)cleanup;
    cpu_page.cleanup = (s441_page_cleanup_t)cleanup;
    memory_page.cleanup = (s441_page_cleanup_t)cleanup;
    restart_page.cleanup = (s441_page_cleanup_t)cleanup;
    about_page.cleanup = (s441_page_cleanup_t)cleanup;
    callbacks_initialized = 1u;
}

static void show_page(uint32_t page_id) {
    ui_diagnostics.last_page = page_id;
    S441_ACTIVITY_ACTIVATE(((uint32_t)S441_APP_ID << 16) | page_id, 0u);
}

static void title_back_clicked(void *event) {
    (void)event;
    /* The recovered close ABI explicitly treats ID 0 as the top page. */
    S441_ACTIVITY_CLOSE(0u);
}

static int begin_page(struct s441_page_view *view, void *root,
        const char *title, uint32_t title_mode,
        s441_lvx_event_callback_t back_callback) {
    const void *callback = back_callback ? (const void *)
        thumb_callback_address((const void *)back_callback) : 0;
    if (!root || view->root == root) return 0;
    view->root = root;
    /* O63 page builders attach the title directly to the Activity root. */
    view->title_bar = S441_LVX_PAGE_TITLE_CREATE(root, title,
        title_mode, callback, 0);
    if (!view->title_bar) {
        view->root = 0;
        return 0;
    }
    /* The 466x466 round display needs a stable centered owner. A
     * size-to-content owner lets a type-3 row move the parent's geometry and
     * shifts the whole action to the right on the notification detail page. */
    view->content_root = S441_LVX_PAGE_CONTENT_CREATE(root,
        S441_CONTENT_WIDTH, S441_CONTENT_HEIGHT);
    if (!view->content_root) {
        view->root = 0;
        view->title_bar = 0;
        return 0;
    }
    S441_LVX_OBJECT_ALIGN_TO(view->content_root, view->title_bar,
        S441_ALIGN_OUT_BOTTOM_MID, 0, S441_CONTENT_TOP_GAP);
    /* page_content_create clears this flag. Every application and
     * notification page uses this common owner, so restoring it here makes
     * every page scrollable without changing any visual styling. */
    S441_LVX_OBJECT_ADD_FLAG(view->content_root,
        S441_OBJECT_FLAG_SCROLLABLE);
    ui_diagnostics.last_root = (uint32_t)(uintptr_t)root;
    ui_diagnostics.last_title = (uint32_t)(uintptr_t)view->title_bar;
    ui_diagnostics.last_content = (uint32_t)(uintptr_t)view->content_root;
    return 1;
}

static struct s441_page_view *view_for_page(
        struct s441_page_descriptor *page) {
    if (page == &home_page) return &page_views[S441_PAGE_HOME];
    if (page == &files_page) return &page_views[S441_PAGE_FILES];
    if (page == &cpu_page) return &page_views[S441_PAGE_CPU];
    if (page == &memory_page) return &page_views[S441_PAGE_MEMORY];
    if (page == &restart_page) return &page_views[S441_PAGE_RESTART];
    if (page == &about_page) return &page_views[S441_PAGE_ABOUT];
    return 0;
}

static void page_cleanup(struct s441_page_descriptor *page) {
    struct s441_page_view *view = view_for_page(page);
    uint32_t index;
    if (!view) return;
    view->root = 0;
    view->title_bar = 0;
    view->content_root = 0;
    view->body_panel = 0;
    view->body = 0;
    for (index = 0u; index < S441_ACTION_COUNT; ++index) {
        view->actions[index].row = 0;
        view->actions[index].callback = 0;
    }
}

static void style_label(void *label, int32_t x, int32_t y, int32_t width,
        int32_t height, uint32_t text_alignment, uint32_t color) {
    S441_LVX_OBJECT_SET_SIZE(label, width, height);
    S441_LVX_OBJECT_SET_TEXT_COLOR(label, color,
        S441_STYLE_SELECTOR_DEFAULT);
    S441_LVX_OBJECT_SET_TEXT_ALIGN(label, text_alignment,
        S441_STYLE_SELECTOR_DEFAULT);
    S441_LVX_OBJECT_ALIGN(label, S441_ALIGN_TOP_MID, x, y);
}

static void *make_label(void *root, const char *text, int32_t x, int32_t y,
        int32_t width, int32_t height, uint32_t text_alignment) {
    void *label = S441_LVX_LABEL_CREATE(root);
    if (!label) return 0;
    S441_LVX_LABEL_SET_TEXT(label, text);
    /* label_set_text may recalculate the object geometry. Apply the fixed
     * round-screen viewport after every initial text assignment. */
    style_label(label, x, y, width, height, text_alignment, S441_TEXT_COLOR);
    return label;
}

static void set_info_text(void *label, const char *text,
        uint32_t text_alignment) {
    if (!label) return;
    S441_LVX_LABEL_SET_TEXT(label, text ? text : empty_text);
    /* Dynamic text updates can trigger the same LVGL size recalculation as
     * initial creation, so restore all viewport constraints afterwards. */
    style_label(label, 0, S441_INFO_LABEL_TOP, S441_INFO_LABEL_WIDTH,
        S441_INFO_LABEL_HEIGHT, text_alignment, S441_TEXT_COLOR);
}

static void *make_info_panel(struct s441_page_view *view, const char *text,
        uint32_t text_alignment) {
    if (!view || !view->content_root) return 0;
    /* A second styled page-content object inside the first one caused two
     * independent layout passes and visibly compressed the text viewport on
     * the round display. The firmware's own pages use a plain label for this
     * kind of body copy, so keep a single content owner. */
    view->body = make_label(view->content_root, text, 0,
        S441_INFO_LABEL_TOP,
        S441_INFO_LABEL_WIDTH, S441_INFO_LABEL_HEIGHT, text_alignment);
    view->body_panel = view->body;
    if (view->body_panel)
        S441_LVX_OBJECT_ADD_FLAG(view->body_panel,
            S441_OBJECT_FLAG_HIDDEN);
    return view->body;
}

static void make_action(void *root, struct s441_action_view *slot,
        const char *primary, void *base, int32_t gap,
        s441_lvx_event_callback_t callback) {
    s441_lvx_event_callback_t thumb_callback;
    if (!root || !slot || !callback) return;
    thumb_callback = (s441_lvx_event_callback_t)
        thumb_callback_address((const void *)callback);
    slot->callback = callback;
    slot->row = S441_LVX_ROW_CREATE(root);
    if (!slot->row) {
        ++ui_diagnostics.row_create_failures;
        return;
    }
    ++ui_diagnostics.row_create_count;
    ui_diagnostics.last_row = (uint32_t)(uintptr_t)slot->row;
    /* The complete O63 type-3 call site at 0x2C4962C2 passes NULL in R1.
     * R1 is an already-created icon/source object, not a resource path.
     * Passing a path string here consumed horizontal space through the wrong
     * ABI and was the main cause of compressed labels. */
    S441_LVX_ROW_INIT(slot->row, 0,
        primary ? primary : empty_text, 0, 0u, 0u,
        S441_NATIVE_ROW_TYPE_MENU);
    S441_LVX_OBJECT_SET_SIZE(slot->row, S441_ACTION_WIDTH,
        S441_ACTION_HEIGHT);
    S441_LVX_OBJECT_CLEAR_FLAG(slot->row, S441_OBJECT_FLAG_SCROLLABLE);
    if (base) S441_LVX_OBJECT_ALIGN_TO(slot->row, base,
        S441_ALIGN_OUT_BOTTOM_MID, 0, gap);
    else S441_LVX_OBJECT_ALIGN(slot->row, S441_ALIGN_TOP_MID, 0, gap);
    S441_LVX_EVENT_ADD(slot->row, thumb_callback, S441_EVENT_CLICKED, slot);
}

static void position_action(struct s441_action_view *action, void *base,
        int32_t gap) {
    if (!action || !action->row || !base) return;
    S441_LVX_OBJECT_ALIGN_TO(action->row, base,
        S441_ALIGN_OUT_BOTTOM_MID, 0, gap);
}

static void position_action_at_top(struct s441_page_view *view,
        struct s441_action_view *action) {
    if (!view || !view->content_root || !action || !action->row) return;
    S441_LVX_OBJECT_ALIGN(action->row, S441_ALIGN_TOP_MID, 0,
        S441_FIRST_ROW_GAP);
}

static void update_action(struct s441_action_view *action, const char *primary,
        uint8_t visible) {
    if (!action || !action->row) return;
    if (!visible) {
        S441_LVX_OBJECT_ADD_FLAG(action->row, S441_OBJECT_FLAG_HIDDEN);
        return;
    }
    /* release_icon=0 retains the icon resource acquired by row_init. */
    S441_LVX_ROW_UPDATE(action->row, 0u,
        primary ? primary : empty_text, 0, 0u, 0u);
    S441_LVX_OBJECT_SET_SIZE(action->row, S441_ACTION_WIDTH,
        S441_ACTION_HEIGHT);
    S441_LVX_OBJECT_CLEAR_FLAG(action->row, S441_OBJECT_FLAG_HIDDEN);
}

static void home_files_clicked(void *event) {
    (void)event;
    record_action(S441_ACTION_HOME_FILES, 0);
    show_page(S441_PAGE_FILES);
}

static void home_status_clicked(void *event) {
    (void)event;
    record_action(S441_ACTION_HOME_MONITOR, 0);
    show_page(S441_PAGE_CPU);
}

static void home_memory_clicked(void *event) {
    (void)event;
    record_action(S441_ACTION_HOME_MONITOR, 0);
    show_page(S441_PAGE_MEMORY);
}

static void home_restart_clicked(void *event) {
    (void)event;
    record_action(S441_ACTION_HOME_RESTART, 0);
    reboot_armed = 0u;
    show_page(S441_PAGE_RESTART);
}

static void home_about_clicked(void *event) {
    (void)event;
    record_action(S441_ACTION_HOME_ABOUT, 0);
    show_page(S441_PAGE_ABOUT);
}

static void notification_launch_clicked(void *event) {
    (void)event;
    record_action(S441_ACTION_NOTIFICATION_OPEN, 0);
    show_page(S441_PAGE_HOME);
}

static void copy_text(char *target, uint32_t capacity, const char *source) {
    uint32_t index = 0u;
    if (!target || !capacity) return;
    if (source) while (source[index] && index + 1u < capacity) {
        target[index] = source[index];
        ++index;
    }
    target[index] = '\0';
}

static void format_metric(char *target, uint32_t capacity,
        const char *label, const char *value) {
    uint32_t index = 0u;
    if (!target || !capacity) return;
    while (label && *label && index + 1u < capacity)
        target[index++] = *label++;
    if (index + 1u < capacity) target[index++] = ' ';
    while (value && *value && index + 1u < capacity)
        target[index++] = *value++;
    target[index] = '\0';
}

static uint32_t utf8_character_bytes(uint8_t first) {
    if ((first & 0x80u) == 0u) return 1u;
    if ((first & 0xe0u) == 0xc0u) return 2u;
    if ((first & 0xf0u) == 0xe0u) return 3u;
    if ((first & 0xf8u) == 0xf0u) return 4u;
    return 1u;
}

static void copy_compact_name(char *target, uint32_t capacity,
        const char *source) {
    uint32_t source_length = 0u;
    uint32_t output = 0u;
    uint32_t limit;
    if (!target || capacity < 4u) return;
    if (!source) {
        target[0] = '\0';
        return;
    }
    while (source[source_length]) ++source_length;
    limit = capacity - 1u;
    if (limit > S441_FILE_NAME_VISIBLE_BYTES)
        limit = S441_FILE_NAME_VISIBLE_BYTES;
    if (source_length <= limit) {
        copy_text(target, capacity, source);
        return;
    }
    /* Keep the row to one line without splitting a UTF-8 codepoint. */
    limit -= 3u;
    while (source[output] && output < limit) {
        uint32_t bytes = utf8_character_bytes((uint8_t)source[output]);
        uint32_t index;
        if (output + bytes > limit) break;
        for (index = 1u; index < bytes; ++index) {
            if ((((uint8_t)source[output + index]) & 0xc0u) != 0x80u) {
                bytes = 1u;
                break;
            }
        }
        for (index = 0u; index < bytes; ++index)
            target[output + index] = source[output + index];
        output += bytes;
    }
    target[output++] = '.';
    target[output++] = '.';
    target[output++] = '.';
    target[output] = '\0';
}

static void format_percent(char *target, uint32_t capacity, uint32_t value) {
    uint32_t index = 0u;
    if (!target || capacity < 3u) return;
    if (value > 100u) value = 100u;
    if (value >= 100u) target[index++] = '1';
    if (value >= 10u) target[index++] = (char)('0' + value / 10u % 10u);
    target[index++] = (char)('0' + value % 10u);
    if (index + 1u < capacity) target[index++] = '%';
    target[index] = '\0';
}

static void set_file_error(const char *message) {
    file_mode = S441_FILE_MODE_DIRECTORY;
    file_notice = message;
}

static int load_directory(const char *path, uint32_t start) {
    struct s441_fs_directory_page page;
    int result = s441_fs_list_directory_page(path, start, &page);
    if (result != S441_FS_OK) return result;
    copy_text(current_path, sizeof(current_path), path);
    directory_page = page;
    file_mode = S441_FILE_MODE_DIRECTORY;
    text_offset = 0u;
    text_length = 0u;
    text_has_next = 0u;
    file_notice = 0;
    prepare_file_rows();
    return S441_FS_OK;
}

static void prepare_file_rows(void) {
    uint32_t index;
    for (index = 0u; index < S441_FS_DIR_PAGE_ENTRIES; ++index) {
        if (index < directory_page.count) {
            copy_compact_name(file_row_primary[index],
                sizeof(file_row_primary[index]),
                directory_page.entries[index].name);
        } else {
            copy_text(file_row_primary[index], sizeof(file_row_primary[index]),
                "No item");
        }
    }
}

/* Keep arbitrary files safe for the text label. This mirrors the original
 * viewer's read-only behavior: visible whitespace is retained while control
 * bytes cannot alter the page layout. */
static void sanitize_file_text(char *text, uint32_t length) {
    uint32_t index;
    uint32_t lines = 1u;
    for (index = 0u; index < length; ++index) {
        uint8_t value = (uint8_t)text[index];
        if (value == '\n' || value == '\r') {
            if (value == '\n' && lines < S441_FILE_TEXT_MAX_LINES) {
                ++lines;
            } else {
                text[index] = ' ';
            }
            continue;
        }
        if (value < 0x20u && value != '\t' && value != '\n' &&
                value != '\r') text[index] = '.';
    }
}

static int load_text_page(uint32_t offset) {
    int result = s441_fs_read_text_page(selected_path, offset, file_text,
        sizeof(file_text), &text_length, &text_has_next);
    if (result != S441_FS_OK) return result;
    sanitize_file_text(file_text, text_length);
    text_offset = offset;
    file_mode = S441_FILE_MODE_TEXT;
    file_notice = 0;
    return S441_FS_OK;
}

static void ensure_file_actions(uint32_t count) {
    struct s441_page_view *view = &page_views[S441_PAGE_FILES];
    uint32_t index;
    if (!view->content_root) return;
    if (!count) count = 1u;
    if (count > S441_ACTION_COUNT) count = S441_ACTION_COUNT;
    for (index = 0u; index < count; ++index) {
        if (view->actions[index].row) continue;
        make_action(view->content_root, &view->actions[index],
            "Loading...", index ? view->actions[index - 1u].row : 0,
            index ? S441_NEXT_ROW_GAP : S441_FIRST_ROW_GAP,
            file_callbacks[index]);
    }
}

static void render_files(void) {
    struct s441_page_view *view = &page_views[S441_PAGE_FILES];
    uint32_t index;
    if (!view->root) return;
    if (file_mode == S441_FILE_MODE_TEXT) {
        ensure_file_actions(1u);
        S441_LVX_OBJECT_CLEAR_FLAG(view->body_panel, S441_OBJECT_FLAG_HIDDEN);
        set_info_text(view->body, file_text, S441_TEXT_ALIGN_LEFT);
        update_action(&view->actions[0],
            text_has_next ? "Next page" : "First page",
            text_has_next || text_offset);
        position_action(&view->actions[0], view->body_panel,
            S441_INFO_ROW_GAP);
        for (index = 1u; index < S441_ACTION_COUNT; ++index)
            update_action(&view->actions[index], empty_text, 0u);
        return;
    }
    S441_LVX_OBJECT_ADD_FLAG(view->body_panel, S441_OBJECT_FLAG_HIDDEN);
    prepare_file_rows();
    ensure_file_actions(directory_page.count);
    for (index = 0u; index < S441_ACTION_COUNT; ++index) {
        uint8_t visible = index < directory_page.count;
        if (!directory_page.count && index == 0u) visible = 1u;
        update_action(&view->actions[index], visible ?
            (directory_page.count ? file_row_primary[index] :
                (file_notice ? file_notice : directory_empty)) : empty_text,
            visible);
        if (index == 0u) position_action_at_top(view, &view->actions[index]);
        else position_action(&view->actions[index],
            view->actions[index - 1u].row, S441_NEXT_ROW_GAP);
    }
}

static void open_entry(uint32_t index, uint32_t action) {
    const struct s441_fs_entry *entry;
    int result;
    if (index >= directory_page.count) {
        record_action(action, S441_FS_ERR_ARGUMENT);
        return;
    }
    entry = &directory_page.entries[index];
    result = s441_fs_join(current_path, entry->name, path_buffer,
        sizeof(path_buffer));
    if (result != S441_FS_OK) {
        set_file_error(file_read_error);
    } else if (entry->is_directory) {
        result = load_directory(path_buffer, 0u);
        if (result != S441_FS_OK) set_file_error(directory_read_error);
    } else {
        copy_text(selected_path, sizeof(selected_path), path_buffer);
        result = load_text_page(0u);
        if (result != S441_FS_OK) set_file_error(file_read_error);
    }
    record_action(action, result);
    render_files();
}

static void file_action_index(uint32_t index, void *event) {
    int result = S441_FS_OK;
    (void)event;
    if (file_mode == S441_FILE_MODE_DIRECTORY) {
        open_entry(index, S441_ACTION_FILE_PRIMARY + index);
        return;
    }
    else if (text_has_next) {
        result = load_text_page(text_offset + text_length);
        if (result != S441_FS_OK)
            set_file_error(file_read_error);
        render_files();
    } else if (text_offset) {
        result = load_text_page(0u);
        if (result != S441_FS_OK) set_file_error(file_read_error);
        render_files();
    }
    record_action(S441_ACTION_FILE_PRIMARY, result);
}

#define FILE_ACTION_CALLBACK(n) \
    static void files_action_##n(void *event) { file_action_index(n, event); }
FILE_ACTION_CALLBACK(0) FILE_ACTION_CALLBACK(1)
FILE_ACTION_CALLBACK(2) FILE_ACTION_CALLBACK(3)
FILE_ACTION_CALLBACK(4) FILE_ACTION_CALLBACK(5)
FILE_ACTION_CALLBACK(6) FILE_ACTION_CALLBACK(7)
FILE_ACTION_CALLBACK(8) FILE_ACTION_CALLBACK(9)
FILE_ACTION_CALLBACK(10) FILE_ACTION_CALLBACK(11)
FILE_ACTION_CALLBACK(12) FILE_ACTION_CALLBACK(13)
FILE_ACTION_CALLBACK(14) FILE_ACTION_CALLBACK(15)
FILE_ACTION_CALLBACK(16) FILE_ACTION_CALLBACK(17)
FILE_ACTION_CALLBACK(18) FILE_ACTION_CALLBACK(19)
FILE_ACTION_CALLBACK(20) FILE_ACTION_CALLBACK(21)
FILE_ACTION_CALLBACK(22) FILE_ACTION_CALLBACK(23)
FILE_ACTION_CALLBACK(24) FILE_ACTION_CALLBACK(25)
FILE_ACTION_CALLBACK(26) FILE_ACTION_CALLBACK(27)
FILE_ACTION_CALLBACK(28) FILE_ACTION_CALLBACK(29)
FILE_ACTION_CALLBACK(30) FILE_ACTION_CALLBACK(31)
FILE_ACTION_CALLBACK(32) FILE_ACTION_CALLBACK(33)
FILE_ACTION_CALLBACK(34) FILE_ACTION_CALLBACK(35)
FILE_ACTION_CALLBACK(36) FILE_ACTION_CALLBACK(37)
FILE_ACTION_CALLBACK(38) FILE_ACTION_CALLBACK(39)
FILE_ACTION_CALLBACK(40) FILE_ACTION_CALLBACK(41)
FILE_ACTION_CALLBACK(42) FILE_ACTION_CALLBACK(43)
FILE_ACTION_CALLBACK(44) FILE_ACTION_CALLBACK(45)
FILE_ACTION_CALLBACK(46) FILE_ACTION_CALLBACK(47)
FILE_ACTION_CALLBACK(48) FILE_ACTION_CALLBACK(49)
FILE_ACTION_CALLBACK(50) FILE_ACTION_CALLBACK(51)
FILE_ACTION_CALLBACK(52) FILE_ACTION_CALLBACK(53)
FILE_ACTION_CALLBACK(54) FILE_ACTION_CALLBACK(55)
FILE_ACTION_CALLBACK(56) FILE_ACTION_CALLBACK(57)
FILE_ACTION_CALLBACK(58) FILE_ACTION_CALLBACK(59)
FILE_ACTION_CALLBACK(60) FILE_ACTION_CALLBACK(61)
FILE_ACTION_CALLBACK(62) FILE_ACTION_CALLBACK(63)

static const s441_lvx_event_callback_t file_callbacks[S441_ACTION_COUNT] = {
    files_action_0, files_action_1, files_action_2, files_action_3,
    files_action_4, files_action_5, files_action_6, files_action_7,
    files_action_8, files_action_9, files_action_10, files_action_11,
    files_action_12, files_action_13, files_action_14, files_action_15,
    files_action_16, files_action_17, files_action_18, files_action_19,
    files_action_20, files_action_21, files_action_22, files_action_23,
    files_action_24, files_action_25, files_action_26, files_action_27,
    files_action_28, files_action_29, files_action_30, files_action_31,
    files_action_32, files_action_33, files_action_34, files_action_35,
    files_action_36, files_action_37, files_action_38, files_action_39,
    files_action_40, files_action_41, files_action_42, files_action_43,
    files_action_44, files_action_45, files_action_46, files_action_47,
    files_action_48, files_action_49, files_action_50, files_action_51,
    files_action_52, files_action_53, files_action_54, files_action_55,
    files_action_56, files_action_57, files_action_58, files_action_59,
    files_action_60, files_action_61, files_action_62, files_action_63,
};

static void files_title_back_clicked(void *event) {
    int result;
    (void)event;
    if (file_mode == S441_FILE_MODE_TEXT) {
        file_mode = S441_FILE_MODE_DIRECTORY;
    } else if (current_path[1]) {
        result = s441_fs_parent(current_path, path_buffer, sizeof(path_buffer));
        if (result == S441_FS_OK) result = load_directory(path_buffer, 0u);
        if (result != S441_FS_OK) set_file_error(directory_read_error);
    } else {
        S441_ACTIVITY_CLOSE(0u);
        return;
    }
    render_files();
}

static void cpu_refresh(void *event) {
    struct s441_page_view *view = &page_views[S441_PAGE_CPU];
    uint32_t cpu_percent = 0u;
    int cpu_result;
    (void)event;
    cpu_result = s441_fs_read_cpu(cpu_text, sizeof(cpu_text), &cpu_percent);
    if (cpu_result == S441_FS_OK || cpu_result == S441_FS_ERR_TRUNCATED)
        format_percent(cpu_text, sizeof(cpu_text), cpu_percent);
    else copy_text(cpu_text, sizeof(cpu_text), "Unavailable");
    format_metric(cpu_row_text, sizeof(cpu_row_text), "CPU", cpu_text);
    update_action(&view->actions[0], cpu_row_text, 1u);
    if (event) record_action(S441_ACTION_MONITOR_REFRESH, cpu_result);
}

static void memory_refresh(void *event) {
    struct s441_page_view *view = &page_views[S441_PAGE_MEMORY];
    uint32_t memory_percent = 0u;
    int memory_result;
    (void)event;
    memory_result = s441_fs_read_memory(memory_text, sizeof(memory_text),
        &memory_percent);
    if (memory_result == S441_FS_OK ||
            memory_result == S441_FS_ERR_TRUNCATED)
        format_percent(memory_text, sizeof(memory_text), memory_percent);
    else copy_text(memory_text, sizeof(memory_text), "Unavailable");
    format_metric(memory_row_text, sizeof(memory_row_text), "Memory",
        memory_text);
    update_action(&view->actions[0], memory_row_text, 1u);
    if (event) record_action(S441_ACTION_MEMORY_REFRESH, memory_result);
}

static void restart_confirm(void *event) {
    (void)event;
    if (!reboot_armed) {
        reboot_armed = 1u;
        record_action(S441_ACTION_RESTART_CONFIRM, 0);
        update_action(&page_views[S441_PAGE_RESTART].actions[0],
            "Tap again to restart", 1u);
        return;
    }
    /* Use the real managed reboot entry. 0x0c63e895 is poweroff. */
    record_action(S441_ACTION_RESTART_CONFIRM, 0);
    S441_SYSTEM_REBOOT(reboot_tag);
    update_action(&page_views[S441_PAGE_RESTART].actions[0],
        "Restart requested", 1u);
}

static void home_create(struct s441_page_descriptor *page, void *root,
        void *start_data) {
    struct s441_page_view *view = &page_views[S441_PAGE_HOME];
    (void)page;
    (void)start_data;
    if (!begin_page(view, root, "Shell++ II",
            S441_PAGE_TITLE_MODE_HOME, 0)) return;
    record_page_create(S441_PAGE_HOME);
    make_action(view->content_root, &view->actions[0],
        "Files", 0,
        S441_FIRST_ROW_GAP,
        home_files_clicked);
    make_action(view->content_root, &view->actions[1],
        "CPU",
        view->actions[0].row, S441_NEXT_ROW_GAP,
        home_status_clicked);
    make_action(view->content_root, &view->actions[2],
        "Memory",
        view->actions[1].row, S441_NEXT_ROW_GAP,
        home_memory_clicked);
    make_action(view->content_root, &view->actions[3],
        "Soft reboot",
        view->actions[2].row, S441_NEXT_ROW_GAP,
        home_restart_clicked);
    make_action(view->content_root, &view->actions[4],
        "About",
        view->actions[3].row, S441_NEXT_ROW_GAP,
        home_about_clicked);
}

static void files_create(struct s441_page_descriptor *page, void *root,
        void *start_data) {
    struct s441_page_view *view = &page_views[S441_PAGE_FILES];
    (void)page;
    (void)start_data;
    if (!begin_page(view, root, "Files",
            S441_PAGE_TITLE_MODE_STANDARD, files_title_back_clicked)) return;
    record_page_create(S441_PAGE_FILES);
    if (file_mode == S441_FILE_MODE_DIRECTORY &&
            load_directory(current_path, 0u) != S441_FS_OK)
        set_file_error(directory_read_error);
    view->body = make_info_panel(view, "Loading...", S441_TEXT_ALIGN_LEFT);
    if (!view->body) return;
    render_files();
}

static void cpu_create(struct s441_page_descriptor *page, void *root,
        void *start_data) {
    struct s441_page_view *view = &page_views[S441_PAGE_CPU];
    (void)page;
    (void)start_data;
    if (!begin_page(view, root, "CPU",
            S441_PAGE_TITLE_MODE_STANDARD, title_back_clicked)) return;
    record_page_create(S441_PAGE_CPU);
    make_action(view->content_root, &view->actions[0],
        "CPU reading...", 0, S441_FIRST_ROW_GAP, cpu_refresh);
    cpu_refresh(0);
}

static void memory_create(struct s441_page_descriptor *page, void *root,
        void *start_data) {
    struct s441_page_view *view = &page_views[S441_PAGE_MEMORY];
    (void)page;
    (void)start_data;
    if (!begin_page(view, root, "Memory",
            S441_PAGE_TITLE_MODE_STANDARD, title_back_clicked)) return;
    record_page_create(S441_PAGE_MEMORY);
    make_action(view->content_root, &view->actions[0],
        "Memory reading...",
        0, S441_FIRST_ROW_GAP, memory_refresh);
    memory_refresh(0);
}

static void restart_create(struct s441_page_descriptor *page, void *root,
        void *start_data) {
    struct s441_page_view *view = &page_views[S441_PAGE_RESTART];
    (void)page;
    (void)start_data;
    if (!begin_page(view, root, "Soft reboot",
            S441_PAGE_TITLE_MODE_STANDARD, title_back_clicked)) return;
    record_page_create(S441_PAGE_RESTART);
    make_action(view->content_root, &view->actions[0],
        "Soft reboot", 0,
        S441_FIRST_ROW_GAP,
        restart_confirm);
}

static void about_create(struct s441_page_descriptor *page, void *root,
        void *start_data) {
    struct s441_page_view *view = &page_views[S441_PAGE_ABOUT];
    (void)page;
    (void)start_data;
    if (!begin_page(view, root, "About",
            S441_PAGE_TITLE_MODE_STANDARD, title_back_clicked)) return;
    record_page_create(S441_PAGE_ABOUT);
    view->body = make_info_panel(view, about_text, S441_TEXT_ALIGN_LEFT);
    if (view->body)
        S441_LVX_OBJECT_CLEAR_FLAG(view->body, S441_OBJECT_FLAG_HIDDEN);
}

static void notification_open(struct s441_notification_descriptor *message,
        void *root) {
    (void)message;
    /* Notification detail roots belong to the notification host rather than
     * Activity Manager. Let the host own back navigation. */
    if (!begin_page(&notification_view, root, "Shell++ II",
            S441_PAGE_TITLE_MODE_STANDARD, 0)) return;
    make_action(notification_view.content_root,
        &notification_view.actions[0], "Open Shell++ II",
        0,
        S441_FIRST_ROW_GAP,
        notification_launch_clicked);
}

static void notification_close(struct s441_notification_descriptor *message,
        void *context) {
    uint32_t index;
    (void)message;
    (void)context;
    notification_view.root = 0;
    notification_view.title_bar = 0;
    notification_view.content_root = 0;
    notification_view.body_panel = 0;
    notification_view.body = 0;
    for (index = 0u; index < S441_ACTION_COUNT; ++index) {
        notification_view.actions[index].row = 0;
        notification_view.actions[index].callback = 0;
    }
}

const struct s441_app_descriptor *s441_ui_app_descriptor(void) {
    initialize_firmware_callbacks();
    return &app_descriptor;
}

void *const *s441_ui_pages(void) {
    initialize_firmware_callbacks();
    return page_table;
}

uint32_t s441_ui_page_count(void) {
    return S441_PAGE_COUNT;
}

void s441_ui_prepare_loaded_notification(
        struct s441_notification_descriptor *notification) {
    if (!notification) return;
    notification->open_detail = (s441_notification_open_t)
        thumb_callback_address((const void *)notification_open);
    notification->close_detail = (s441_notification_close_t)
        thumb_callback_address((const void *)notification_close);
    notification->context = notification;
}

static void loaded_toast_builder(void *toast_root, uint32_t destroying) {
    if (destroying) {
        if (loaded_toast_root == toast_root) {
            loaded_toast_root = 0;
            loaded_toast_action.row = 0;
            loaded_toast_action.callback = 0;
        }
        return;
    }
    ++ui_diagnostics.toast_create_count;
    ui_diagnostics.last_toast_root = (uint32_t)(uintptr_t)toast_root;
    if (!toast_root) {
        ++ui_diagnostics.toast_create_failures;
        return;
    }
    loaded_toast_root = toast_root;
    make_action(toast_root, &loaded_toast_action, "Shell++ II loaded",
        0, 0, notification_launch_clicked);
    if (!loaded_toast_action.row) {
        ++ui_diagnostics.toast_create_failures;
        return;
    }
    S441_LVX_OBJECT_ALIGN(loaded_toast_action.row, S441_ALIGN_CENTER, 0, 0);
}

int s441_ui_show_loaded_toast(void) {
    uint32_t before = ui_diagnostics.toast_create_count;
    uint32_t failures = ui_diagnostics.toast_create_failures;
    S441_CREATE_TOAST((s441_toast_builder_t)
        thumb_callback_address((const void *)loaded_toast_builder),
        &loaded_toast_action, 1u, &loaded_toast_action, 1u);
    if (ui_diagnostics.toast_create_count == before ||
            ui_diagnostics.toast_create_failures != failures ||
            !loaded_toast_root || !loaded_toast_action.row) return -1;
    return 0;
}

const struct s441_ui_diagnostics *s441_ui_diagnostics(void) {
    return &ui_diagnostics;
}
