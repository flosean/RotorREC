#pragma once

typedef void (*boot_button_callback_t)(void);

void boot_button_start(boot_button_callback_t short_press,
                       boot_button_callback_t long_press);
