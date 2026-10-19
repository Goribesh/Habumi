#include <assert.h>
#include <stdio.h>

#include "layout-compatto.h"

int main(void)
{
    CompatLayout layout;
    CompatSize size;

    layout = compat_layout(900, 700, 240);
    assert(layout.view_width == 660 && layout.view_height == 700);
    assert(layout.rail_x == 660 && layout.rail_width == 240);

    layout = compat_layout(400, 800, 100);
    assert(layout.view_width == 300 && layout.view_height == 800);
    assert(layout.rail_x == 300 && layout.rail_width == 100);

    layout = compat_layout(4, 0, 12);
    assert(layout.view_width == 0 && layout.view_height == 0);
    assert(layout.rail_x == 0 && layout.rail_width == 4);
    layout = compat_layout(-4, -8, -2);
    assert(layout.view_width == 0 && layout.view_height == 0);
    assert(layout.rail_x == 0 && layout.rail_width == 0);

    size = compat_fit(1080, 1920, 1000, 900, 200);
    assert(size.width == 706 && size.height == 900);
    size = compat_fit(1920, 1080, 1200, 800, 160);
    assert(size.width == 1200 && size.height == 585);

    size = compat_fit(100, 100, 100, 100, 100);
    assert(size.width == 0 && size.height == 0);
    size = compat_fit(100, 100, 0, 100, 0);
    assert(size.width == 0 && size.height == 0);
    size = compat_fit(-1, 100, 100, 100, 0);
    assert(size.width == 0 && size.height == 0);
    size = compat_fit(100, 100, 100, 100, -1);
    assert(size.width == 0 && size.height == 0);

    size = compat_fit(32767, 1, 1, 32767, 0);
    assert(size.width == 1 && size.height == 1);
    size = compat_fit(1, 32767, 32767, 1, 0);
    assert(size.width == 1 && size.height == 1);
    size = compat_fit(32767, 32767, 32767, 32767, 480);
    assert(size.width == 32767 && size.height == 32287);

    size = compat_fit(1600, 900, 1600, 900, 240);
    assert(size.width == 1600 && size.height == 765);
    puts("layout-compatto: ok");
    return 0;
}
