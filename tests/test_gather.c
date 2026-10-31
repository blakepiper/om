/* cc -std=c11 -O2 tests/test_gather.c -lm -o .cache/test-gather
   .cache/test-gather [.cache/preview.json]
   Check both visible lettering and continuity of gathering/release. */
#define main om_application_main
#include "../src/om.c"
#undef main
#include <assert.h>

static Canvas canvas(int width, int height) {
    Canvas c = {.width=width,.height=height};
    c.columns = (width+5)/6;
    c.particle_columns = (int)ceil((width+32.0)/7);
    c.particle_count = c.particle_columns*((int)ceil(height/2.5)+9);
    c.particles = calloc((size_t)c.particle_count,sizeof *c.particles);
    c.streams = calloc((size_t)c.columns,sizeof *c.streams);
    c.previous = calloc((size_t)width*(size_t)height,sizeof *c.previous);
    c.frame = calloc((size_t)width*(size_t)height,sizeof *c.frame);
    assert(c.particles && c.streams && c.previous && c.frame);
    for (int i = 0; i < c.columns; ++i)
        c.streams[i] = (Stream){.speed=2.2+(i%11)*.5,.offset=i*2};
    return c;
}
static void release_canvas(Canvas *c) {
    free(c->particles); free(c->streams); free(c->previous); free(c->frame);
    free(c->mantra_points);
}
static void preview(const Canvas *c, const char *path) {
    FILE *out = fopen(path,"w");
    assert(out);
    fprintf(out,"{\"width\":%d,\"height\":%d,\"words\":[",c->width,c->height);
    bool first = true;
    for (int y = 0; y < c->height; ++y)
        for (int x = 0; x < c->width; ++x) {
            Tile tile = c->frame[y*c->width+x];
            if (tile.glyph < 0) continue;
            fprintf(out,"%s[%d,%d,%d,%u]",first ? "" : ",",x,y,tile.glyph,tile.rgb);
            first = false;
        }
    fputs("]}\n",out); fclose(out);
}
static void check_carried_strokes(Canvas *c, const Options *o, double t) {
    draw(c,o,t,4,1,t);
    bool *carried = calloc((size_t)c->width*c->height,sizeof *carried);
    assert(carried);
    for (int id = 0; id < c->particle_count; id += 2) {
        Flow p = gathering_position(c,o,id,t,t);
        double x = mod(p.x+16,c->width+32.0)-16;
        double y = mod(p.y+12,c->height+24.0)-12;
        double width = c->mantra_patch_width*c->mantra_pose.scale;
        double height = c->mantra_patch_height*c->mantra_pose.scale;
        for (int row = (int)fmax(0,floor(y)); row < fmin(c->height,y+height); ++row)
            for (int col = (int)fmax(0,floor(x)); col < fmin(c->width,x+width); ++col)
                carried[row*c->width+col] = true;
    }
    int fragments = 0;
    for (int cell = 0; cell < c->width*c->height; ++cell) {
        if (c->frame[cell].glyph < 260) continue;
        ++fragments;
        /* A destination-only overlay would paint outside these moving footprints. */
        assert(carried[cell]);
    }
    assert(fragments > 0);
    free(carried);
}
int main(int argc, char **argv) {
    assert(freopen("/dev/null","w",stdout));
    Options o = {.mode=1,.mode_auto=true,.scene_interval=24,.speed=1,.palette=4,
                 .palette_auto=true,.density=.85,.color=24,.mantra_interval=60};
    unsigned styles = 0, paths = 0;
    int widths[] = {80,120,240,64}, heights[] = {24,40,32,20};
    for (int size = 0; size < 4; ++size) {
        Canvas c = canvas(widths[size],heights[size]);
        reset_motion(&c,&o,0,0);
        Flow *previous = calloc((size_t)c.particle_count,sizeof *previous);
        assert(previous);
        double next_scene = 24, max_step = 0;
        int last_hold_event = -1;
        bool saw_gather = false, saw_hold = false, saw_release = false;
        for (int frame = 0; frame <= 100*60; ++frame) {
            double t = frame/60.0;
            if (t >= next_scene) {
                start_motion(&c,next_mode(c.motion.to),t,t,24,1);
                next_scene += 24;
            }
            update_gathering(&c,&o,t,t);
            if (c.mantra_stage == 1) saw_gather = true;
            if (c.mantra_stage == 2) saw_hold = true;
            if (c.mantra_stage == 3) saw_release = true;
            double age = t-c.mantra_event*o.mantra_interval-o.mantra_interval*.25;
            double settled = c.gathering.gather+c.gathering.hold;
            assert(settled+c.gathering.release < o.mantra_interval*.75);
            if (c.mantra_stage == 2) {
                styles |= 1u << c.gathering.style;
                paths |= 1u << c.gathering.path;
                MantraPose pose = c.mantra_pose;
                if (c.gathering.style < 3) assert(pose.left >= 0 && pose.top >= 0);
                if (c.gathering.style < 3) assert(pose.left+c.mantra_width*pose.scale < c.width);
                if (c.gathering.style < 3) assert(pose.top+c.mantra_height*pose.scale < c.height);
            }
            for (int id = 0; id < c.particle_count; ++id) {
                Flow p = gathering_position(&c,&o,id,t,t);
                assert(isfinite(p.x) && isfinite(p.y) && isfinite(p.light));
                if (frame) {
                    double dx = remainder(p.x-previous[id].x,c.width+32.0);
                    double dy = remainder(p.y-previous[id].y,c.height+24.0);
                    double step = hypot(dx,dy);
                    if (step > max_step) max_step = step;
                    if (c.gathering.style != 4) assert(step < 3);
                }
                previous[id] = p;
            }
            if (c.mantra_stage == 2 && age > c.gathering.gather+1 && last_hold_event != c.mantra_event) {
                last_hold_event = c.mantra_event;
                assert(c.mantra_stage == 2 && c.mantra_point_count > 10);
                draw(&c,&o,t,4,1,t);
                int rendered = 0, connected = 0, surrounding = 0;
                for (int cell = 0; cell < c.width*c.height; ++cell) {
                    if (c.frame[cell].glyph >= 0 && c.frame[cell].glyph < 4) {
                        ++rendered;
                        if (matrix_clearance(&c,cell%c.width,cell/c.width,1) > .5) ++surrounding;
                    }
                    if (c.frame[cell].glyph >= 4) ++connected;
                }
                /* Settled text has a solid silhouette, with small words around it. */
                assert(rendered > 3);
                assert(connected > c.mantra_point_count/2);
                assert(surrounding > 3);
                if (argc > 1 && size == 1 && c.mantra_event == 0) preview(&c,argv[1]);
            }
        }
        assert(saw_gather && saw_hold && saw_release);
        fprintf(stderr,"%dx%d: %d stroke targets with visible lettering; two gatherings and releases "
                "continuous (max %.3f cells/frame).\n",c.width,c.height,c.mantra_point_count,max_step);
        free(previous); release_canvas(&c);
    }
    /* Exercise every behavior across seeds, including the shortest interval.
       Event selection stays reproducible and independent of the global RNG. */
    o.mantra_interval = 32;
    for (unsigned seed = 0; seed < 24; ++seed) {
        o.seed = seed;
        o.mode = (int)(seed%MODE_COUNT);
        Canvas c = canvas(120,40), same = canvas(120,40);
        choose_gathering(&c,&o,0);
        random_unit();
        choose_gathering(&same,&o,0);
        assert(c.gathering.style == same.gathering.style);
        assert(c.gathering.hold == same.gathering.hold);
        assert(c.mantra_point_count == same.mantra_point_count);
        assert(fabs(c.mantra_width/c.mantra_height-2.0*c.mantra_mask->width/c.mantra_mask->height) < 1e-9);
        if (c.mantra_mask == &mantra_line) assert(c.mantra_height >= 9);
        styles |= 1u << c.gathering.style;
        paths |= 1u << c.gathering.path;
        reset_motion(&c,&o,0,0);
        double settled = c.gathering.gather+c.gathering.hold;
        assert(8+settled+c.gathering.release < 32);
        MantraPose first = mantra_pose(&c,c.gathering.gather);
        MantraPose last = mantra_pose(&c,settled);
        if (c.gathering.style == 2) assert(fabs(last.left-first.left) > 10);
        double previous_x = 0, previous_dx = 0;
        int bounces = 0;
        for (int frame = 0; frame <= (int)(c.gathering.hold*60); ++frame) {
            MantraPose pose = mantra_pose(&c,c.gathering.gather+frame/60.0);
            if (c.gathering.style < 3) assert(pose.left >= 0 && pose.top >= 0);
            if (c.gathering.style < 3) assert(pose.left+c.mantra_width*pose.scale < c.width);
            if (c.gathering.style < 3) assert(pose.top+c.mantra_height*pose.scale < c.height);
            double x = pose.left-c.mantra_width*(1-pose.scale)*.5;
            double dx = x-previous_x;
            if (frame > 1 && dx*previous_dx < 0) ++bounces;
            previous_x = x; previous_dx = dx;
        }
        if (c.gathering.style == 1) assert(bounces > 0);
        for (int sample = 0; sample < 3; ++sample)
            check_carried_strokes(&c,&o,8+c.gathering.gather*(.35+.2*sample));
        for (int sample = 0; sample < 5; ++sample) {
            double t = 8+c.gathering.gather+c.gathering.hold*(sample+.5)/5;
            draw(&c,&o,t,4,1,t);
            int surrounding = 0, solid = 0;
            for (int cell = 0; cell < c.width*c.height; ++cell) {
                if (c.frame[cell].glyph >= 0 && c.frame[cell].glyph < 4 &&
                    matrix_clearance(&c,cell%c.width,cell/c.width,1) > .5) ++surrounding;
                if (c.frame[cell].glyph >= 260) ++solid;
                int x = cell%c.width, y = cell/c.width;
                if (x >= c.mantra_pose.left && x < c.mantra_pose.left+c.mantra_width*c.mantra_pose.scale &&
                    y >= c.mantra_pose.top && y < c.mantra_pose.top+c.mantra_height*c.mantra_pose.scale)
                    assert(c.frame[cell].glyph < 0 || c.frame[cell].glyph >= 260);
            }
            if (c.gathering.style < 3) assert(surrounding > 10);
            if (c.gathering.style < 4) assert(solid > 50);
            /* Identical clocks must produce identical frames when paused. */
            Tile *held = malloc((size_t)c.width*c.height*sizeof *held);
            assert(held);
            memcpy(held,c.frame,(size_t)c.width*c.height*sizeof *held);
            draw(&c,&o,t,4,1,t);
            for (int cell = 0; cell < c.width*c.height; ++cell) {
                assert(held[cell].glyph == c.frame[cell].glyph);
                assert(held[cell].rgb == c.frame[cell].rgb);
            }
            free(held);
        }
        for (int sample = 0; sample < 3; ++sample)
            check_carried_strokes(&c,&o,8+settled+c.gathering.release*(.25+.2*sample));
        /* Rain falls in fixed columns, with a brighter head than its tail. */
        for (int id = 1; id < c.particle_count; id += 2) {
            Flow a = matrix_position(&c,id,10), b = matrix_position(&c,id,10.01);
            assert(a.x == b.x && b.y > a.y);
            assert(b.y-a.y < .08);
            int head_id = 2*((id/2)%c.columns)+1;
            assert(matrix_position(&c,head_id,10).light >= a.light);
        }
        /* Reset before exercising the gathering boundaries chronologically. */
        c.mantra_stage = 0;
        double boundaries[] = {8,8+c.gathering.gather,8+settled,8+settled+c.gathering.release};
        for (int boundary = 0; boundary < 4; ++boundary) {
            double t = boundaries[boundary], eps = .00001;
            update_gathering(&c,&o,t-eps,t-eps);
            Flow before[4], middle[4];
            for (int id = 0; id < 4; ++id) before[id] = gathering_position(&c,&o,id,t-eps,t-eps);
            update_gathering(&c,&o,t,t);
            for (int id = 0; id < 4; ++id) middle[id] = gathering_position(&c,&o,id,t,t);
            update_gathering(&c,&o,t+eps,t+eps);
            for (int id = 0; id < 4; ++id) {
                Flow after = gathering_position(&c,&o,id,t+eps,t+eps);
                double dx1 = remainder(middle[id].x-before[id].x,c.width+32.0)/eps;
                double dy1 = remainder(middle[id].y-before[id].y,c.height+24.0)/eps;
                double dx2 = remainder(after.x-middle[id].x,c.width+32.0)/eps;
                double dy2 = remainder(after.y-middle[id].y,c.height+24.0)/eps;
                assert(hypot(dx2-dx1,dy2-dy1) < .02);
            }
        }
        release_canvas(&c); release_canvas(&same);
    }
    assert(styles == 31 && paths == 7);
    /* Enlarged ambient words vary over time and become native words at assembly. */
    int enlarged = 0;
    for (int id = 0; id < 400; ++id) {
        double minimum = 10, maximum = 0;
        for (int frame = 0; frame < 1200; ++frame) {
            double size = word_size(42,id,frame/60.0,0);
            assert(isfinite(size) && size >= 1 && size <= 4.9);
            assert(word_size(42,id,frame/60.0,1) == 1);
            minimum = fmin(minimum,size); maximum = fmax(maximum,size);
        }
        if (maximum > 2.5 && maximum-minimum > 1) ++enlarged;
    }
    assert(enlarged > 30 && enlarged < 120);
    fprintf(stderr,"All holds and paths, surrounding rain, paused frames, seeded replay, "
            "endpoint velocities, and size pulses passed.\n");
    Canvas tiny = canvas(40,12);
    reset_motion(&tiny,&o,0,0);
    assert(update_gathering(&tiny,&o,22,22) == 0);
    assert(tiny.mantra_stage == 0);
    release_canvas(&tiny);
    return 0;
}
