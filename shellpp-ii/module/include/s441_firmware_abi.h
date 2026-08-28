#ifndef S441_FIRMWARE_ABI_H
#define S441_FIRMWARE_ABI_H

#include <stdint.h>

/*
 * Xiaomi Watch S4 41 mm (O63/s441), Vela 3.100.028 only.
 *
 * These are module flash aliases for the matching 0x2c... Thumb entrypoints
 * in vela_ap.bin with SHA-256
 * c1a738d70ff5284569439bbcfb1212a94f357cd81c6f715d7a7a34ef0155912a.
 * AP RAM objects intentionally retain their 0x3c... runtime addresses.
 */

struct s441_page_descriptor;

typedef int (*s441_register_driver_t)(const char *path, const void *fops,
    uint32_t mode);
typedef int (*s441_app_register_t)(const void *app,
    void *const *pages, uint32_t page_count);
typedef void *(*s441_app_lookup_t)(uint16_t app_id);
typedef void (*s441_launcher_add_t)(uint16_t app_id);
typedef void (*s441_activity_activate_t)(uint32_t combined_id, uint32_t arg);
typedef void (*s441_activity_close_t)(uint32_t combined_id);
/* miwear_system_reboot retains the tag while scheduling a managed reboot. */
typedef void (*s441_system_reboot_t)(const char *tag);
typedef int32_t (*s441_open_t)(const char *path, int32_t flags, int32_t mode);
typedef int32_t (*s441_read_t)(int32_t fd, void *buffer, uint32_t count);
typedef int32_t (*s441_close_t)(int32_t fd);
typedef void *(*s441_opendir_t)(const char *path);
typedef int32_t (*s441_closedir_t)(void *directory);
typedef uint8_t *(*s441_readdir_t)(void *directory);
typedef void *(*s441_lvx_label_create_t)(void *parent);
typedef void (*s441_lvx_label_set_text_t)(void *label, const char *text);
typedef void (*s441_lvx_object_align_t)(void *object, uint32_t alignment,
    int32_t x_offset, int32_t y_offset);
typedef void (*s441_lvx_object_align_to_t)(void *object, void *base,
    uint32_t alignment, int32_t x_offset, int32_t y_offset);
typedef void (*s441_lvx_object_set_width_t)(void *object, int32_t width);
typedef void (*s441_lvx_object_set_size_t)(void *object, int32_t width,
    int32_t height);
typedef void (*s441_lvx_object_flag_t)(void *object, uint32_t flags);
typedef void (*s441_lvx_object_set_text_color_t)(void *object,
    uint32_t color, uint32_t selector);
typedef void (*s441_lvx_object_set_text_align_t)(void *object,
    uint32_t alignment, uint32_t selector);
typedef void *(*s441_lvx_object_get_text_font_t)(void *object);
typedef void (*s441_lvx_object_set_text_font_t)(void *object,
    const void *font, uint32_t selector);
typedef void (*s441_lvx_object_set_bg_color_t)(void *object,
    uint32_t color, uint32_t selector);
typedef void (*s441_lvx_object_set_bg_opacity_t)(void *object,
    uint32_t opacity, uint32_t selector);
typedef void (*s441_lvx_object_set_radius_t)(void *object,
    int32_t radius, uint32_t selector);
typedef void (*s441_lvx_event_callback_t)(void *event);
typedef void (*s441_lvx_event_add_t)(void *object,
    s441_lvx_event_callback_t callback, uint32_t event_code, void *context);
typedef void (*s441_toast_builder_t)(void *toast_root, uint32_t destroying);
typedef void (*s441_create_toast_t)(s441_toast_builder_t builder,
    void *user_data, uint32_t clickable, void *context, uint32_t style);
/*
 * O63 native list-row ABI recovered from the 0x2C417E34 and 0x2C4FD918
 * callers. The final three row_init arguments are stack arguments. Type 3 is
 * the larger menu variant used by the 0x2C417E34 helper; its complete
 * 0x2C4961B8 call site passes NULL as the second argument. Type 8 with a
 * NULL second argument and secondary label is the compact text row used by the built-in
 * two-row round-screen page at 0x2C4FD918.
 * row_init applies the active native theme and recalculates row geometry.
 * Therefore callers must not copy the 384x100 dimensions seen at the
 * unrelated data-driven 0x2C38FDCC call site. The built-in round-screen path
 * also relies on the row class for interaction state and only adds CLICKED=7.
 *
 * 0x2C6C837D is deliberately not exposed: it is a private post-processor
 * for specific row owners, not a general row finalizer.
 */
typedef void *(*s441_lvx_row_create_t)(void *parent);
typedef void (*s441_lvx_row_init_t)(void *row, const void *icon,
    const char *primary, const char *secondary, uint32_t extra,
    uint32_t selected, uint32_t type);
/* The second row_update argument releases the icon owned by row_init when
 * nonzero. It is not a replacement icon pointer. */
typedef void (*s441_lvx_row_update_t)(void *row, uint32_t release_icon,
    const char *primary, const char *secondary, uint32_t extra,
    uint32_t selected);
/* O63 generic LVGL object factory used for full-screen page content. */
typedef void *(*s441_lvx_content_create_t)(void *root);
/*
 * O63 page-content helper recovered at 0x2C4547AC. It creates a generic
 * content object, applies the firmware page-content style, sets width and
 * height, and clears SCROLLABLE on the container. The built-in caller at
 * 0x2C496214 passes LV_SIZE_CONTENT for both dimensions; other callers pass
 * explicit pixel dimensions.
 */
typedef void *(*s441_lvx_page_content_create_t)(void *parent,
    int32_t width, int32_t height);
/*
 * Recovered from the O63 lvx_page_title_create body at 0x2C5274E8.  The
 * fifth argument is passed in the first stack argument slot and becomes the
 * LVGL event user-data for the optional back callback.
 */
typedef void *(*s441_lvx_page_title_create_t)(void *root, const char *title,
    uint32_t mode, const void *back_callback, void *context);

struct s441_notification_descriptor;
typedef void (*s441_notification_open_t)(
    struct s441_notification_descriptor *message, void *root);
typedef void (*s441_notification_close_t)(
    struct s441_notification_descriptor *message, void *context);
/* Returns the normalized manager-owned message, or NULL on failure. */
typedef void *(*s441_notification_insert_t)(
    struct s441_notification_descriptor *message);
/*
 * O63 notification lifecycle calls this with 1 after creating the reminder
 * host and with 0 before destroying it. Enabling is idempotent: the function
 * subscribes the host to foreground-reminder events only when it is not
 * already subscribed.
 */
typedef void (*s441_notification_reminder_listener_t)(uint32_t enabled);

typedef void (*s441_page_create_t)(struct s441_page_descriptor *page,
    void *root, void *start_data);
typedef void (*s441_page_cleanup_t)(struct s441_page_descriptor *page);

#define S441_REGISTER_DRIVER ((s441_register_driver_t)0x0c6fe05du)
#define S441_APP_REGISTER ((s441_app_register_t)0x0c63b391u)
#define S441_APP_LOOKUP ((s441_app_lookup_t)0x0c639ba9u)
#define S441_LAUNCHER_ADD ((s441_launcher_add_t)0x0c412425u)
#define S441_ACTIVITY_ACTIVATE ((s441_activity_activate_t)0x0c60f0f9u)
#define S441_ACTIVITY_CLOSE ((s441_activity_close_t)0x0c60f3adu)
/* 0x0c63e895 is miwear_system_poweroff and must never be used here. */
#define S441_SYSTEM_REBOOT ((s441_system_reboot_t)0x0c63e831u)
#define S441_OPEN ((s441_open_t)0x0c54e519u)
#define S441_READ ((s441_read_t)0x0c54e2fbu)
#define S441_CLOSE ((s441_close_t)0x0c54e33du)
/*
 * O63 libc directory wrappers.  The open wrapper owns a 0x68-byte object and
 * readdir returns its 0x62-byte embedded dirent buffer (d_type followed by
 * d_name).  These wrappers, rather than a guessed VFS internal, keep the
 * native app on the same ABI as firmware callers.
 */
#define S441_OPENDIR ((s441_opendir_t)0x0c1fdd51u)
#define S441_CLOSEDIR ((s441_closedir_t)0x0c1fdd89u)
#define S441_READDIR ((s441_readdir_t)0x0c1fddb1u)
#define S441_LVX_LABEL_CREATE ((s441_lvx_label_create_t)0x0c4c9af1u)
#define S441_LVX_LABEL_SET_TEXT ((s441_lvx_label_set_text_t)0x0c4ccb91u)
#define S441_LVX_OBJECT_ALIGN ((s441_lvx_object_align_t)0x0c41c6f9u)
#define S441_LVX_OBJECT_ALIGN_TO \
    ((s441_lvx_object_align_to_t)0x0c41e155u)
#define S441_LVX_OBJECT_SET_WIDTH ((s441_lvx_object_set_width_t)0x0c41c735u)
#define S441_LVX_OBJECT_SET_SIZE \
    ((s441_lvx_object_set_size_t)0x0c41c7afu)
#define S441_LVX_OBJECT_ADD_FLAG \
    ((s441_lvx_object_flag_t)0x0c41ad21u)
#define S441_LVX_OBJECT_CLEAR_FLAG \
    ((s441_lvx_object_flag_t)0x0c41afd9u)
#define S441_LVX_OBJECT_SET_TEXT_COLOR \
    ((s441_lvx_object_set_text_color_t)0x0c41ca11u)
#define S441_LVX_OBJECT_SET_TEXT_ALIGN \
    ((s441_lvx_object_set_text_align_t)0x0c41cf71u)
#define S441_LVX_OBJECT_GET_TEXT_FONT \
    ((s441_lvx_object_get_text_font_t)0x0c533d11u)
#define S441_LVX_OBJECT_SET_TEXT_FONT \
    ((s441_lvx_object_set_text_font_t)0x0c41cf45u)
#define S441_LVX_OBJECT_SET_BG_COLOR \
    ((s441_lvx_object_set_bg_color_t)0x0c41c869u)
#define S441_LVX_OBJECT_SET_BG_OPACITY \
    ((s441_lvx_object_set_bg_opacity_t)0x0c41c889u)
#define S441_LVX_OBJECT_SET_RADIUS \
    ((s441_lvx_object_set_radius_t)0x0c41c921u)
#define S441_LVX_EVENT_ADD ((s441_lvx_event_add_t)0x0c417e0du)
#define S441_LVX_ROW_CREATE ((s441_lvx_row_create_t)0x0c51f9b9u)
#define S441_LVX_ROW_INIT ((s441_lvx_row_init_t)0x0c52e1a1u)
#define S441_LVX_ROW_UPDATE ((s441_lvx_row_update_t)0x0c52702du)
#define S441_LVX_CONTENT_CREATE ((s441_lvx_content_create_t)0x0c38903bu)
#define S441_LVX_PAGE_CONTENT_CREATE \
    ((s441_lvx_page_content_create_t)0x0c4547adu)
#define S441_LVX_PAGE_TITLE_CREATE \
    ((s441_lvx_page_title_create_t)0x0c5274e9u)
#define S441_CREATE_TOAST ((s441_create_toast_t)0x0c545e6du)
#define S441_NOTIFICATION_INSERT \
    ((s441_notification_insert_t)0x0c643de5u)
#define S441_NOTIFICATION_REMINDER_LISTENER \
    ((s441_notification_reminder_listener_t)0x0c643eedu)

#define S441_PAGE_PROTOTYPE ((void *)0x3c2040d0u)
#define S441_PAGE_ACTIVITY_API ((void *)0x3c203ff4u)
#define S441_APP_INSTALL_LOOP (*(void * volatile *)0x3c2e94c0u)
#define S441_NOTIFICATION_HOST (*(void * volatile *)0x3c2e8e5cu)
/* The firmware copies this fixed public record during app registration. */
struct s441_app_descriptor {
    void *registry_prev;
    void *registry_next;
    const char *package_name;
    const char *app_path;
    uint16_t app_id;
    uint16_t app_flags;
    const char *name_resource;
    const char *icon_resource;
    const char *(*resolve_display_name)(struct s441_app_descriptor *app);
    void *private_callback_20;
    void *pre_unregister;
    void *release_runtime;
    void *query_state;
    void *manager_page_table;
    void *secondary_page_table;
    void *lifecycle;
    uint32_t state_flags;
};

/* The Activity Manager retains and mutates this fixed public record in place. */
struct s441_page_descriptor {
    void *prototype;
    uint32_t reserved_04;
    uint32_t reserved_08;
    uint32_t reserved_0c;
    const char *page_name;
    uint16_t page_id;
    uint16_t app_id;
    uint32_t flags_18;
    uint32_t reserved_1c;
    uint32_t reserved_20;
    uint32_t manager_refcount;
    uint8_t state_28;
    uint8_t state_29;
    uint8_t type_2a;
    uint8_t reserved_2b;
    void *activity_api;
    void *root_object;
    void (*lifecycle)(struct s441_page_descriptor *page,
        uint32_t event, uint32_t argument);
    void *default_context;
    uint32_t reserved_3c;
    void *manager_prev;
    void *manager_next;
    void *object_api;
    s441_page_create_t create;
    void *callback_50;
    void *callback_54;
    void *callback_58;
    void *callback_5c;
    void *callback_60;
    s441_page_cleanup_t cleanup;
    void *callback_68;
    void *callback_6c;
    uint32_t reserved_70;
};

/*
 * O63 notification public ABI. The first two words form the 64-bit message
 * UID used by the manager for lookup/update; +0x08 is the message ID. The
 * service copies this 0x58-byte record and normalizes optional fields. Leaving
 * +0x1C/+0x20 null selects the firmware's default notification icons.
 */
struct s441_notification_descriptor {
    uint32_t uid_low;
    uint32_t uid_high;
    uint32_t message_id;
    const char *string_0c;
    const char *string_10;
    const char *string_14;
    const char *string_18;
    const char *string_1c;
    const char *string_20;
    const char *string_24;
    const char *string_28;
    uint32_t timestamp;
    uint32_t reserved_30;
    uint32_t reserved_34;
    uint32_t reserved_38;
    uint32_t reserved_3c;
    uint32_t reserved_40;
    uint32_t reserved_44;
    s441_notification_open_t open_detail;
    s441_notification_close_t close_detail;
    uint8_t foreground_alert;
    uint8_t manager_state_51;
    uint8_t manager_state_52;
    uint8_t reserved_53;
    void *context;
};

typedef char s441_app_descriptor_size[
    sizeof(struct s441_app_descriptor) == 0x40u ? 1 : -1];
typedef char s441_page_descriptor_size[
    sizeof(struct s441_page_descriptor) == 0x74u ? 1 : -1];
typedef char s441_notification_descriptor_size[
    sizeof(struct s441_notification_descriptor) == 0x58u ? 1 : -1];
typedef char s441_notification_open_offset[
    __builtin_offsetof(struct s441_notification_descriptor, open_detail) ==
        0x48u ? 1 : -1];
typedef char s441_notification_close_offset[
    __builtin_offsetof(struct s441_notification_descriptor, close_detail) ==
        0x4cu ? 1 : -1];
typedef char s441_notification_alert_offset[
    __builtin_offsetof(struct s441_notification_descriptor, foreground_alert) ==
        0x50u ? 1 : -1];
typedef char s441_notification_context_offset[
    __builtin_offsetof(struct s441_notification_descriptor, context) ==
        0x54u ? 1 : -1];

#endif
