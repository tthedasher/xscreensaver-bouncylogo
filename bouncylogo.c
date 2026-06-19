/* bouncylogo.c — DVD-style bouncing logo XScreenSaver hack (Double-Buffered X11)
 *
 * Build:
 * gcc -O2 -o bouncylogo bouncylogo.c \
 * $(pkg-config --cflags --libs imlib2 x11 xext) \
 * -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <Imlib2.h>

/* ── palette ────────────────────────────────────────────────────────────── */
static const struct { unsigned char r, g, b; } PALETTE[] = {
    { 255,  60,  60 },
    {  60, 200, 255 },
    {  80, 255,  80 },
    { 255, 220,  40 },
    { 200,  80, 255 },
    { 255, 140,  30 },
    { 255, 100, 180 },
    {  40, 160, 255 },
};
#define PALETTE_SIZE ((int)(sizeof(PALETTE)/sizeof(PALETTE[0])))

/* ── state ──────────────────────────────────────────────────────────────── */
typedef struct {
    Display    *dpy;
    Window      win;
    Pixmap      buffer; /* Back buffer to eliminate flashing */
    int         screen;
    Visual     *visual;
    GC          gc;

    int         win_w, win_h;

    /* Pre-baked cached images: one per palette colour + one white for corner */
    Imlib_Image cache[PALETTE_SIZE + 1];  /* last slot = white flash */
    int         img_w, img_h;

    float       x, y;
    float       vx, vy;

    int         color_idx;
    int         celebrating;
    int         celebrate_max;

    int         speed;
    int         fps;
    float       scale;
    const char *image_path;
} State;

/* ── helpers ────────────────────────────────────────────────────────────── */

static void sleep_us(long us)
{
    struct timespec ts = { us / 1000000L, (us % 1000000L) * 1000L };
    nanosleep(&ts, NULL);
}

static int next_color(int current)
{
    int c;
    do { c = rand() % PALETTE_SIZE; } while (c == current && PALETTE_SIZE > 1);
    return c;
}

/* Tint the current imlib context image in-place. */
static void apply_tint(int r, int g, int b, float blend)
{
    int w = imlib_image_get_width();
    int h = imlib_image_get_height();
    DATA32 *data = imlib_image_get_data();

    for (int i = 0; i < w * h; i++) {
        DATA32 px = data[i];
        int a  = (px >> 24) & 0xff;
        if (a == 0) continue;
        int pr = (px >> 16) & 0xff;
        int pg = (px >>  8) & 0xff;
        int pb = (px      ) & 0xff;

        float lum = 0.299f * pr + 0.587f * pg + 0.114f * pb;

        int nr = (int)(lum + blend * (r - lum));
        int ng = (int)(lum + blend * (g - lum));
        int nb = (int)(lum + blend * (b - lum));

        nr = nr < 0 ? 0 : nr > 255 ? 255 : nr;
        ng = ng < 0 ? 0 : ng > 255 ? 255 : ng;
        nb = nb < 0 ? 0 : nb > 255 ? 255 : nb;

        data[i] = ((DATA32)a  << 24)
                | ((DATA32)nr << 16)
                | ((DATA32)ng <<  8)
                | ((DATA32)nb);
    }
    imlib_image_put_back_data(data);
}

/* Build all cached images at startup — called once. */
static void build_cache(State *st, Imlib_Image src, int src_w, int src_h)
{
    for (int c = 0; c < PALETTE_SIZE + 1; c++) {
        imlib_context_set_image(src);
        Imlib_Image scaled = imlib_create_cropped_scaled_image(
            0, 0, src_w, src_h, st->img_w, st->img_h);

        imlib_context_set_image(scaled);
        if (c < PALETTE_SIZE)
            apply_tint(PALETTE[c].r, PALETTE[c].g, PALETTE[c].b, 0.75f);
        else
            apply_tint(255, 255, 255, 0.9f);  /* white flash slot */

        st->cache[c] = scaled;
    }
}

/* ── render ─────────────────────────────────────────────────────────────── */

static void render(State *st)
{
    /* Clear the off-screen back buffer to black */
    XSetForeground(st->dpy, st->gc, BlackPixel(st->dpy, st->screen));
    XFillRectangle(st->dpy, st->buffer, st->gc, 0, 0, st->win_w, st->win_h);

    /* Pick the right cached image */
    int slot = (st->celebrating > 0) ? PALETTE_SIZE : st->color_idx;
    if (st->celebrating > 0) st->celebrating--;

    imlib_context_set_image(st->cache[slot]);
    imlib_context_set_display(st->dpy);
    imlib_context_set_visual(st->visual);
    imlib_context_set_colormap(DefaultColormap(st->dpy, st->screen));
    imlib_context_set_drawable(st->buffer); /* Draw onto the hidden pixmap buffer */
    imlib_context_set_blend(1);
    imlib_render_image_on_drawable((int)st->x, (int)st->y);

    /* Blit the completely generated buffer frame to the live window instantly */
    XCopyArea(st->dpy, st->buffer, st->win, st->gc, 0, 0, st->win_w, st->win_h, 0, 0);

    XFlush(st->dpy);
}

/* ── physics ────────────────────────────────────────────────────────────── */

static void update(State *st)
{
    st->x += st->vx;
    st->y += st->vy;

    int bx = 0, by = 0;

    if (st->x <= 0) {
        st->x  = 0;
        st->vx = fabsf(st->vx);
        bx = 1;
    } else if (st->x + st->img_w >= st->win_w) {
        st->x  = (float)(st->win_w - st->img_w);
        st->vx = -fabsf(st->vx);
        bx = 1;
    }

    if (st->y <= 0) {
        st->y  = 0;
        st->vy = fabsf(st->vy);
        by = 1;
    } else if (st->y + st->img_h >= st->win_h) {
        st->y  = (float)(st->win_h - st->img_h);
        st->vy = -fabsf(st->vy);
        by = 1;
    }

    if (bx || by) {
        st->color_idx = next_color(st->color_idx);
        if (bx && by) {
            st->celebrating = st->celebrate_max;
            fprintf(stderr, "bouncylogo: corner hit!\n");
        }
    }
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    State st;
    memset(&st, 0, sizeof(st));
    st.speed      = 3;
    st.fps        = 60;
    st.scale      = 0.25f;
    st.image_path = NULL;
    Window xwin   = 0;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-image")  && i+1 < argc) st.image_path = argv[++i];
        else if (!strcmp(argv[i], "-speed")  && i+1 < argc) st.speed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-fps")    && i+1 < argc) st.fps   = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-scale")  && i+1 < argc) st.scale = atof(argv[++i]);
        else if ((!strcmp(argv[i], "-window-id") ||
                  !strcmp(argv[i], "-window"))   && i+1 < argc)
            xwin = (Window)strtoul(argv[++i], NULL, 0);
    }

    if (!st.image_path) {
        const char *env = getenv("XSCREENSAVER_WINDOW");
        if (env) xwin = (Window)strtoul(env, NULL, 0);
    }

    if (!st.image_path) {
        fprintf(stderr, "bouncylogo: -image <path> is required\n");
        return 1;
    }

    srand((unsigned)time(NULL));

    st.dpy    = XOpenDisplay(NULL);
    if (!st.dpy) { fprintf(stderr, "bouncylogo: cannot open display\n"); return 1; }
    st.screen = DefaultScreen(st.dpy);
    st.visual = DefaultVisual(st.dpy, st.screen);

    if (xwin == 0) {
        const char *env = getenv("XSCREENSAVER_WINDOW");
        if (env) xwin = (Window)strtoul(env, NULL, 0);
    }
    if (xwin == 0)
        xwin = RootWindow(st.dpy, st.screen);
    st.win = xwin;

    XWindowAttributes wa;
    XGetWindowAttributes(st.dpy, st.win, &wa);
    st.win_w = wa.width;
    st.win_h = wa.height;

    st.gc = XCreateGC(st.dpy, st.win, 0, NULL);

    /* Allocate standard X11 off-screen Pixmap buffer matching window attributes */
    st.buffer = XCreatePixmap(st.dpy, st.win, st.win_w, st.win_h, wa.depth);

    /* Load image */
    imlib_context_set_display(st.dpy);
    imlib_context_set_visual(st.visual);
    imlib_context_set_colormap(DefaultColormap(st.dpy, st.screen));
    imlib_context_set_drawable(st.win);
    imlib_context_set_dither(1);

    Imlib_Image src = imlib_load_image(st.image_path);
    if (!src) {
        fprintf(stderr, "bouncylogo: cannot load '%s'\n", st.image_path);
        XFreePixmap(st.dpy, st.buffer);
        return 1;
    }

    imlib_context_set_image(src);
    int src_w = imlib_image_get_width();
    int src_h = imlib_image_get_height();

    /* Compute scaled size */
    st.img_h = (int)(st.win_h * st.scale);
    st.img_w = (int)((float)src_w / src_h * st.img_h);
    if (st.img_w >= st.win_w) st.img_w = st.win_w - 2;
    if (st.img_h >= st.win_h) st.img_h = st.win_h - 2;

    /* Pre-bake all tinted versions */
    fprintf(stderr, "bouncylogo: building image cache...\n");
    build_cache(&st, src, src_w, src_h);
    imlib_context_set_image(src);
    imlib_free_image(); /* done with source */
    fprintf(stderr, "bouncylogo: cache ready.\n");

    /* Top-left screen coordinate initial positions */
    st.x = (float)(rand() % (st.win_w - st.img_w));
    st.y = (float)(rand() % (st.win_h - st.img_h));

    float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
    st.vx = cosf(angle) * st.speed;
    st.vy = sinf(angle) * st.speed;
    if (fabsf(st.vx) < 1.0f) st.vx = st.vx < 0 ? -1.0f :  1.0f;
    if (fabsf(st.vy) < 1.0f) st.vy = st.vy < 0 ? -1.0f :  1.0f;

    st.color_idx     = rand() % PALETTE_SIZE;
    st.celebrate_max = st.fps / 2;

    long frame_us = 1000000L / st.fps;

    /* Main loop */
    for (;;) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        update(&st);
        render(&st);

        while (XPending(st.dpy)) {
            XEvent ev;
            XNextEvent(st.dpy, &ev);
        }

        clock_gettime(CLOCK_MONOTONIC, &t1);
        long elapsed = (t1.tv_sec  - t0.tv_sec)  * 1000000L
                     + (t1.tv_nsec - t0.tv_nsec) / 1000L;
        long remaining = frame_us - elapsed;
        if (remaining > 0) sleep_us(remaining);
    }

    XFreePixmap(st.dpy, st.buffer);
    XFreeGC(st.dpy, st.gc);
    XCloseDisplay(st.dpy);
    return 0;
}