#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <libraries/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "fits.h"
#include "viewer.h"

#define VIEW_WIDTH 320
#define VIEW_HEIGHT 200
#define VIEW_PIXELS ((long)VIEW_WIDTH * (long)VIEW_HEIGHT)
#define VIEW_BYTES_PER_ROW (VIEW_WIDTH / 8)
#define VIEW_PLANE_SIZE ((long)VIEW_BYTES_PER_ROW * (long)VIEW_HEIGHT)

static int opened_intuition;
static int opened_graphics;
static UBYTE chunky_buffer[VIEW_PIXELS];
static UBYTE planar_buffer[4][VIEW_PLANE_SIZE];

static void debug_step(const char *text)
{
    printf("amigafits: debug: %s\n", text);
    fflush(stdout);
}

static int open_runtime_libraries(char *error_text)
{
    opened_intuition = 0;
    opened_graphics = 0;

    debug_step("checking intuition.library");
    if (IntuitionBase == 0) {
        debug_step("opening intuition.library");
        IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 0L);
        opened_intuition = IntuitionBase != 0;
    }
    if (IntuitionBase == 0) {
        sprintf(error_text, "cannot open intuition.library");
        return -1;
    }
    debug_step("intuition.library ready");

    debug_step("checking graphics.library");
    if (GfxBase == 0) {
        debug_step("opening graphics.library");
        GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 0L);
        opened_graphics = GfxBase != 0;
    }
    if (GfxBase == 0) {
        sprintf(error_text, "cannot open graphics.library");
        if (opened_intuition) {
            CloseLibrary((struct Library *)IntuitionBase);
            IntuitionBase = 0;
            opened_intuition = 0;
        }
        return -1;
    }
    debug_step("graphics.library ready");

    return 0;
}

static void close_runtime_libraries(void)
{
    if (opened_graphics && GfxBase != 0) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = 0;
    }
    if (opened_intuition && IntuitionBase != 0) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = 0;
    }
    opened_graphics = 0;
    opened_intuition = 0;
}

static void progress_label(const char *text)
{
    printf("amigafits: %s [", text);
    fflush(stdout);
}

static void progress_done(void)
{
    printf("]\n");
    fflush(stdout);
}

static void progress_dot(void)
{
    putchar('.');
    fflush(stdout);
}

static void set_colormap_palette(struct ViewPort *vp, int depth)
{
    static const UBYTE colormap[16][3] = {
        {0, 0, 0},
        {0, 0, 3},
        {0, 0, 6},
        {0, 1, 9},
        {0, 3, 12},
        {0, 6, 15},
        {0, 10, 15},
        {0, 14, 13},
        {0, 15, 7},
        {5, 15, 0},
        {10, 15, 0},
        {15, 15, 0},
        {15, 11, 0},
        {15, 6, 0},
        {15, 10, 10},
        {15, 15, 15}
    };
    int i;
    int colors;

    colors = 1 << depth;

    for (i = 0; i < colors; i++) {
        int source;

        source = colors == 1 ? 0 : i * 15 / (colors - 1);
        SetRGB4(vp, (LONG)i,
                (LONG)colormap[source][0],
                (LONG)colormap[source][1],
                (LONG)colormap[source][2]);
    }
}

static int render_image(const struct FitsImage *image, char *error_text)
{
    int x;
    int y;
    int byte_x;
    long scaled_width;
    long scaled_height;
    long crop_x;
    long crop_y;
    int plane;
    int source_xs[VIEW_WIDTH];
    int source_ys[VIEW_HEIGHT];
    int depth;

    depth = image->depth;
    if (depth < 1 || depth > 4) {
        sprintf(error_text, "invalid display depth");
        return -1;
    }

    for (plane = 0; plane < depth; plane++) {
        memset(planar_buffer[plane], 0, (size_t)VIEW_PLANE_SIZE);
    }

    scaled_width = VIEW_WIDTH;
    scaled_height = (long)image->height * VIEW_WIDTH / image->width;
    if (scaled_height < VIEW_HEIGHT) {
        scaled_height = VIEW_HEIGHT;
        scaled_width = (long)image->width * VIEW_HEIGHT / image->height;
    }

    crop_x = (scaled_width - VIEW_WIDTH) / 2;
    crop_y = (scaled_height - VIEW_HEIGHT) / 2;

    for (x = 0; x < VIEW_WIDTH; x++) {
        source_xs[x] = (int)(((long)x + crop_x) * image->width / scaled_width);
        if (source_xs[x] < 0) {
            source_xs[x] = 0;
        } else if (source_xs[x] >= image->width) {
            source_xs[x] = image->width - 1;
        }
    }

    for (y = 0; y < VIEW_HEIGHT; y++) {
        source_ys[y] = (int)(((long)y + crop_y) * image->height / scaled_height);
        if (source_ys[y] < 0) {
            source_ys[y] = 0;
        } else if (source_ys[y] >= image->height) {
            source_ys[y] = image->height - 1;
        }
    }

    progress_label("scaling image");
    for (y = 0; y < VIEW_HEIGHT; y++) {
        int source_y;
        UBYTE *chunky_row;

        source_y = source_ys[y];
        chunky_row = chunky_buffer + (long)y * VIEW_WIDTH;

        for (x = 0; x < VIEW_WIDTH; x++) {
            int source_x;

            source_x = source_xs[x];
            chunky_row[x] = image->pens[(long)source_y * image->width + source_x];
        }
        if ((y + 1) % 20 == 0) {
            progress_dot();
        }
    }
    progress_done();

    progress_label("converting to bitplanes");
    for (y = 0; y < VIEW_HEIGHT; y++) {
        UBYTE *chunky_row;
        ULONG row_offset;

        chunky_row = chunky_buffer + (long)y * VIEW_WIDTH;
        row_offset = (ULONG)y * VIEW_BYTES_PER_ROW;

        for (byte_x = 0; byte_x < VIEW_WIDTH / 8; byte_x++) {
            UBYTE plane_bytes[4];
            int base;
            int bit;

            plane_bytes[0] = 0;
            plane_bytes[1] = 0;
            plane_bytes[2] = 0;
            plane_bytes[3] = 0;
            base = byte_x * 8;

            for (bit = 0; bit < 8; bit++) {
                UBYTE pen;
                UBYTE mask;

                pen = chunky_row[base + bit];
                mask = (UBYTE)(0x80 >> bit);

                for (plane = 0; plane < depth; plane++) {
                    if (pen & (1 << plane)) {
                        plane_bytes[plane] |= mask;
                    }
                }
            }

            for (plane = 0; plane < depth; plane++) {
                planar_buffer[plane][row_offset + byte_x] = plane_bytes[plane];
            }
        }
        if ((y + 1) % 20 == 0) {
            progress_dot();
        }
    }
    progress_done();

    return 0;
}

static int copy_render_to_bitmap(struct BitMap *bitmap, int depth, char *error_text)
{
    int plane;
    int y;

    if (bitmap == 0 || bitmap->Depth < depth) {
        sprintf(error_text, "viewer bitmap has no direct bitplanes");
        return -1;
    }

    for (plane = 0; plane < depth; plane++) {
        if (bitmap->Planes[plane] == 0) {
            sprintf(error_text, "viewer bitmap has no direct bitplanes");
            return -1;
        }
    }

    for (plane = 0; plane < depth; plane++) {
        if (bitmap->BytesPerRow == VIEW_BYTES_PER_ROW) {
            memcpy(bitmap->Planes[plane], planar_buffer[plane], (size_t)VIEW_PLANE_SIZE);
        } else {
            for (y = 0; y < VIEW_HEIGHT; y++) {
                memcpy(bitmap->Planes[plane] + (ULONG)y * bitmap->BytesPerRow,
                       planar_buffer[plane] + (ULONG)y * VIEW_BYTES_PER_ROW,
                       VIEW_BYTES_PER_ROW);
            }
        }
    }
    return 0;
}

static void wait_for_exit(struct Window *window)
{
    ULONG mask;
    int done;

    mask = 1UL << window->UserPort->mp_SigBit;
    done = 0;
    while (!done) {
        struct IntuiMessage *message;

        Wait(mask);
        while ((message = (struct IntuiMessage *)GetMsg(window->UserPort)) != 0) {
            ULONG class_value;

            class_value = message->Class;
            ReplyMsg((struct Message *)message);

            if (class_value == IDCMP_MOUSEBUTTONS ||
                class_value == IDCMP_RAWKEY ||
                class_value == IDCMP_CLOSEWINDOW) {
                done = 1;
            }
        }
    }
}

static void drain_window_messages(struct Window *window)
{
    struct IntuiMessage *message;

    while ((message = (struct IntuiMessage *)GetMsg(window->UserPort)) != 0) {
        ReplyMsg((struct Message *)message);
    }
}

int viewer_show(const struct FitsImage *image, const char *title, char *error_text)
{
    struct Screen *screen;
    struct Window *window;
    struct NewScreen new_screen;
    struct NewWindow new_window;

    debug_step("viewer_show entered");
    if (render_image(image, error_text) != 0) {
        return -1;
    }

    debug_step("opening runtime libraries");
    if (open_runtime_libraries(error_text) != 0) {
        return -1;
    }
    debug_step("runtime libraries ready");

    debug_step("preparing NewScreen");
    new_screen.LeftEdge = 0;
    new_screen.TopEdge = 0;
    new_screen.Width = VIEW_WIDTH;
    new_screen.Height = VIEW_HEIGHT;
    new_screen.Depth = image->depth;
    new_screen.DetailPen = (1 << image->depth) - 1;
    new_screen.BlockPen = 0;
    new_screen.ViewModes = 0;
    new_screen.Type = CUSTOMSCREEN;
    new_screen.Font = 0;
    new_screen.DefaultTitle = (UBYTE *)"AmigaFITS";
    new_screen.Gadgets = 0;
    new_screen.CustomBitMap = 0;

    debug_step("calling OpenScreen");
    screen = OpenScreen(&new_screen);
    debug_step("OpenScreen returned");
    if (screen == 0) {
        sprintf(error_text, "cannot open 320x200x4 custom screen");
        close_runtime_libraries();
        return -1;
    }

    debug_step("setting colormap palette");
    set_colormap_palette(&screen->ViewPort, image->depth);

    debug_step("preparing NewWindow");
    new_window.LeftEdge = 0;
    new_window.TopEdge = 0;
    new_window.Width = VIEW_WIDTH;
    new_window.Height = VIEW_HEIGHT;
    new_window.DetailPen = (1 << image->depth) - 1;
    new_window.BlockPen = 0;
    new_window.IDCMPFlags = IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY | IDCMP_CLOSEWINDOW;
    new_window.Flags = ACTIVATE | BACKDROP | BORDERLESS | RMBTRAP;
    new_window.FirstGadget = 0;
    new_window.CheckMark = 0;
    new_window.Title = (UBYTE *)title;
    new_window.Screen = screen;
    new_window.BitMap = 0;
    new_window.MinWidth = VIEW_WIDTH;
    new_window.MinHeight = VIEW_HEIGHT;
    new_window.MaxWidth = VIEW_WIDTH;
    new_window.MaxHeight = VIEW_HEIGHT;
    new_window.Type = CUSTOMSCREEN;

    debug_step("calling OpenWindow");
    window = OpenWindow(&new_window);
    debug_step("OpenWindow returned");
    if (window == 0) {
        CloseScreen(screen);
        sprintf(error_text, "cannot open viewer window");
        close_runtime_libraries();
        return -1;
    }

    debug_step("bringing window to front");
    WindowToFront(window);
    ActivateWindow(window);
    drain_window_messages(window);
    debug_step("copying image to screen bitmap");
    if (copy_render_to_bitmap(&screen->BitMap, image->depth, error_text) != 0) {
        CloseWindow(window);
        CloseScreen(screen);
        close_runtime_libraries();
        return -1;
    }
    debug_step("calling ScreenToFront");
    ScreenToFront(screen);
    debug_step("ScreenToFront returned");
    drain_window_messages(window);
    debug_step("image drawn, waiting for input");
    wait_for_exit(window);
    debug_step("input received, closing");

    CloseWindow(window);
    CloseScreen(screen);
    close_runtime_libraries();
    debug_step("viewer_show leaving");
    return 0;
}
