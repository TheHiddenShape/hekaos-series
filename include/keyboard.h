#ifndef KEYBOARD_H
#define KEYBOARD_H

typedef enum
{
    KEYMAP_QWERTY,
    KEYMAP_AZERTY,
} keymap_t;

void set_keymap (keymap_t layout);

#endif
