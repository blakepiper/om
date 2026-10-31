/* Compile in nix develop:
   cc -std=c11 -O2 tests/test_morph.c -lm -o .cache/test-morph
   .cache/test-morph
   Exercise continuous trajectories before projection into terminal cells. */
#define main om_application_main
#include "../src/om.c"
#undef main
#include <assert.h>

static double separation(Flow a, Flow b) { return hypot(a.x-b.x,a.y-b.y); }

static void projection_sizes(void) {
    /* Tilted formations must remain finite even in an extremely wide, short
       terminal, where an unbounded perspective denominator can cross zero. */
    const int sizes[][2] = {{1,1},{80,24},{240,32},{320,90},{4096,1},{1,512}};
    for (size_t size = 0; size < sizeof sizes/sizeof sizes[0]; ++size) {
        Canvas c = {.width=sizes[size][0],.height=sizes[size][1]};
        c.particle_columns = (int)ceil((c.width+32.0)/7);
        c.particle_count = c.particle_columns*((int)ceil(c.height/2.5)+9);
        for (int mode = 1; mode < MODE_COUNT; ++mode)
            for (int step = 0; step < 180; ++step)
                for (int id = 0; id < c.particle_count; id += 7) {
                    Flow p = formation(&c,mode,id,step*.73);
                    assert(isfinite(p.x) && isfinite(p.y) && isfinite(p.light));
                    assert(p.light >= .25 && p.light <= .98);
                    if (mode == 4 || mode == 5) {
                        Flow after = formation(&c,mode,id,step*.73+.001);
                        /* Direction must hold for every word, not just the average. */
                        if (mode == 4) assert(after.x < p.x);
                        if (mode == 5) assert(after.y < p.y);
                    }
                    if (mode == 2 || mode == 3 || mode == 6) {
                        assert(fabs(p.x) < 8.0*(c.width+c.height));
                        assert(fabs(p.y) < 8.0*(c.width+c.height));
                    }
                }
    }
}

static void boundary(Canvas *c, double time, int destination) {
    Flow *positions = calloc((size_t)c->particle_count,sizeof *positions);
    Flow *velocities = calloc((size_t)c->particle_count,sizeof *velocities);
    assert(positions && velocities);
    const double eps = .00001;
    for (int id = 0; id < c->particle_count; ++id) {
        positions[id] = motion_position(c,id,time,time);
        Flow before = motion_position(c,id,time-eps,time-eps);
        velocities[id] = (Flow){(positions[id].x-before.x)/eps,
                                (positions[id].y-before.y)/eps,0};
    }
    start_motion(c,destination,time,time,10,1);
    for (int id = 0; id < c->particle_count; ++id) {
        Flow current = motion_position(c,id,time,time);
        Flow after = motion_position(c,id,time+eps,time+eps);
        Flow velocity = {(after.x-current.x)/eps,(after.y-current.y)/eps,0};
        assert(separation(positions[id],current) < 1e-8);
        assert(fabs(positions[id].light-current.light) < 1e-8);
        assert(separation(velocities[id],velocity) < .003);
    }
    free(positions);
    free(velocities);
}

int main(void) {
    unsigned openings = 0, colors = 0;
    for (unsigned seed = 0; seed < 100; ++seed) {
        Options a = {.seed=seed,.mode_auto=true,.palette_auto=true}, b = a;
        double phase = startup_animation(&a);
        assert(phase == startup_animation(&b));
        assert(a.mode == b.mode && a.palette == b.palette);
        assert(phase >= 0 && phase < 120);
        openings |= 1u << a.mode;
        colors |= 1u << a.palette;
        Options pinned = {.seed=seed,.mode=2,.palette=8};
        startup_animation(&pinned);
        assert(pinned.mode == 2 && pinned.palette == 8);
    }
    assert(openings == (1u << MODE_COUNT)-1);
    assert(colors == (1u << (PALETTE_COUNT-1))-1);
    projection_sizes();
    /* Ripples move both sides and keep words in every region of the viewport. */
    Canvas flag = {.width=120,.height=40,.particle_columns=22,.particle_count=550};
    for (int step = 0; step < 120; ++step) {
        unsigned covered = 0;
        for (int id = 0; id < flag.particle_count; ++id) {
            Flow p = formation(&flag,8,id,step*.1);
            if (p.x >= 0 && p.x < flag.width && p.y >= 0 && p.y < flag.height) {
                int col = (int)(p.x/flag.width*4), row = (int)(p.y/flag.height*4);
                covered |= 1u << (row*4+col);
            }
        }
        assert(covered == 0xffff);
    }
    for (int column = 0; column < flag.particle_columns; ++column) {
        Flow initial = formation(&flag,8,column,0);
        double movement = 0, lighting = 0;
        for (int step = 1; step < 120; ++step) {
            Flow p = formation(&flag,8,column,step*.1);
            movement = fmax(movement,separation(initial,p));
            lighting = fmax(lighting,fabs(initial.light-p.light));
        }
        assert(movement > 3 && lighting > .2);
    }
    /* Lava retains background activity in all regions while each bubble rises. */
    for (int step = 0; step < 240; ++step) {
        double t = step*.5;
        unsigned covered = 0;
        double before[6] = {0}, after[6] = {0};
        for (int id = 0; id < flag.particle_count; ++id) {
            Flow p = formation(&flag,9,id,t);
            if (id%2 == 0) {
                double x = mod(p.x+16,flag.width+32.0)-16;
                double y = mod(p.y+12,flag.height+24.0)-12;
                if (x >= 0 && x < flag.width && y >= 0 && y < flag.height)
                    covered |= 1u << ((int)(y/flag.height*4)*4+(int)(x/flag.width*4));
            } else {
                int bubble = (id/2)%6;
                before[bubble] += p.y;
                after[bubble] += formation(&flag,9,id,t+.01).y;
            }
        }
        assert(covered == 0xffff);
        for (int bubble = 0; bubble < 6; ++bubble)
            assert(after[bubble] < before[bubble]);
    }
    Canvas c = {.width=120,.height=40,.columns=20,.particle_columns=22};
    c.particle_count = c.particle_columns*((int)ceil(c.height/2.5)+9);
    c.particles = calloc((size_t)c.particle_count,sizeof *c.particles);
    c.streams = calloc((size_t)c.columns,sizeof *c.streams);
    c.previous = calloc((size_t)c.width*(size_t)c.height,sizeof *c.previous);
    c.frame = calloc((size_t)c.width*(size_t)c.height,sizeof *c.frame);
    assert(c.particles && c.streams && c.previous && c.frame);
    for (int i = 0; i < c.columns; ++i)
        c.streams[i] = (Stream){.speed=3+i*.1,.offset=i*2};
    Options o = {.mode=1,.mode_auto=true,.scene_interval=10,.speed=1,
                 .palette=4,.palette_auto=true,.density=.85,.color=24};
    assert(freopen("/dev/null","w",stdout));
    for (int mode = 4; mode < MODE_COUNT; ++mode) {
        Options pinned = o;
        pinned.mode = mode; pinned.mode_auto = false;
        reset_motion(&c,&pinned,0,0);
        for (int frame = 0; frame < 120; ++frame) {
            double t = frame*.5;
            draw(&c,&pinned,t,4,1,t);
            int rendered = 0;
            for (int cell = 0; cell < c.width*c.height; ++cell)
                if (c.frame[cell].glyph >= 0 && c.frame[cell].rgb != 0) ++rendered;
            assert(rendered > 60);
        }
    }
    reset_motion(&c,&o,0,0);
    double max_step = 0;
    unsigned visited = 0;
    for (int stage = 0; stage < MODE_COUNT*2; ++stage) {
        visited |= 1u << c.motion.to;
        for (int frame = 0; frame < 600; ++frame) {
            double time = stage*10+frame/60.0;
            int visible = 0;
            for (int id = 0; id < c.particle_count; ++id) {
                Flow a = motion_position(&c,id,time,time);
                Flow b = motion_position(&c,id,time+1.0/60,time+1.0/60);
                assert(isfinite(a.x) && isfinite(a.y) && isfinite(a.light));
                assert(a.light >= .25 && a.light <= .98);
                double step = separation(a,b);
                if (step > max_step) max_step = step;
                assert(step < 2);
                assert(fabs(a.light-b.light) < .08);
                double x = mod(a.x+16,c.width+32.0)-16;
                double y = mod(a.y+12,c.height+24.0)-12;
                if (x > 2 && x < c.width-7 && y > 0 && y < c.height-2) ++visible;
            }
            /* No blackout or collapse into an empty offscreen arrangement. */
            assert(visible > 20);
            if (frame%30 == 0) {
                draw(&c,&o,time,4,1,time);
                int rendered = 0;
                for (int cell = 0; cell < c.width*c.height; ++cell)
                    if (c.frame[cell].glyph >= 0 && c.frame[cell].rgb != 0) ++rendered;
                assert(rendered > 20);
            }
        }
        boundary(&c,(stage+1)*10,next_mode(c.motion.to));
    }
    assert(visited == (1u << MODE_COUNT)-1); /* Includes unattended rain. */
    /* Repeated key presses mid-morph must preserve position and velocity too. */
    boundary(&c,MODE_COUNT*20+3.5,4);
    boundary(&c,MODE_COUNT*20+5.1,5);
    boundary(&c,MODE_COUNT*20+7.7,6);
    fprintf(stderr,"Twenty morphs: continuous positions and velocities, no blackout; "
           "max movement %.3f cells/frame. Mid-morph interruptions also continuous.\n",max_step);
    free(c.particles);
    free(c.streams);
    free(c.previous);
    free(c.frame);
    return 0;
}
