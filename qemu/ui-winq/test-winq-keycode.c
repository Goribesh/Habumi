/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include "winq-keycode.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d\n", __LINE__); return 1; } } while (0)
int main(void)
{
    CHECK(winq_key_scancode('Z', 0x2e << 16) == 0x2e);
    CHECK(winq_key_scancode(VK_RIGHT, (0x4d << 16) | (1 << 24)) == 0xe04d);
    CHECK(winq_key_scancode(VK_TAB, 0) == 0x0f);
    CHECK(winq_key_scancode(VK_RIGHT, 0) == 0xe04d);
    CHECK(winq_key_scancode(VK_PACKET, 0) == 0);
    puts("winq-keycode: 5 checks passed");
    return 0;
}
