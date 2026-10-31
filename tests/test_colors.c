/* cc -std=c11 -O2 tests/test_colors.c -lm -o .cache/test-colors */
#define main om_application_main
#include "../src/om.c"
#undef main
#include <assert.h>

static int component(uint32_t rgb, int shift) { return (int)((rgb >> shift) & 255u); }

int main(void) {
    Color parsed;
    assert(parse_background("\033]11;rgb:30/18/3c\a",&parsed));
    assert(parsed.r == 48 && parsed.g == 24 && parsed.b == 60);
    assert(parse_background("\033]11;rgb:aaaa/bbbb/cccc\033\\",&parsed));
    assert(parsed.r == 170 && parsed.g == 187 && parsed.b == 204);
    assert(parse_background("\033]11;rgb:a/b/c\a",&parsed));
    assert(parsed.r == 170 && parsed.g == 187 && parsed.b == 204);
    assert(!parse_background("\033]11;rgb:12345/ff/00\a",&parsed));
    assert(!parse_background("\033]11;rgb:-1/ff/00\a",&parsed));
    assert(!parse_background("\033]11;rgb:gg/ff/00\a",&parsed));
    Color themes[] = {{48,24,60},{235,240,245},{12,45,39},{0,0,0}};
    for (size_t theme = 0; theme < sizeof themes/sizeof themes[0]; ++theme) {
        terminal_background = themes[theme];
        background_known = true;
        uint32_t expected = ((uint32_t)themes[theme].r << 16)
                          | ((uint32_t)themes[theme].g << 8) | (uint32_t)themes[theme].b;
        for (int palette = 0; palette < PALETTE_COUNT; ++palette) {
            for (int step = 0; step < 100; ++step) {
                double phase = step/100.0;
                assert(color_at(palette,palette,1,phase,0,0) == expected);
                uint32_t ink = color_at(palette,palette,1,phase,1,0);
                for (int sample = 0; sample <= 20; ++sample) {
                    uint32_t faded = color_at(palette,palette,1,phase,sample/20.0,0);
                    for (int shift = 0; shift <= 16; shift += 8) {
                        int a = component(expected,shift), b = component(ink,shift);
                        int value = component(faded,shift);
                        assert(value >= (a < b ? a : b) && value <= (a > b ? a : b));
                    }
                }
            }
        }
    }
    background_known = false;
    for (int palette = 0; palette < PALETTE_COUNT; ++palette)
        for (int step = 0; step < 100; ++step) {
            uint32_t rgb = color_at(palette,palette,1,step/100.0,.025,0);
            int maximum = component(rgb,0);
            if (component(rgb,8) > maximum) maximum = component(rgb,8);
            if (component(rgb,16) > maximum) maximum = component(rgb,16);
            assert(maximum >= 70);
        }
    puts("Theme fades follow dark, light, and tinted backgrounds; unsupported terminals retain colored ink.");
    return 0;
}
