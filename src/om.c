#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "mantra.h"

#define PI 3.14159265358979323846
#define SLOT 6
#define PALETTE_COUNT 9
#define MODE_COUNT 11

typedef struct { double r, g, b; } Color;
typedef struct { const char *name; Color stops[4]; } Palette;
static const Palette palettes[] = {
    {"jade",     {{10,48,45}, {13,152,110}, {81,233,151}, {202,255,214}}},
    {"lotus",    {{52,18,88}, {172,45,149}, {255,128,166}, {255,226,187}}},
    {"glacier",  {{13,28,87}, {24,116,183}, {68,218,233}, {213,252,255}}},
    {"ember",    {{68,12,37}, {182,36,65}, {255,113,53}, {255,230,145}}},
    {"nebula",   {{31,19,86}, {104,55,196}, {221,82,222}, {189,226,255}}},
    {"saffron",  {{62,29,12}, {171,85,20}, {246,184,54}, {255,246,190}}},
    {"peacock",  {{19,28,81}, {29,128,148}, {61,223,177}, {227,171,255}}},
    {"prism",    {{65,55,211}, {230,66,152}, {249,174,69}, {62,231,200}}},
    {"rainbow",  {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}}},
};
static const char *modes[] = {"rain", "river", "lotus", "bloom", "incense", "lanterns", "mandala", "whirlpool", "flag", "lava", "galaxy"};
/* Keep the words intact: never animate an isolated vowel mark or virama. */
static const char *devanagari[] = {"ॐ", "मणि", "पद्मे", "हूँ"};
static const char *latin[] = {"om", "mani", "padme", "hum"};
typedef struct { uint32_t rgb; int glyph; bool bold; } Tile;
typedef struct { double x, y, light; } Flow;
typedef struct {
    Flow from_offset, to_offset, origin, velocity;
    Flow gather_origin, gather_velocity, gather_target, release_offset;
} Particle;
typedef struct {
    int from, to;
    double changed, duration;
    bool linear_source;
} Motion;
typedef struct { double speed, offset, length, gap; int phase; } Stream;
typedef struct {
    double gather, hold, release, phase, vx, vy, scale;
    int style, path;
    unsigned seed;
} Gathering;
typedef struct { double left, top, scale; } MantraPose;
typedef struct {
    int width, height, columns;
    Tile *previous, *frame;
    Stream *streams;
    Particle *particles;
    int particle_columns, particle_count;
    Motion motion;
    Flow *mantra_points;
    int mantra_point_count, mantra_event, mantra_stage;
    bool mantra_prepared;
    Gathering gathering;
    MantraPose mantra_pose;
    const MantraMask *mantra_mask;
    double mantra_left, mantra_top, mantra_width, mantra_height;
    double mantra_patch_width, mantra_patch_height;
} Canvas;
typedef struct {
    int fps, palette, mode, color;
    double speed, density, interval, scene_interval, mantra_interval, duration;
    unsigned seed;
    bool ascii, palette_auto, mode_auto, no_mantra;
} Options;

static struct termios saved_termios;
static bool terminal_active;
static volatile sig_atomic_t stopped, resized, suspended, continued;
static uint32_t rng_state;
static Color terminal_background;
static bool background_known, background_queried;
static char pending_keys[128];
static size_t pending_key_count;

static double clamp(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
static double mix(double a, double b, double t) { return a + (b - a) * t; }
static double smooth(double t) { t = clamp(t, 0, 1); return t * t * (3 - 2 * t); }
static double smoother(double t) {
    t = clamp(t,0,1);
    return t*t*t*(t*(6*t-15)+10);
}
static double mod(double x, double n) { return x - floor(x / n) * n; }
static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
static double random_unit(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (double)rng_state / UINT32_MAX;
}
/* Stateless variation: frame rate, pausing and resize never consume randomness. */
static double variation(unsigned seed, unsigned key) {
    uint32_t h = seed ^ (key*0x9e3779b9u);
    h = (h ^ (h >> 16))*0x7feb352du;
    h = (h ^ (h >> 15))*0x846ca68bu;
    h ^= h >> 16;
    return h/4294967296.0;
}
static void on_signal(int sig) {
    if (sig == SIGWINCH) resized = 1;
    else if (sig == SIGTSTP) suspended = 1;
    else if (sig == SIGCONT) continued = 1;
    else stopped = sig;
}
static void restore_terminal(void) {
    if (!terminal_active) return;
    fputs("\033[?2026l\033[0m\033[?25h\033[?1049l", stdout);
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
    terminal_active = false;
}
static bool parse_background(const char *text, Color *color) {
    if (strncmp(text,"\033]11;rgb:",9)) return false;
    const char *p = text+9;
    double channels[3];
    for (int channel = 0; channel < 3; ++channel) {
        size_t digits = strspn(p,"0123456789abcdefABCDEF");
        if (digits < 1 || digits > 4) return false;
        char *end;
        unsigned long value = strtoul(p,&end,16);
        if (end != p+digits) return false;
        unsigned long maximum = (1ul << (digits*4))-1;
        if (value > maximum) return false;
        channels[channel] = (double)value*255.0/(double)maximum;
        if (channel < 2) {
            if (*end != '/') return false;
            p = end+1;
        } else if (*end != '\a' && strncmp(end,"\033\\",2)) return false;
    }
    *color = (Color){channels[0],channels[1],channels[2]};
    return true;
}
static void query_background(void) {
    if (background_queried) return;
    background_queried = true;
    /* OSC 11 asks for the configured background, including non-black themes.
       This query is invisible; unsupported terminals get a short bounded wait. */
    fputs("\033]11;?\033\\",stdout);
    fflush(stdout);
    char reply[512] = {0};
    size_t count = 0;
    double deadline = now()+.08;
    while (!stopped && count < sizeof reply-1) {
        double remaining = deadline-now();
        if (remaining <= 0) break;
        struct pollfd input = {.fd=STDIN_FILENO,.events=POLLIN};
        if (poll(&input,1,(int)ceil(remaining*1000)) <= 0) break;
        ssize_t got = read(STDIN_FILENO,reply+count,sizeof reply-1-count);
        if (got <= 0) break;
        count += (size_t)got;
        reply[count] = '\0';
        char *start = strstr(reply,"\033]11;");
        if (!start) continue;
        char *end = strchr(start,'\a');
        size_t ending = 1;
        if (!end) { end = strstr(start,"\033\\"); ending = 2; }
        if (!end) continue;
        background_known = parse_background(start,&terminal_background);
        size_t before = (size_t)(start-reply), after = (size_t)(end-reply)+ending;
        memmove(reply+before,reply+after,count-after);
        count = before+count-after;
        reply[count] = '\0';
        break;
    }
    /* Preserve any keys pressed during the query. Don't treat a partial OSC
       reply as an Escape key if the terminal answered past the timeout. */
    char *partial = strstr(reply,"\033]11;");
    if (partial) count = (size_t)(partial-reply);
    if (count > sizeof pending_keys) count = sizeof pending_keys;
    memcpy(pending_keys,reply,count);
    pending_key_count = count;
}
static ssize_t read_keys(char *keys, size_t capacity) {
    if (pending_key_count) {
        size_t count = pending_key_count < capacity ? pending_key_count : capacity;
        memcpy(keys,pending_keys,count);
        memmove(pending_keys,pending_keys+count,pending_key_count-count);
        pending_key_count -= count;
        return (ssize_t)count;
    }
    struct pollfd input = {.fd=STDIN_FILENO,.events=POLLIN};
    if (poll(&input,1,0) <= 0) return 0;
    if (input.revents & (POLLHUP|POLLERR|POLLNVAL)) return -1;
    ssize_t count = read(STDIN_FILENO,keys,capacity);
    return count < 0 && errno == EINTR ? 0 : count;
}
static bool enter_terminal(void) {
    struct termios raw = saved_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) return false;
    terminal_active = true;
    query_background();
    fputs("\033[?1049h\033[?25l\033[0m\033[49m\033[2J", stdout);
    return true;
}
static void invalidate(Canvas *c) {
    if (c->previous)
        for (int i = 0; i < c->width * c->height; ++i) c->previous[i].glyph = -2;
}
static bool resize_canvas(Canvas *c) {
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) < 0 || !size.ws_col || !size.ws_row)
        return false;
    int width = size.ws_col, height = size.ws_row;
    /* Protect allocation and output volume on unusually large/invalid PTYs. */
    if (width > 4096) width = 4096;
    if (height > 1024) height = 1024;
    int columns = (width + SLOT - 1) / SLOT;
    Tile *tiles = calloc((size_t)width * (size_t)height, sizeof *tiles);
    Tile *frame = calloc((size_t)width * (size_t)height, sizeof *frame);
    Stream *streams = calloc((size_t)columns, sizeof *streams);
    int particle_columns = (int)ceil((width+32.0)/7);
    int particle_count = particle_columns*((int)ceil(height/2.5)+9);
    Particle *particles = calloc((size_t)particle_count,sizeof *particles);
    if (!tiles || !frame || !streams || !particles) {
        free(tiles); free(frame); free(streams); free(particles); return false;
    }
    free(c->previous);
    free(c->frame);
    free(c->streams);
    free(c->particles);
    free(c->mantra_points);
    c->previous = tiles;
    c->frame = frame;
    c->streams = streams;
    c->particles = particles;
    c->mantra_points = NULL;
    c->mantra_point_count = 0;
    c->mantra_event = -1;
    c->mantra_stage = 0;
    c->mantra_prepared = false;
    c->particle_columns = particle_columns;
    c->particle_count = particle_count;
    c->width = width;
    c->height = height;
    c->columns = columns;
    for (int x = 0; x < columns; ++x) {
        streams[x] = (Stream){
            .speed = 2.2 + random_unit() * 5.5,
            .offset = random_unit() * (height + 45),
            .length = 7 + random_unit() * (height * .75 + 5),
            .gap = 4 + random_unit() * 15,
            .phase = (int)(random_unit() * 4),
        };
    }
    invalidate(c);
    fputs("\033[0m\033[49m\033[2J", stdout);
    return true;
}
static Color palette_color(int p, double phase) {
    if (!strcmp(palettes[p].name,"rainbow")) {
        /* A closed spectrum: red -> gold -> green -> cyan -> violet -> red. */
        double hue = mod(phase,1)*6;
        double rising = hue-floor(hue), falling = 1-rising;
        Color spectrum[] = {{1,rising,0}, {falling,1,0}, {0,1,rising},
                            {0,falling,1}, {rising,0,1}, {1,0,falling}};
        Color c = spectrum[(int)hue];
        /* A small white component keeps the fine letterforms readable. */
        return (Color){35+220*c.r,35+220*c.g,35+220*c.b};
    }
    double pos = mod(phase, 1) * 3;
    int i = (int)pos;
    double f = smooth(pos - i);
    Color a = palettes[p].stops[i], b = palettes[p].stops[i + 1];
    return (Color){mix(a.r,b.r,f), mix(a.g,b.g,f), mix(a.b,b.b,f)};
}
static uint32_t color_at(int previous, int current, double fade, double phase,
                         double light, double highlight) {
    Color a = palette_color(previous, phase), b = palette_color(current, phase);
    double r = mix(a.r,b.r,fade), g = mix(a.g,b.g,fade), blue = mix(a.b,b.b,fade);
    r = mix(r,255,.5*highlight);
    g = mix(g,255,.5*highlight);
    blue = mix(blue,255,.5*highlight);
    /* Dimming is a blend into the user's background, not a blend into black.
       If querying isn't supported, keep saturated midtones rather than
       inventing an opaque black shadow on an unknown/transparent background. */
    light = clamp(light,0,1);
    if (background_known) {
        r = mix(terminal_background.r,r,light);
        g = mix(terminal_background.g,g,light);
        blue = mix(terminal_background.b,blue,light);
    } else {
        double strongest = fmax(r,fmax(g,blue));
        if (strongest > 0 && strongest < 128) {
            r *= 128/strongest; g *= 128/strongest; blue *= 128/strongest;
        }
        double intensity = .55+.45*light;
        r *= intensity; g *= intensity; blue *= intensity;
    }
    unsigned rr = (unsigned)clamp(round(r), 0, 255);
    unsigned gg = (unsigned)clamp(round(g), 0, 255);
    unsigned bb = (unsigned)clamp(round(blue), 0, 255);
    return (rr << 16) | (gg << 8) | bb;
}
static void set_color(uint32_t rgb, bool bold, int depth) {
    int r = (int)(rgb >> 16), g = (int)((rgb >> 8) & 255), b = (int)(rgb & 255);
    if (depth == 256) {
        /* Pick the nearer of the xterm cube and its grayscale ramp. */
        static const int level[] = {0,95,135,175,215,255};
        int channels[] = {r,g,b}, indices[3], cube_error = 0;
        for (int c = 0; c < 3; ++c) {
            int best = 0;
            for (int i = 1; i < 6; ++i)
                if (abs(channels[c]-level[i]) < abs(channels[c]-level[best])) best = i;
            indices[c] = best;
            int d = channels[c] - level[best];
            cube_error += d*d;
        }
        int gray = (int)clamp(round(((r+g+b)/3.0 - 8)/10), 0, 23);
        int value = 8 + gray*10;
        int gray_error = (r-value)*(r-value) + (g-value)*(g-value) + (b-value)*(b-value);
        int index = gray_error < cube_error ? 232 + gray :
            16 + indices[0]*36 + indices[1]*6 + indices[2];
        printf("\033[%d;38;5;%dm", bold ? 1 : 22, index);
    } else printf("\033[%d;38;2;%d;%d;%dm", bold ? 1 : 22, r,g,b);
}
/* Words retain their identity as they travel. Only the final projection lands
   on terminal cells; positions and trajectories remain continuous. */
static void word(Canvas *c, const Options *o, double x, double y, int glyph,
                 double light, double t, int from, double fade, double highlight) {
    const int dev_width[] = {1,2,3,1}, ascii_width[] = {2,4,5,3};
    int width = o->ascii ? ascii_width[glyph] : dev_width[glyph];
    int col = (int)lround(x), row = (int)floor(y);
    if (col < 0 || col+width >= c->width || y < -1 || y >= c->height) return;
    /* Fade at the edges instead of popping when a path wraps offscreen. */
    light *= smooth(x/4)*smooth((c->width-width-1-x)/4);
    double phase = .56 + .09*sin(x*3/fmax(c->width,1) + y*2.4/c->height - t*.35);
    if (!o->palette_auto && !strcmp(palettes[o->palette].name,"rainbow"))
        phase = x/fmax(c->width,1) + y*.3/c->height - t*.125;
    double fraction = y-floor(y);
    for (int dy = 0; dy < 2; ++dy) {
        int r = row+dy;
        if (r < 0 || r >= c->height) continue;
        double weight = pow(dy ? fraction : 1-fraction,.7);
        double intensity = light*weight;
        if (intensity < .025) continue;
        Tile *slot = &c->frame[r*c->width+col];
        bool free_space = true;
        for (int k = 0; k < width; ++k)
            if (slot[k].glyph != -1) free_space = false;
        if (!free_space) continue;
        slot[0] = (Tile){.rgb=color_at(from,o->palette,fade,phase,intensity,highlight),.glyph=glyph};
        for (int k = 1; k < width; ++k) slot[k].glyph = -3;
    }
}
static bool included(int a, int b, double density) {
    unsigned hash = (unsigned)a*374761393u + (unsigned)b*668265263u;
    hash = (hash ^ (hash >> 13))*1274126177u;
    return (hash & 65535)/65536.0 <= density;
}
static double bank_wave(double x, double t, double k, int edge) {
    double phase = edge*1.9;
    double swell = k*2*x-t*.75+phase;
    return 1.05*sin(swell+.35*sin(k*x+t*.19))
         + .42*sin(k*4*x-t*1.3+phase+.8)
         + .18*sin(k*7*x-t*1.8+phase);
}
static Flow river_current(const Canvas *c, int lane, int j, int count, double t) {
    double surface_time = t;
    /* Let the body of the river drift at half its previous pace. Surface
       ripples have their own clock, so slowing the current doesn't freeze them. */
    t *= .5;
    double extent = c->width+32.0, base_y = (lane-3)*2.5;
    double across_river = (base_y-c->height*.5)/fmax(c->height,1);
    double phase = lane*2.399963;
    /* The middle runs faster than the banks. Small, integrated changes in
       speed accelerate gently and always keep the current moving downstream. */
    double speed = 14+7*exp(-4*across_river*across_river)+1.5*sin(phase);
    double travel = t*speed + 1.3*sin(t*.65+phase)+.7*sin(t*1.17+phase*.7);
    /* Keep material coordinates unwrapped, including during a morph. All
       surface waves are periodic over the viewport's offscreen margin. */
    double x = j*extent/count+travel+2.5*sin(lane*1.8)-16;
    double k = 2*PI/extent;
    double y = base_y + 2.4*sin(k*(x-7*t)+.3*sin(t*.3))
             + .8*sin(k*2*(x-11*t)+base_y*.09)
             + .45*sin(k*3*x-t*1.3+lane*.28);
    double bank_width = fmax(3,c->height*.14);
    double top = exp(-pow(base_y/bank_width,2));
    double bottom = exp(-pow((base_y-c->height+1)/bank_width,2));
    y += top*bank_wave(x,surface_time,k,0) + bottom*bank_wave(x,surface_time,k,1);
    x += .7*sin(base_y*.4+t*.65+k*x);
    /* Broad, slowly breathing eddies bend whole neighboring strands together.
       Periodic distance keeps both sides of the offscreen wrap identical. */
    double curl_light = 0;
    for (int eddy = 0; eddy < 2; ++eddy) {
        double center_x = extent*(.28+.46*eddy)+surface_time*2.1;
        double center_y = c->height*(.5+.22*sin(surface_time*.12+eddy*PI));
        double theta = k*(x-center_x);
        double dx = sin(theta)/(2*k), dy = y-center_y;
        double radius = fmax(4,fmin(c->height*.32,extent*.12));
        double periodic_dx = sin(theta*.5)/k;
        double envelope = exp(-(periodic_dx*periodic_dx+dy*dy)/(radius*radius));
        double twist = (eddy ? -1 : 1)*1.1*envelope
                     *(.8+.2*sin(surface_time*.18+eddy));
        double cs = cos(twist), sn = sin(twist);
        x += 2*(dx*(cs-1)-dy*sn);
        y += dx*sn+dy*(cs-1);
        curl_light += envelope*sin(theta*2+surface_time*.35+eddy);
    }
    double ripple = 0;
    int newest = (int)floor(t/5.5);
    for (int n = 0; n < 3; ++n) {
        int event = newest-n;
        double age = t-event*5.5;
        if (age >= 12) continue;
        /* Ripples expand from localized disturbances and drift with the water,
           rather than pushing the entire screen up and down in lockstep. */
        double source_x = c->width*(.16+.12*sin(event*2.17))+age*9;
        double source_y = c->height*(.5+.27*sin(event*1.73+.8));
        double visible_x = mod(x+16,extent)-16;
        double dx = (visible_x-source_x)*.5, dy = y-source_y;
        double distance = hypot(dx,dy), front = distance-age*2.6;
        double envelope = exp(-front*front/26-age*.14)
                        *smooth(age/.7)*smooth((12-age)/2);
        double edge = smooth((visible_x+16)/8)*smooth((extent-16-visible_x)/8);
        double displacement = sin(front*1.35)*envelope*edge;
        x += 1.1*dx/(distance+1)*displacement;
        y += .85*dy/(distance+1)*displacement;
        ripple += displacement;
    }
    /* Intersecting wavefronts make soft caustics: light slides over the water
       independently of the current instead of blinking on individual words. */
    double caustic = sin(k*3*x+base_y*.23-surface_time*.48
                        +.65*sin(k*x+surface_time*.17))
                   *cos(k*2*x-base_y*.31-surface_time*.32);
    double light = clamp(.60+.13*sin(k*x-t*.8+lane*.32)
                         +.15*caustic+.09*curl_light+.12*ripple,.25,.98);
    return (Flow){x,y,light};
}
static Flow project_petals(const Canvas *c, double center_x, double center_y,
                           double radius, double angle, double height,
                           double tilt, double turn, double light) {
    /* Character cells are roughly twice as tall as wide. Project a shallow
       sculpture in that space; front/back shading gives the folds depth. */
    double x = cos(angle)*radius, y = sin(angle)*radius;
    double tilted_y = y*cos(tilt)-height*sin(tilt);
    double depth = y*sin(tilt)+height*cos(tilt);
    double perspective = 1/(1+.28*tanh(depth));
    double screen_x = x*cos(turn)-tilted_y*sin(turn);
    double screen_y = x*sin(turn)+tilted_y*cos(turn);
    return (Flow){center_x+screen_x*c->height*2*perspective,
                  center_y+screen_y*c->height*perspective,
                  clamp(light-.17*tanh(depth*2),.25,.98)};
}
static Flow incense_drift(const Canvas *c, int id, double t) {
    int column = id%c->particle_columns, row = id/c->particle_columns;
    int rows = c->particle_count/c->particle_columns;
    double extent = c->width+32.0, k = 2*PI/extent;
    double phase = row*.71;
    /* Integrated gusts keep every word moving left, even as adjacent ribbons
       stretch and braid. Coordinates remain unwrapped through every morph. */
    double speed = 6.5+2.2*sin(phase);
    double x = column*extent/c->particle_columns-16-t*speed
             + 2*sin(t*.38+phase)+.6*sin(t*.9+phase*.4);
    double ribbon = k*2*x+t*.24+.75*sin(k*x-t*.16)+row*.26;
    double curl = fmin(4,c->height*.10);
    double y = row*(c->height+24.0)/rows-12
             + curl*sin(ribbon)+curl*.32*sin(k*4*x+t*.3+phase);
    double light = .57+.17*sin(ribbon+.8)+.12*cos(k*3*x+t*.25-phase*.3);
    return (Flow){x,y,clamp(light,.25,.98)};
}
static Flow rising_lanterns(const Canvas *c, int id, double t) {
    int column = id%c->particle_columns, row = id/c->particle_columns;
    int rows = c->particle_count/c->particle_columns;
    double extent = c->height+24.0, k = 2*PI/extent;
    double phase = column*2.399963;
    double speed = 2.6+1.6*variation(0,(unsigned)column+700);
    /* Gentle lift never reverses. Broad lateral sways make loose processions
       of lanterns, with light breathing through neighboring clusters. */
    double y = row*extent/rows-12-t*speed+1.1*sin(column+row*.7)
             + .6*sin(t*.5+phase)+.2*sin(t*.9+phase*.7);
    double sway = fmin(6,c->width*.06);
    double x = column*(c->width+32.0)/c->particle_columns-16
             + sway*sin(k*y+t*.25+phase)
             + sway*.35*sin(k*2*y-t*.17+phase*.6);
    double glow = .5+.5*sin(t*.5+row*.7+column*1.4);
    double light = .5+.22*glow+.13*sin(k*y+phase);
    return (Flow){x,y,clamp(light,.25,.98)};
}
static Flow turning_mandala(const Canvas *c, int id, double t) {
    int rings = (int)clamp(fmin(c->width/14.0,c->height/6.0),3,12);
    int ring = (int)floor(sqrt((id+.5)/c->particle_count)*rings);
    if (ring >= rings) ring = rings-1;
    double lo = ring/(double)rings, hi = (ring+1.0)/rings;
    int first = (int)ceil(c->particle_count*lo*lo-.5);
    int last = (int)ceil(c->particle_count*hi*hi-.5);
    int count = last-first;
    if (count < 1) count = 1;
    double base = (ring+.6)/rings;
    /* Counter-turning prayer wheels periodically align eight luminous gates.
       Stable ring membership avoids the center resets of a radial emitter. */
    double angle = (id-first)*2*PI/count+ring*.36
                 + t*(ring%2 ? -1 : 1)*(.045+.012*(ring%3));
    double gate = 8*angle-t*.12;
    double radius = base*(1+.055*cos(gate+ring*.28))
                  *(1+.04*sin(t*.4-base*5));
    double rx = fmax(1,(c->width-14)*.5), ry = fmax(1,(c->height-4)*.5);
    double light = .53+.26*pow(.5+.5*cos(gate),4)+.12*sin(t*.4-base*5);
    return (Flow){c->width*.5+rx*radius*cos(angle),
                  c->height*.5+ry*radius*sin(angle),clamp(light,.25,.98)};
}
static Flow rippling_flag(const Canvas *c, int id, double t) {
    int column = id%c->particle_columns, row = id/c->particle_columns;
    int rows = c->particle_count/c->particle_columns;
    double u = column/(double)c->particle_columns;
    double v = row/(double)rows;
    /* Tile the whole viewport, including offscreen margins. Shared traveling
       folds ripple everywhere without an anchored edge or a fabric outline.
       Spatial phases repeat at the wrap boundaries to keep the sheet seamless. */
    double phase = 2*PI*(2*u+v)-t*1.65;
    double flutter = 2*PI*(4*u-v)-t*2.7;
    double gust = .85+.15*sin(t*.37);
    double fold = sin(phase)+.22*sin(flutter);
    double x = (c->width+32.0)*u-16
             + fmin(3,c->width*.02)*gust*cos(phase);
    double y = (c->height+24.0)*v-12
             + fmin(4,c->height*.095)*gust*fold;
    /* Moving highlights follow the fabric's slope, rather than individual words. */
    double slope = gust*(cos(phase)+.51*cos(flutter));
    double light = clamp(.65+.23*slope-.08*gust*fold,.25,.98);
    return (Flow){x,y,light};
}
static Flow lava_lamp(const Canvas *c, int id, double t) {
    double width = c->width+32.0, height = c->height+24.0;
    if (id%2 == 0) {
        /* Keep a circulating field behind the bubbles across the entire screen. */
        int index = id/2, column = index%c->particle_columns;
        int rows = ((c->particle_count+1)/2+c->particle_columns-1)/c->particle_columns;
        double u = column/(double)c->particle_columns;
        double y = (index/c->particle_columns)*height/rows-12-t*.65;
        double phase = 2*PI*y/height;
        double x = width*u-16+3*sin(phase+t*.23+2*PI*u);
        y += 1.2*sin(2*PI*u+t*.32);
        return (Flow){x,y,.36+.10*sin(phase+t*.4+4*PI*u)};
    }
    int bubbles = (int)clamp(c->width/20,4,12);
    int index = id/2, bubble = index%bubbles, point = index/bubbles;
    int total = c->particle_count/2;
    int count = (total-1-bubble)/bubbles+1;
    unsigned key = 900000+(unsigned)bubble*7;
    double phase = variation(0,key)*2*PI;
    double speed = 1.2+1.6*variation(0,key+1);
    double center_y = height*(.1+variation(0,key+2))-t*speed;
    double travel = mod((c->height+12-center_y)/height,1);
    /* Birth and dissolution happen outside the viewport. Unwrapped centers
       preserve continuous trajectories when a bubble returns from the top. */
    double growth = smooth(travel/.18)*smooth((1-travel)/.18);
    double radius = fmin(c->height*.19,c->width*.105)
                  *(.45+1.05*variation(0,key+3))*growth;
    radius *= .90+.10*sin(t*.31+phase);
    double center_x = c->width*(.08+.84*variation(0,key+4))
                    + fmin(6,c->width*.045)*sin(t*.27+phase);
    double angle = point*2.399963+t*.16;
    double fraction = sqrt((point+.5)/count);
    double shape = 1+.11*sin(3*angle+t*.7+phase)
                    +.07*cos(2*angle-t*.48);
    double x = center_x+2*radius*fraction*cos(angle)*shape;
    double y = center_y+radius*fraction*sin(angle)*shape;
    double light = .55+.22*(1-fraction*fraction)
                   +.12*cos(angle+.8)+.07*sin(t*.6+phase);
    return (Flow){x,y,clamp(mix(.25,light,growth),.25,.98)};
}
static Flow spiral_galaxy(const Canvas *c, int id, double t) {
    const int arms = 6;
    int arm = id%arms;
    double phase = mod((id+.5)/c->particle_count+t/48,1);
    /* Infall occupies most of the orbit. Recycle invisibly along a continuous
       return path, so morphs can still preserve every particle's trajectory. */
    double radius = phase < .84 ? 1.1-1.02*smoother(phase/.84)
                               : .08+1.02*smoother((phase-.84)/.16);
    double scatter = (variation(0,(unsigned)id+950000)-.5)*.23;
    double angle = arm*2*PI/arms+5*radius+2*log(radius+.08)-t*.22+scatter;
    double light = (.52+.17*sin(radius*11-t*.6)+.22*exp(-pow((radius-.22)/.1,2)))
                 *smooth(phase/.04)*smooth((.82-phase)/.09);
    return (Flow){c->width*(.5+.46*radius*cos(angle)),
                  c->height*(.5+.44*radius*sin(angle)),clamp(light,0,.98)};
}
static Flow whirlpools(const Canvas *c, int id, double t) {
    int vortex = id%3, index = id/3;
    int count = (c->particle_count-1-vortex)/3+1;
    double fraction = (index+.5)/count;
    double phase = vortex*2*PI/3;
    double radius = sqrt(fraction)*(.78+.08*sin(t*.22+phase));
    double angle = index*2.399963 + radius*7
                 + t*(vortex == 1 ? -.38 : .38)/(radius+.45);
    double cx = c->width*(.17+.33*vortex+.012*sin(t*.16+phase));
    double cy = c->height*(.5+.14*sin(t*.19+phase));
    double rx = fmax(1,c->width*.16), ry = fmax(1,c->height*.32);
    double light = .55+.18*cos(angle*3-radius*9+t*.4)+.12*(1-radius);
    return (Flow){cx+rx*radius*cos(angle), cy+ry*radius*sin(angle),
                  clamp(light,.25,.98)};
}
/* Every formation describes the same pool of words. No particles are created,
   deleted, or reassigned when the geometry changes. */
static Flow formation(const Canvas *c, int mode, int id, double t) {
    if (mode == 4) return incense_drift(c,id,t);
    if (mode == 5) return rising_lanterns(c,id,t);
    if (mode == 6) return turning_mandala(c,id,t);
    if (mode == 7) return whirlpools(c,id,t);
    if (mode == 8) return rippling_flag(c,id,t);
    if (mode == 9) return lava_lamp(c,id,t);
    if (mode == 10) return spiral_galaxy(c,id,t);
    int column = id%c->particle_columns, row = id/c->particle_columns;
    double extent = c->width+32.0;
    if (mode == 0) {
        const Stream *stream = &c->streams[column*c->columns/c->particle_columns];
        double y = row*2.5+t*stream->speed+stream->offset;
        double phase = y*2*PI/(c->height+24.0);
        /* Neighboring curtains share a slow breeze. Keep height unwrapped so
           falling words can bend continuously into the next river current. */
        double across = column*2*PI/c->particle_columns;
        double breeze = 2.4*sin(t*.23+across*.5)
                      + 1.2*sin(phase-t*.31+across);
        double light = .25+.65*pow(.5+.5*cos(phase-column*.7-t*.35),1.5);
        return (Flow){column*extent/c->particle_columns-16+breeze,y,light};
    }
    if (mode == 1) {
        if (row < 2 && c->height >= 8) {
            double x = column*extent/c->particle_columns+t*5+row*4-16;
            double wave = bank_wave(x,t,2*PI/extent,row);
            return (Flow){x,(row ? c->height-2.0 : 1.0)+wave,.5+.12*wave};
        }
        return river_current(c,row-2,column,c->particle_columns,t);
    }
    double scale = c->height*2.0;
    double radius_max = hypot(c->width/(2*scale),.5)+.16;
    double center_x = c->width*(.5+.045*sin(t*.27));
    double center_y = c->height*(.5+.045*cos(t*.31));
    double radius, angle, light, height, tilt, turn;
    if (mode == 2) {
        int rings = (int)ceil(radius_max*c->height/3);
        int ring = (int)floor(sqrt((id+.5)/c->particle_count)*rings);
        if (ring >= rings) ring = rings-1;
        double lo = ring/(double)rings, hi = (ring+1.0)/rings;
        int first = (int)ceil(c->particle_count*lo*lo-.5);
        int last = (int)ceil(c->particle_count*hi*hi-.5);
        int count = last-first;
        if (count < 1) count = 1;
        double base = (ring+.6)*radius_max/rings;
        double layer = ring*.72;
        angle = (id-first)*2*PI/count+t*(ring%2 ? -.18 : .18)/(base+.5);
        double petal = 6*angle+layer+t*.22;
        radius = base*(1+.14*sin(petal)+.055*sin(3*angle-layer-t*.17));
        height = base*(.24*cos(petal)+.12*sin(3*angle+t*.24));
        tilt = .48+.20*sin(t*.11);
        turn = .16*sin(t*.09);
        light = .64+.17*sin(petal+.8)+.09*cos(2*angle-t*.3);
    } else {
        int arms = (int)clamp(c->width/10,8,24);
        int arm = id%arms, j = id/arms;
        int count = (c->particle_count-1-arm)/arms+1;
        /* Continuous breathing replaces radial wrapping, which would teleport
           a word from an outer spiral to the center halfway through a morph. */
        double phase = j/(double)count+arm/(double)(arms*count)+t*.012;
        radius = radius_max*(.12+.88*(.5-.5*cos(phase*2*PI)));
        double strand = arm*2*PI/arms;
        double braid = radius*9-t*.42+strand*3;
        angle = strand+radius*3.8-t*.20+.19*sin(braid);
        radius *= 1+.065*cos(braid+PI*.5);
        height = radius*(.22*cos(braid)+.13*sin(2*angle+t*.18));
        tilt = .38*sin(t*.12)+.18;
        turn = .12*cos(t*.10);
        light = .63+.18*cos(braid-.7)+.10*sin(angle*2-radius*5+t*.3);
    }
    return project_petals(c,center_x,center_y,radius,angle,height,tilt,turn,light);
}
static Flow motion_position(const Canvas *c, int id, double t, double timeline) {
    const Motion *m = &c->motion;
    const Particle *p = &c->particles[id];
    Flow a;
    if (m->linear_source) {
        double age = timeline-m->changed;
        a = (Flow){p->origin.x+p->velocity.x*age,
                   p->origin.y+p->velocity.y*age,p->origin.light};
    } else {
        a = formation(c,m->from,id,t);
        a.x += p->from_offset.x; a.y += p->from_offset.y;
    }
    Flow b = formation(c,m->to,id,t);
    b.x += p->to_offset.x; b.y += p->to_offset.y;
    double w = smoother((timeline-m->changed)/m->duration);
    return (Flow){mix(a.x,b.x,w),mix(a.y,b.y,w),mix(a.light,b.light,w)};
}
static int next_mode(int mode) { return (mode+1)%MODE_COUNT; }
static void start_motion(Canvas *c, int to, double t, double timeline,
                         double duration, double speed) {
    Motion old = c->motion;
    bool interrupted = (old.from != old.to || old.linear_source)
                       && timeline < old.changed+old.duration-1e-6;
    for (int id = 0; id < c->particle_count; ++id) {
        Flow current = motion_position(c,id,t,timeline);
        /* Preserve the tangent if a key changes destination mid-morph. */
        Flow before = motion_position(c,id,t-.0001*speed,timeline-.0001);
        Flow after = motion_position(c,id,t+.0001*speed,timeline+.0001);
        Flow source = formation(c,old.to,id,t), target = formation(c,to,id,t);
        Particle *p = &c->particles[id];
        p->origin = current;
        p->velocity = (Flow){(after.x-before.x)/.0002,(after.y-before.y)/.0002,0};
        p->from_offset = (Flow){current.x-source.x,current.y-source.y,0};
        /* Equivalent offscreen copies keep the destination near the source.
           These offsets stay fixed throughout the morph; no shortest-path flip. */
        double width = c->width+32.0, height = c->height+24.0;
        p->to_offset = (Flow){round((current.x-target.x)/width)*width,
                             round((current.y-target.y)/height)*height,0};
    }
    c->motion = (Motion){old.to,to,timeline,duration,interrupted};
}
static void reset_motion(Canvas *c, const Options *o, double t, double timeline) {
    c->motion = (Motion){o->mode,o->mode,timeline,1,false};
    if (o->mode_auto) start_motion(c,next_mode(o->mode),t,timeline,o->scene_interval,o->speed);
}
/* Rasterized once with Indic shaping, then sampled into particle destinations
   and filled terminal strokes; no font library or image protocol at runtime. */
static bool mask_ink(const MantraMask *mask, int x, int y) {
    int bit = y*mask->width+x;
    return (mask->bits[bit/8] & (1u << (bit%8))) != 0;
}
/* Terminal fonts have one cell size. Larger words use the same shaped outlines
   as the full mantra, sampled into braille cells at a continuously varying size. */
static double word_size(unsigned seed, int id, double t, double gather) {
    unsigned key = (unsigned)id*4+10000;
    if (variation(seed,key) > .2) return 1;
    double phase = variation(seed,key+1)*2*PI;
    double pulse = smooth((sin(t*(.35+.3*variation(seed,key+2))+phase)+.15)/1.15);
    double step = t/5+variation(seed,key+3)*10;
    unsigned epoch = (unsigned)floor(step);
    double drift = mix(variation(seed,key+epoch+100000),
                       variation(seed,key+epoch+100001),smoother(step-floor(step)));
    return 1+(2.1+1.8*drift)*pulse*(1-gather);
}
static void large_word(Canvas *c, const Options *o, double x, double y, int glyph,
                       double size, double light, double t, int from, double fade) {
    static const MantraMask *dev[] = {&mantra_word_0,&mantra_word_1,&mantra_word_2,&mantra_word_3};
    static const MantraMask *ascii[] = {&mantra_latin_0,&mantra_latin_1,&mantra_latin_2,&mantra_latin_3};
    const MantraMask *mask = o->ascii ? ascii[glyph] : dev[glyph];
    double alpha = smooth((size-1)/1.5);
    if (alpha < .02 || light*alpha < .025) return;
    double height = size, width = height*2*mask->width/mask->height;
    double left = x-width*.5, top = y-height*.5;
    light *= alpha*smooth((left+width)/4)*smooth((c->width-left)/4);
    double phase = .56+.09*sin(x*3/c->width+y*2.4/c->height-t*.35);
    if (!o->palette_auto && !strcmp(palettes[o->palette].name,"rainbow"))
        phase = x/c->width+y*.3/c->height-t*.125;
    uint32_t rgb = color_at(from,o->palette,fade,phase,light,0);
    const int dots[4][2] = {{0,3},{1,4},{2,5},{6,7}};
    for (int row = (int)fmax(0,floor(top)); row < fmin(c->height,top+height); ++row) {
        for (int col = (int)fmax(0,floor(left)); col < fmin(c->width-1,left+width); ++col) {
            Tile *tile = &c->frame[row*c->width+col];
            if (tile->glyph != -1) continue;
            unsigned pattern = 0;
            for (int dy = 0; dy < 4; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    int mx = (int)floor((col-left+(dx+.5)/2)*mask->width/width);
                    int my = (int)floor((row-top+(dy+.5)/4)*mask->height/height);
                    if (mx >= 0 && mx < mask->width && my >= 0 && my < mask->height
                        && mask_ink(mask,mx,my)) pattern |= 1u << dots[dy][dx];
                }
            }
            if (pattern) *tile = (Tile){.glyph=4+(int)pattern,.rgb=rgb};
        }
    }
}
static void prepare_mantra(Canvas *c, bool ascii) {
    c->mantra_prepared = true;
    free(c->mantra_points);
    c->mantra_points = NULL;
    c->mantra_point_count = 0;
    if (c->width < 64 || c->height < 20 || c->particle_count < 1) return;
    /* Preserve the shaped font's aspect ratio (cells are twice as tall as wide).
       A ticker may also use two lines when a single line would lose its marks. */
    double scale = c->gathering.scale;
    double line_height = fmin((c->width-12)*mantra_line.height/(2.0*mantra_line.width),
                              c->height-6)*scale;
    bool single = (c->gathering.style == 2 || c->width >= c->height*6) && line_height >= 9;
    const MantraMask *mask = single ? &mantra_line : &mantra_stack;
    double maximum_height = fmin((c->width-12)*mask->height/(2.0*mask->width),c->height-6);
    double height = clamp(maximum_height*scale,fmin(maximum_height,single ? 9 : 18),maximum_height);
    double width = height*2*mask->width/mask->height;
    double left = (c->width-width)/2, top = (c->height-height)/2;
    int available = (c->particle_count+1)/2;
    Flow *points = calloc((size_t)available,sizeof *points);
    if (!points) return;
    double spacing = ascii ? 6 : 3;
    int count = 0;
    for (int attempt = 0; attempt < 12; ++attempt) {
        int columns = (int)fmax(1,floor(width/spacing));
        int rows = (int)fmax(1,floor(height));
        count = 0;
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < columns; ++x) {
                int x0 = x*mask->width/columns, x1 = (x+1)*mask->width/columns;
                int y0 = y*mask->height/rows, y1 = (y+1)*mask->height/rows;
                int ink = 0, area = (x1-x0)*(y1-y0);
                for (int yy = y0; yy < y1; ++yy)
                    for (int xx = x0; xx < x1; ++xx) ink += mask_ink(mask,xx,yy);
                if (!area || !ink) continue;
                if (count < available)
                    points[count] = (Flow){left+x*width/columns,top+y*height/rows,.9};
                ++count;
            }
        }
        if (count <= available) {
            c->mantra_patch_width = width/columns;
            c->mantra_patch_height = height/rows;
            break;
        }
        /* Reduce particle sampling, never shrink the readable outline. */
        spacing *= 1.25;
    }
    if (count < 1 || count > available) { free(points); return; }
    c->mantra_points = points;
    c->mantra_point_count = count;
    c->mantra_mask = mask;
    c->mantra_left = left; c->mantra_top = top;
    c->mantra_width = width; c->mantra_height = height;
}
static void choose_gathering(Canvas *c, const Options *o, int event) {
    unsigned seed = o->seed ^ ((unsigned)event*0x85ebca6bu);
    Gathering g = {.seed=seed};
    g.style = (int)(variation(seed,1)*5); /* hover, bounce, ticker, inflate, big bang */
    g.path = (int)(variation(seed,2)*3);  /* curls, waves, scattered arcs */
    g.gather = 4.5+variation(seed,3)*2.5;
    g.release = 4.5+variation(seed,4)*2.5;
    g.hold = g.style ? 8+variation(seed,5)*5 : 3+variation(seed,5)*5;
    /* Even the shortest interval leaves an ambient gap before the next event. */
    g.hold = fmin(g.hold,o->mantra_interval*.75-g.gather-g.release-.5);
    g.phase = variation(seed,6)*2*PI;
    g.vx = (variation(seed,7) < .5 ? -1 : 1)*(5+variation(seed,8)*5);
    g.vy = (variation(seed,9) < .5 ? -1 : 1)*(1.5+variation(seed,10)*2);
    g.scale = g.style == 0 ? .84+variation(seed,11)*.12 : .68+variation(seed,11)*.14;
    c->gathering = g;
    prepare_mantra(c,o->ascii);
    c->mantra_event = event;
    c->mantra_stage = 0;
}
/* Integrated smoothstep: accelerate and brake the block over one second. */
static double travel_ramp(double t) {
    if (t <= 0) return 0;
    if (t >= 1) return t-.5;
    return t*t*t-.5*t*t*t*t;
}
static double reflected(double value, double span) {
    if (span <= 0) return 0;
    return span-fabs(mod(value,2*span)-span);
}
static MantraPose mantra_pose(const Canvas *c, double age) {
    const Gathering *g = &c->gathering;
    double held = clamp(age-g->gather,0,g->hold);
    double travel = travel_ramp(held)-travel_ramp(held-g->hold+1);
    double breathe = smoother(age/g->gather)*smoother((g->gather+g->hold-age)/1.5);
    double scale = 1-.08*breathe*(.5+.5*sin(age*1.3+g->phase));
    if (g->style == 3) {
        /* Grow beyond the viewport, then breathe back to the original pose
           before the words disperse. Smooth endpoints avoid a reset jump. */
        double u = held/fmax(g->hold,.001);
        double swell = smoother(u/.65)*smoother((1-u)/.35);
        double full = fmax(c->width/c->mantra_width,c->height/c->mantra_height)*1.5;
        scale = 1+(full-1)*swell;
        return (MantraPose){(c->width-c->mantra_width*scale)*.5,
                            (c->height-c->mantra_height*scale)*.5,scale};
    }
    if (g->style == 4) {
        double u = held/fmax(g->hold,.001);
        double collapse = smoother(u/.45);
        double burst = smoother((u-.45)/.12);
        double reset = smoother((u-.68)/.32);
        double full = fmax(c->width/c->mantra_width,c->height/c->mantra_height)*1.5;
        scale = mix(mix(1,.025,collapse),full,burst);
        scale = mix(scale,1,reset);
        return (MantraPose){(c->width-c->mantra_width*scale)*.5,
                            (c->height-c->mantra_height*scale)*.5,scale};
    }
    /* Fixed travel bounds accommodate the largest pulse and complete words. */
    double sx = fmax(0,c->width-c->mantra_width-8);
    double sy = fmax(0,c->height-c->mantra_height-3);
    double x = sx*.5, y = sy*.5;
    if (g->style == 1) {
        x = reflected(sx*.5+travel*g->vx,sx);
        y = reflected(sy*.5+travel*g->vy,sy);
    } else if (g->style == 2) {
        /* One readable, horizontal pass, with a randomly chosen direction. */
        double progress = travel/fmax(1,g->hold-1);
        x = sx*(g->vx > 0 ? progress : 1-progress);
        y = sy*(.35+.3*variation(g->seed,12));
    } else {
        x += sx*.32*sin(travel*.6+g->phase)*sin(travel*.3);
        y += sy*.32*sin(travel*.5)*cos(travel*.3+g->phase);
    }
    return (MantraPose){4+x+c->mantra_width*(1-scale)*.5,
                        1+y+c->mantra_height*(1-scale)*.5,scale};
}
static Flow posed_target(const Canvas *c, Flow target, MantraPose pose) {
    target.x = pose.left+(target.x-c->mantra_left)*pose.scale;
    target.y = pose.top+(target.y-c->mantra_top)*pose.scale;
    return target;
}
static double gathering_weight(const Gathering *g, double age) {
    double settled = g->gather+g->hold;
    if (age < 0 || age >= settled+g->release) return 0;
    if (age < g->gather) return smoother(age/g->gather);
    if (age < settled) return 1;
    return 1-smoother((age-settled)/g->release);
}
/* Keep half the same word identities outside the large letters. Their target
   is a live falling stream, so the assembly/release easing matches its velocity
   as well as its position. The animation clock keeps running while hidden. */
static bool background_particle(int id) { return (id & 1) != 0; }
static Flow matrix_position(const Canvas *c, int id, double t) {
    int index = id/2, column = index%c->columns, row = index/c->columns;
    int count = (c->particle_count/2-1-column)/c->columns+1;
    unsigned seed = c->gathering.seed;
    unsigned key = (unsigned)column*3+5000;
    double speed = 3.5+4*variation(seed,key);
    double offset = variation(seed,key+1)*(c->height+24.0);
    /* Pack each trail into a column with a gap behind its dim tail. Each word
       wraps only in the offscreen margin, preserving its identity and speed. */
    double spacing = fmin(1.6+.6*variation(seed,key+2),(c->height+24.0)/fmax(count,1));
    double tail = row/fmax(count-1,1);
    double light = .18+.65*pow(1-tail,1.4);
    return (Flow){column*SLOT+1,
                  t*speed+offset-row*spacing,light};
}
static Flow gathering_target(const Canvas *c, int id, double t, double age) {
    const Particle *p = &c->particles[id];
    if (!background_particle(id)) return posed_target(c,p->gather_target,mantra_pose(c,age));
    Flow target = matrix_position(c,id,t);
    target.x += p->gather_target.x;
    target.y += p->gather_target.y;
    return target;
}
/* Feather the background around the whole block, including word footprints.
   The clearance moves and pulses with the letters; no hard rectangular cutout. */
static double matrix_clearance(const Canvas *c, double x, double y, double gather) {
    MantraPose pose = c->mantra_pose;
    double right = pose.left+c->mantra_width*pose.scale;
    double bottom = pose.top+c->mantra_height*pose.scale;
    double dx = fmax(pose.left-5-x,x-right-1)/4;
    double dy = fmax(pose.top-2-y,y-bottom-1)/2.5;
    double outside = smoother(fmax(dx,dy));
    return mix(1,outside,smoother(gather));
}
static double update_gathering(Canvas *c, const Options *o, double t, double timeline) {
    if (o->no_mantra || o->mantra_interval < 32) { c->mantra_stage = 0; return 0; }
    int event = (int)floor(timeline/o->mantra_interval);
    if (!c->mantra_prepared || c->mantra_event != event) choose_gathering(c,o,event);
    if (!c->mantra_point_count) return 0;
    const Gathering *g = &c->gathering;
    double start = event*o->mantra_interval+o->mantra_interval*.25;
    double age = timeline-start, settled = g->gather+g->hold;
    int stage = age < 0 || age >= settled+g->release ? 0 :
                age < g->gather ? 1 : age < settled ? 2 : 3;
    if (!stage) { c->mantra_stage = 0; return 0; }
    if (c->mantra_stage == 0) {
        /* Match the ambient tangent at the start, even after a late frame. */
        double started_t = t-(timeline-start)*o->speed;
        for (int id = 0; id < c->particle_count; ++id) {
            Flow p = motion_position(c,id,started_t,start);
            Flow a = motion_position(c,id,started_t-.0001*o->speed,start-.0001);
            Flow b = motion_position(c,id,started_t+.0001*o->speed,start+.0001);
            Particle *particle = &c->particles[id];
            particle->gather_origin = (Flow){mod(p.x+16,c->width+32.0)-16,
                                            mod(p.y+12,c->height+24.0)-12,p.light};
            particle->gather_velocity = (Flow){(b.x-a.x)/.0002,(b.y-a.y)/.0002,0};
            if (background_particle(id)) {
                Flow rain = matrix_position(c,id,started_t);
                particle->gather_target = (Flow){
                    round((particle->gather_origin.x-rain.x)/(c->width+32.0))*(c->width+32.0),
                    round((particle->gather_origin.y-rain.y)/(c->height+24.0))*(c->height+24.0),0};
            } else {
                int point = (int)((long long)(id/2)*c->mantra_point_count/((c->particle_count+1)/2));
                particle->gather_target = c->mantra_points[point];
            }
        }
        c->mantra_stage = 1;
    }
    if (stage == 3 && c->mantra_stage != 3) {
        double released = start+settled;
        double released_t = t-(timeline-released)*o->speed;
        for (int id = 0; id < c->particle_count; ++id) {
            Flow p = motion_position(c,id,released_t,released);
            Particle *particle = &c->particles[id];
            Flow target = gathering_target(c,id,released_t,settled);
            double width = c->width+32.0, height = c->height+24.0;
            particle->release_offset = (Flow){
                round((target.x-p.x)/width)*width,
                round((target.y-p.y)/height)*height,0};
        }
    }
    c->mantra_pose = mantra_pose(c,age);
    c->mantra_stage = stage;
    return gathering_weight(g,age);
}
static Flow gathering_position(const Canvas *c, const Options *o, int id,
                               double t, double timeline) {
    Flow ambient = motion_position(c,id,t,timeline);
    if (!c->mantra_stage) return ambient;
    const Particle *p = &c->particles[id];
    const Gathering *g = &c->gathering;
    double start = c->mantra_event*o->mantra_interval+o->mantra_interval*.25;
    double age = timeline-start, settled = g->gather+g->hold;
    Flow target = gathering_target(c,id,t,age);
    if (age >= g->gather && age < settled) return target;
    bool assembling = age < g->gather;
    double u = clamp(assembling ? age/g->gather : (age-settled)/g->release,0,1);
    double phase = variation(g->seed,(unsigned)id+20)*2*PI;
    /* Different words lead/lag the wave without changing endpoint tangents. */
    double progress = u+.12*sin(phase+g->path)*pow(sin(PI*u),2);
    double w = smoother(progress);
    Flow result;
    if (assembling) {
        double tangent = u-6*u*u*u+8*u*u*u*u-3*u*u*u*u*u;
        result = (Flow){mix(p->gather_origin.x,target.x,w)+p->gather_velocity.x*g->gather*tangent,
                        mix(p->gather_origin.y,target.y,w)+p->gather_velocity.y*g->gather*tangent,
                        mix(p->gather_origin.light,target.light,w)};
    } else {
        ambient.x += p->release_offset.x; ambient.y += p->release_offset.y;
        result = (Flow){mix(target.x,ambient.x,w),mix(target.y,ambient.y,w),
                        mix(target.light,ambient.light,w)};
    }
    double envelope = 64*pow(u*(1-u),3);
    double radius = fmin(c->height*.16,5)*envelope;
    double angle = phase+(assembling ? 1 : -1)*u*2*PI;
    if (g->path == 0) {
        result.x += 2*radius*cos(angle); result.y += radius*sin(angle);
    } else if (g->path == 1) {
        result.x += 2*radius*sin(u*3*PI+target.y*.3);
        result.y += radius*sin(u*2*PI+target.x*.12);
    } else {
        result.x += 3*radius*cos(phase); result.y += radius*sin(phase);
    }
    return result;
}
/* Each moving word carries its own piece of the shaped outline. Pieces
   condense along the particle trajectories, meet to form the letters, and
   separate again on release. Nothing is painted at an unoccupied destination. */
static void mantra_fragment(Canvas *c, const Options *o, int id, Flow position,
                            double gather, double t, int from, double fade) {
    if (gather <= .005 || !c->mantra_mask) return;
    const Flow target = c->particles[id].gather_target;
    double scale = c->mantra_pose.scale;
    double left = position.x, top = position.y;
    double width = c->mantra_patch_width*scale;
    double height = c->mantra_patch_height*scale;
    double source_x = target.x-c->mantra_left, source_y = target.y-c->mantra_top;
    for (int y = (int)floor(top); y < top+height; ++y) {
        for (int x = (int)floor(left); x < left+width; ++x) {
            if (x < 0 || x >= c->width-1 || y < 0 || y >= c->height) continue;
            Tile *tile = &c->frame[y*c->width+x];
            if (tile->glyph != -1 && tile->glyph < 260) continue;
            unsigned pattern = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    double center_x = x+(dx+.5)/2, center_y = y+(dy+.5)/2;
                    /* Ownership at quadrant centers lets adjacent pieces join
                       without seams or double-painting shared edge samples. */
                    if (center_x < left || center_x >= left+width ||
                        center_y < top || center_y >= top+height) continue;
                    int ink = 0;
                    for (int sy = 0; sy < 3; ++sy) {
                        for (int sx = 0; sx < 3; ++sx) {
                            double px = source_x+(x-left+(dx+(sx+.5)/3)/2)/scale;
                            double py = source_y+(y-top+(dy+(sy+.5)/3)/2)/scale;
                            int mx = (int)floor(px*c->mantra_mask->width/c->mantra_width);
                            int my = (int)floor(py*c->mantra_mask->height/c->mantra_height);
                            if (mx >= 0 && mx < c->mantra_mask->width && my >= 0 && my < c->mantra_mask->height)
                                ink += mask_ink(c->mantra_mask,mx,my);
                        }
                    }
                    if (ink >= 3) pattern |= 1u << (dy*2+dx);
                }
            }
            if (!pattern) continue;
            double phase = .56+.09*sin(x*3.0/c->width+y*2.4/c->height-t*.35);
            if (!o->palette_auto && !strcmp(palettes[o->palette].name,"rainbow"))
                phase = x/(double)c->width+y*.3/c->height-t*.125;
            uint32_t rgb = color_at(from,o->palette,fade,phase,position.light*gather,.8*gather);
            if (tile->glyph >= 260) {
                pattern |= (unsigned)(tile->glyph-260);
                /* Overlapping pieces share ink rather than flickering by order. */
                if (tile->rgb > rgb) rgb = tile->rgb;
            }
            *tile = (Tile){.glyph=260+(int)pattern,.rgb=rgb};
        }
    }
}
static void draw_glyph(int glyph, const char **words) {
    if (glyph < 4) { fputs(words[glyph],stdout); return; }
    static const unsigned quadrants[] = {
        0x20,0x2598,0x259d,0x2580,0x2596,0x258c,0x259e,0x259b,
        0x2597,0x259a,0x2590,0x259c,0x2584,0x2599,0x259f,0x2588
    };
    unsigned cp = glyph >= 260 ? quadrants[glyph-260] : 0x2800u+(unsigned)(glyph-4);
    unsigned char utf8[] = {(unsigned char)(0xe0u | (cp >> 12)),
                            (unsigned char)(0x80u | ((cp >> 6) & 63u)),
                            (unsigned char)(0x80u | (cp & 63u)),0};
    fputs((const char *)utf8,stdout);
}
static void draw(Canvas *c, const Options *o, double t, int from, double fade,
                 double timeline) {
    const char **words = o->ascii ? latin : devanagari;
    double gather = update_gathering(c,o,t,timeline);
    for (int i = 0; i < c->width*c->height; ++i)
        c->frame[i] = (Tile){.glyph=-1};
    for (int layer = 0; layer < 2; ++layer) {
        for (int id = 0; id < c->particle_count; ++id) {
            /* Keep draw priority stable even outside gatherings. */
            bool behind = background_particle(id);
            if (behind != (layer == 1)) continue;
            bool background = c->mantra_stage && behind;
            int column = id%c->particle_columns, row = id/c->particle_columns;
            bool visible = included(row,column,o->density);
            if (!visible && gather <= 0) continue;
            Flow p = gathering_position(c,o,id,t,timeline);
            if (!visible) p.light *= gather;
            double x = mod(p.x+16,c->width+32.0)-16;
            double y = mod(p.y+12,c->height+24.0)-12;
            if (background) p.light *= matrix_clearance(c,x,y,gather);

            int glyph = (column+row*3)%4;
            double size = c->width >= 40 && c->height >= 12 ? word_size(o->seed,id,t,gather) : 1;
            double enlarged = smooth((size-1)/1.5);
            double highlight = background ? gather*.45*pow(clamp((p.light-.3)/.46,0,1),2)
                                          : smoother((gather-.9)/.1);
            double fragment = c->mantra_stage && !behind ? gather : 0;
            word(c,o,x,y,glyph,p.light*(1-enlarged)*(1-fragment),t,from,fade,highlight);
            large_word(c,o,x,y,glyph,size,p.light*(1-fragment),t,from,fade);
            if (fragment > 0)
                mantra_fragment(c,o,id,(Flow){x,y,p.light},fragment,t,from,fade);
        }
    }
    /* Keep the event horizon empty, including enlarged glyphs crossing it.
       Dissolve the aperture with the formation and release it for gatherings. */
    double motion_blend = smoother((timeline-c->motion.changed)/c->motion.duration);
    double galaxy = ((c->motion.from == 10 ? 1-motion_blend : 0)
                     +(c->motion.to == 10 ? motion_blend : 0))*(1-gather);
    if (galaxy > .001) {
        /* Two neighboring streams fill out each spiral arm. They share the
           infall and fade in with the galaxy, then yield to the large mantra. */
        for (int id = 0; id < c->particle_count; ++id) {
            int column = id%c->particle_columns, row = id/c->particle_columns;
            if (!included(row,column,o->density)) continue;
            Flow p = spiral_galaxy(c,id,t);
            double dx = (p.x-c->width*.5)/fmax(1,c->width);
            double dy = (p.y-c->height*.5)/fmax(1,c->height);
            for (int side = -1; side <= 1; side += 2) {
                double angle = side*.15;
                double x = c->width*(.5+dx*cos(angle)-dy*sin(angle));
                double y = c->height*(.5+dx*sin(angle)+dy*cos(angle));
                word(c,o,x,y,(column+row*3+(side == 1 ? 1 : 3))%4,
                     p.light*galaxy*(1-gather),t,from,fade,0);
            }
        }
    }
    if (galaxy > 0) {
        double rx = fmax(3,c->width*.055), ry = fmax(1.5,c->height*.055);
        for (int y = 0; y < c->height; ++y)
            for (int x = 0; x < c->width; ++x) {
                double distance = hypot((x+.5-c->width*.5)/rx,(y+.5-c->height*.5)/ry);
                double aperture = galaxy*smooth((1.2-distance)/.2);
                if (aperture > variation(0,(unsigned)(y*c->width+x)+960000))
                    c->frame[y*c->width+x] = (Tile){.glyph=-1};
            }
    }
    /* At the height of inflation the letter strokes merge into a single
       luminous field. A spatial dissolve carries the field back into ink. */
    if (c->mantra_stage == 2 && (c->gathering.style == 3 || c->gathering.style == 4)) {
        double start = c->mantra_event*o->mantra_interval+o->mantra_interval*.25;
        double u = (timeline-start-c->gathering.gather)/c->gathering.hold;
        double fill = c->gathering.style == 3
                    ? smoother((u-.35)/.20)*smoother((.90-u)/.20)
                    : smoother((u-.48)/.09)*smoother((.88-u)/.16);
        uint32_t rgb = color_at(from,o->palette,fade,.56,.9,.8);
        for (int y = 0; y < c->height; ++y)
            for (int x = 0; x < c->width-1; ++x)
                if (variation(c->gathering.seed,(unsigned)(y*c->width+x)+1000) < fill)
                    c->frame[y*c->width+x] = (Tile){.glyph=275,.rgb=rgb};
    }
    /* Compose first, then publish complete rows in one synchronized update.
       No visible clear-then-repaint phase, and no periodic word replacement. */
    for (int y = 0; y < c->height; ++y) {
        bool changed = false;
        for (int x = 0; x < c->width && !changed; ++x) {
            const Tile *a = &c->previous[y*c->width+x], *b = &c->frame[y*c->width+x];
            changed = a->glyph != b->glyph || a->rgb != b->rgb;
        }
        if (!changed) continue;
        printf("\033[%d;1H\033[0;49m\033[2K",y+1);
        for (int x = 0; x < c->width; ++x) {
            const Tile *tile = &c->frame[y*c->width+x];
            if (tile->glyph < 0) continue;
            printf("\033[%d;%dH",y+1,x+1);
            set_color(tile->rgb,false,o->color);
            draw_glyph(tile->glyph,words);
        }
    }
    memcpy(c->previous,c->frame,(size_t)c->width*(size_t)c->height*sizeof *c->frame);
}
static void usage(FILE *out) {
    fputs("om — a full-terminal mantra visualizer\n\n"
          "Usage: ./om [options]\n\n"
          "  --mode NAME       river, bloom, lotus, rain, incense, lanterns, mandala, whirlpool, flag,\n"
          "                    lava, galaxy, cycle (default: cycle)\n"
          "  --palette NAME    pin a palette (default: blend curated palettes)\n"
          "  --fps N           frames per second, 5..60 (default: 60)\n"
          "  --speed N         animation speed, 0.1..5 (default: 1)\n"
          "  --density N       glyph density, 0.2..1 (default: 0.95)\n"
          "  --interval N      seconds per palette, 5..3600 (default: 8)\n"
          "  --scene-interval N  seconds per seamless morph, 10..3600 (default: 24)\n"
          "  --mantra-interval N  seconds between large mantra formations, 32..3600 (default: 120)\n"
          "  --no-mantra       disable the large mantra gatherings\n"
          "  --color MODE      auto, truecolor, 256 (default: auto)\n"
          "  --ascii           use Latin words instead of Devanagari\n"
          "  --duration N      exit after N seconds (default: unlimited)\n"
          "  --seed N          reproducible layout and animation variations\n"
          "  --list            list palettes and motions\n"
          "  -h, --help        show this help\n\n"
          "Keys: q/esc quit, space pause, p palette, m motion, a auto-cycle,\n"
          "      +/- speed, [/] density. Ctrl-C also exits cleanly.\n",out);
}
static int named(const char *s, const char *const *names, int count) {
    for (int i = 0; i < count; ++i) if (!strcmp(s,names[i])) return i;
    return -1;
}
static bool number(const char *s, double lo, double hi, double *value) {
    char *end;
    errno = 0;
    double n = strtod(s,&end);
    if (!*s || *end || errno || !isfinite(n) || n < lo || n > hi) return false;
    *value = n;
    return true;
}
static int parse(int argc, char **argv, Options *o) {
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!strcmp(arg,"--help") || !strcmp(arg,"-h")) { usage(stdout); return 1; }
        if (!strcmp(arg,"--list")) {
            fputs("Motions:",stdout);
            for (int m = 0; m < MODE_COUNT; ++m) printf(" %s",modes[m]);
            puts("; cycle (default) morphs continuously\nAlias: aurora = river\nPalettes:");
            for (int p = 0; p < PALETTE_COUNT; ++p) printf("  %s\n",palettes[p].name);
            return 1;
        }
        if (!strcmp(arg,"--ascii")) { o->ascii = true; continue; }
        if (!strcmp(arg,"--no-mantra")) { o->no_mantra = true; continue; }
        if (!strcmp(arg,"--no-hint")) continue; /* Compatibility with older commands. */
        if (i+1 >= argc) { fprintf(stderr,"om: missing value or unknown option: %s\n",arg); return -1; }
        const char *value = argv[++i];
        if (!strcmp(arg,"--mode")) {
            if (!strcmp(value,"cycle")) { o->mode_auto = true; continue; }
            int m = !strcmp(value,"aurora") ? 1 : named(value,modes,MODE_COUNT);
            if (m < 0) goto invalid;
            o->mode = m; o->mode_auto = false;
        } else if (!strcmp(arg,"--palette")) {
            int p = -1;
            for (int j = 0; j < PALETTE_COUNT; ++j)
                if (!strcmp(value,palettes[j].name)) p = j;
            if (p < 0) goto invalid;
            o->palette = p; o->palette_auto = false;
        } else if (!strcmp(arg,"--color")) {
            if (!strcmp(value,"256")) o->color = 256;
            else if (!strcmp(value,"truecolor")) o->color = 24;
            else if (!strcmp(value,"auto")) o->color = 0;
            else goto invalid;
        } else {
            double n;
            if (!strcmp(arg,"--fps")) {
                if (!number(value,5,60,&n) || floor(n) != n) goto invalid;
                o->fps = (int)n;
            } else if (!strcmp(arg,"--speed")) {
                if (!number(value,.1,5,&o->speed)) goto invalid;
            } else if (!strcmp(arg,"--density")) {
                if (!number(value,.2,1,&o->density)) goto invalid;
            } else if (!strcmp(arg,"--interval")) {
                if (!number(value,5,3600,&o->interval)) goto invalid;
            } else if (!strcmp(arg,"--scene-interval")) {
                if (!number(value,10,3600,&o->scene_interval)) goto invalid;
            } else if (!strcmp(arg,"--mantra-interval")) {
                if (!number(value,32,3600,&o->mantra_interval)) goto invalid;
            } else if (!strcmp(arg,"--duration")) {
                if (!number(value,.05,86400,&o->duration)) goto invalid;
            } else if (!strcmp(arg,"--seed")) {
                if (!number(value,0,UINT_MAX,&n) || floor(n) != n) goto invalid;
                o->seed = (unsigned)n;
            } else { fprintf(stderr,"om: unknown option: %s\n",arg); return -1; }
        }
        continue;
invalid:
        fprintf(stderr,"om: invalid value for %s: %s\n",arg,value);
        return -1;
    }
    return 0;
}
static double startup_animation(Options *o) {
    if (o->mode_auto) o->mode = (int)(variation(o->seed,800001)*MODE_COUNT);
    if (o->palette_auto) o->palette = (int)(variation(o->seed,800002)*(PALETTE_COUNT-1));
    return variation(o->seed,800003)*120;
}
int main(int argc, char **argv) {
    struct timespec seed_time;
    clock_gettime(CLOCK_REALTIME,&seed_time);
    Options o = {.fps=60,.palette=4,.mode=1,.speed=1,.density=.95,.interval=8,.scene_interval=24,.mantra_interval=120,
                 .palette_auto=true,.mode_auto=true,
                 .seed=(unsigned)seed_time.tv_sec ^ (unsigned)seed_time.tv_nsec ^ ((unsigned)getpid()*0x9e3779b9u)};
    int parsed = parse(argc,argv,&o);
    if (parsed) return parsed < 0 ? 2 : 0;
    double opening_time = startup_animation(&o);
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fputs("om: run in an interactive terminal (try ./om --help).\n",stderr); return 1;
    }
    const char *term = getenv("TERM"), *color = getenv("COLORTERM");
    if (term && !strcmp(term,"dumb")) {
        fputs("om: an ANSI-compatible terminal is required.\n",stderr); return 1;
    }
    if (!o.color) o.color = color && (strstr(color,"truecolor") || strstr(color,"24bit")) ? 24 : 256;
    setlocale(LC_ALL,"");
    rng_state = o.seed ? o.seed : 1;
    if (tcgetattr(STDIN_FILENO,&saved_termios) < 0) { perror("om: terminal"); return 1; }
    static char output_buffer[262144];
    setvbuf(stdout,output_buffer,_IOFBF,sizeof output_buffer);
    atexit(restore_terminal);
    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    int signals[] = {SIGINT,SIGTERM,SIGHUP,SIGWINCH,SIGTSTP,SIGCONT,SIGPIPE};
    for (size_t i = 0; i < sizeof signals/sizeof signals[0]; ++i)
        sigaction(signals[i],&action,NULL);
    if (!enter_terminal()) { perror("om: terminal"); return 1; }
    Canvas c = {0};
    int result = 0;
    if (!resize_canvas(&c)) { result = 1; goto done; }
    double started = now(), last = started, deadline = started, animation = opening_time, timeline = 0;
    /* Begin blending immediately, with no initial hold on the opening palette. */
    double palette_changed = -3, next_palette = 0;
    double next_scene = o.scene_interval;
    reset_motion(&c,&o,animation,timeline);
    int from_palette = o.palette;
    bool paused = false;
    while (!stopped) {
        double frame_start = now(), dt = clamp(frame_start-last,0,.25);
        last = frame_start;
        if (o.duration && frame_start-started >= o.duration) break;
        if (suspended) {
            restore_terminal();
            suspended = 0;
            /* SIGSTOP is unconditional; SIGCONT resumes at the next statement. */
            raise(SIGSTOP);
            continued = 1;
        }
        if (continued) {
            continued = 0;
            if (!terminal_active && !enter_terminal()) { result = 1; break; }
            resized = 1;
            last = now();
        }
        if (resized) {
            resized = 0;
            if (!resize_canvas(&c)) { result = 1; break; }
            reset_motion(&c,&o,animation,timeline);
            next_scene = timeline+o.scene_interval;
        }
        if (!paused) { animation += dt*o.speed; timeline += dt; }
        char keys[128];
        ssize_t count = read_keys(keys,sizeof keys);
        if (count < 0) break;
        if (count > 0) {
            for (ssize_t i = 0; i < count; ++i) {
                char key = keys[i];
                if (key == 'q' || key == 'Q' || key == 27) { stopped = 1; break; }
                if (key == ' ') paused = !paused;
                else if (key == '+' || key == '=') o.speed = clamp(o.speed+.2,.1,5);
                else if (key == '-') o.speed = clamp(o.speed-.2,.1,5);
                else if (key == '[') o.density = clamp(o.density-.05,.2,1);
                else if (key == ']') o.density = clamp(o.density+.05,.2,1);
                else if (key == 'p') {
                    from_palette = o.palette;
                    o.palette = (o.palette+1)%PALETTE_COUNT;
                    palette_changed = timeline;
                    next_palette = timeline+o.interval;
                } else if (key == 'm') {
                    o.mode = (o.mode+1)%MODE_COUNT;
                    start_motion(&c,o.mode,animation,timeline,o.scene_interval,o.speed);
                    next_scene = timeline+o.scene_interval;
                } else if (key == 'a') {
                    bool automatic = !(o.palette_auto || o.mode_auto);
                    o.palette_auto = o.mode_auto = automatic;
                    next_palette = timeline+o.interval;
                    next_scene = timeline+o.scene_interval;
                }
                invalidate(&c);
            }
        }
        if (o.palette_auto && timeline >= next_palette) {
            from_palette = o.palette;
            /* The full rainbow preset is opt-in; automatic color stays cohesive. */
            o.palette = (o.palette+1)%(PALETTE_COUNT-1);
            palette_changed = timeline;
            next_palette = timeline+o.interval;
        }
        if (o.mode_auto && timeline >= next_scene) {
            /* Every ambient formation participates in the same continuous cycle. */
            o.mode = c.motion.to;
            start_motion(&c,next_mode(o.mode),animation,timeline,o.scene_interval,o.speed);
            next_scene = timeline+o.scene_interval;
        }
        double fade = smooth((timeline-palette_changed)/o.interval);
        fputs("\033[?2026h",stdout);
        draw(&c,&o,animation,from_palette,fade,timeline);
        fputs("\033[?2026l",stdout);
        if (fflush(stdout) == EOF) { result = 1; break; }
        deadline += 1.0/o.fps;
        double remaining = deadline-now();
        if (remaining < -1.0/o.fps) deadline = now();
        if (remaining > 0) {
            struct timespec delay = {.tv_sec=0,.tv_nsec=(long)(remaining*1e9)};
            nanosleep(&delay,NULL);
        }
    }
done:
    restore_terminal();
    free(c.previous);
    free(c.frame);
    free(c.streams);
    free(c.particles);
    free(c.mantra_points);
    if (result) fputs("om: terminal output or size is unavailable.\n",stderr);
    return result;
}
