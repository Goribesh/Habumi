/* SPDX-License-Identifier: GPL-2.0-only */
/* Conserva lo scancode fisico; ricava solo quello omesso dagli input sintetici. */
#ifndef WINQ_KEYCODE_H
#define WINQ_KEYCODE_H
#include <windows.h>
static inline unsigned winq_key_scancode(WPARAM vk, LPARAM info)
{
    unsigned scan = (info >> 16) & 0xff;
    if (!scan && vk != VK_PACKET) {
        scan = MapVirtualKeyExA((UINT)vk, MAPVK_VK_TO_VSC_EX, GetKeyboardLayout(0));
        /* Sul layout italiano l'API restituisce 0x4d anche per VK_RIGHT:
         * i VK di navigazione dedicati non sono quelli del tastierino. */
        switch (vk) {
        case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
        case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
        case VK_INSERT: case VK_DELETE: case VK_DIVIDE:
        case VK_RCONTROL: case VK_RMENU: case VK_LWIN: case VK_RWIN: case VK_APPS:
            if (scan) { scan |= 0xe000; }
            break;
        }
    }
    if ((info & (1 << 24)) && scan) { scan |= 0xe000; }
    return scan;
}
#endif
