#ifndef S441_NATIVE_UI_H
#define S441_NATIVE_UI_H

#include <stdint.h>

#include "s441_firmware_abi.h"

#define S441_APP_ID 0x00cdu

struct s441_ui_diagnostics {
    uint32_t page_create_count;
    uint32_t click_count;
    uint32_t last_action;
    uint32_t last_page;
    int32_t last_result;
    uint32_t row_create_count;
    uint32_t row_create_failures;
    uint32_t last_root;
    uint32_t last_title;
    uint32_t last_content;
    uint32_t last_row;
    uint32_t toast_create_count;
    uint32_t toast_create_failures;
    uint32_t last_toast_root;
};

const struct s441_app_descriptor *s441_ui_app_descriptor(void);
void *const *s441_ui_pages(void);
uint32_t s441_ui_page_count(void);
void s441_ui_prepare_loaded_notification(
    struct s441_notification_descriptor *notification);
int s441_ui_show_loaded_toast(void);
const struct s441_ui_diagnostics *s441_ui_diagnostics(void);

#endif
