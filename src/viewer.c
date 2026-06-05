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
#define VIEW_DEPTH 4

static int opened_intuition;
static int opened_graphics;

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

static UBYTE pixel_to_pen(float value, float low, float high)
{
    float scaled;
    int pen;

    if (value <= low) {
        return 0;
    }
    if (value >= high) {
        return 15;
    }

    scaled = (value - low) * 15.0F / (high - low);
    pen = (int)(scaled + 0.5F);
    if (pen < 0) {
        pen = 0;
    }
    if (pen > 15) {
        pen = 15;
    }
    return (UBYTE)pen;
}

static void set_colormap_palette(struct ViewPort *vp)
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

    for (i = 0; i < 16; i++) {
        SetRGB4(vp, (LONG)i,
                (LONG)colormap[i][0],
                (LONG)colormap[i][1],
                (LONG)colormap[i][2]);
    }
}

static void draw_image_slow(struct RastPort *rp, const struct FitsImage *image,
                            float low, float high)
{
    int x;
    int y;
    long scaled_width;
    long scaled_height;
    long crop_x;
    long crop_y;

    scaled_width = VIEW_WIDTH;
    scaled_height = (long)image->height * VIEW_WIDTH / image->width;
    if (scaled_height < VIEW_HEIGHT) {
        scaled_height = VIEW_HEIGHT;
        scaled_width = (long)image->width * VIEW_HEIGHT / image->height;
    }

    crop_x = (scaled_width - VIEW_WIDTH) / 2;
    crop_y = (scaled_height - VIEW_HEIGHT) / 2;

    for (y = 0; y < VIEW_HEIGHT; y++) {
        int source_y;

        source_y = (int)(((long)y + crop_y) * image->height / scaled_height);
        if (source_y < 0) {
            source_y = 0;
        } else if (source_y >= image->height) {
            source_y = image->height - 1;
        }

        for (x = 0; x < VIEW_WIDTH; x++) {
            int source_x;

            source_x = (int)(((long)x + crop_x) * image->width / scaled_width);
            if (source_x < 0) {
                source_x = 0;
            } else if (source_x >= image->width) {
                source_x = image->width - 1;
            }

            SetAPen(rp, (LONG)pixel_to_pen(
                image->pixels[(long)source_y * image->width + source_x],
                low, high));
            WritePixel(rp, (LONG)x, (LONG)y);
        }
    }
}

static int bitmap_has_planes(struct BitMap *bitmap)
{
    int plane;

    if (bitmap == 0 || bitmap->Depth < VIEW_DEPTH) {
        return 0;
    }

    for (plane = 0; plane < VIEW_DEPTH; plane++) {
        if (bitmap->Planes[plane] == 0) {
            return 0;
        }
    }
    return 1;
}

static void draw_image(struct RastPort *rp, struct BitMap *bitmap,
                       const struct FitsImage *image, float low, float high)
{
    int x;
    int y;
    long scaled_width;
    long scaled_height;
    long crop_x;
    long crop_y;
    ULONG plane_size;
    PLANEPTR planes[VIEW_DEPTH];
    int plane;
    int source_xs[VIEW_WIDTH];
    int source_ys[VIEW_HEIGHT];

    if (!bitmap_has_planes(bitmap)) {
        draw_image_slow(rp, image, low, high);
        return;
    }

    plane_size = (ULONG)bitmap->BytesPerRow * (ULONG)bitmap->Rows;
    for (plane = 0; plane < VIEW_DEPTH; plane++) {
        planes[plane] = bitmap->Planes[plane];
        memset(planes[plane], 0, (size_t)plane_size);
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

    for (y = 0; y < VIEW_HEIGHT; y++) {
        int source_y;

        source_y = source_ys[y];

        for (x = 0; x < VIEW_WIDTH; x++) {
            int source_x;
            UBYTE pen;
            UBYTE mask;
            ULONG offset;

            source_x = source_xs[x];
            pen = pixel_to_pen(image->pixels[(long)source_y * image->width + source_x],
                               low, high);
            mask = (UBYTE)(0x80 >> (x & 7));
            offset = (ULONG)y * bitmap->BytesPerRow + (ULONG)(x >> 3);

            if (pen & 1) {
                planes[0][offset] |= mask;
            }
            if (pen & 2) {
                planes[1][offset] |= mask;
            }
            if (pen & 4) {
                planes[2][offset] |= mask;
            }
            if (pen & 8) {
                planes[3][offset] |= mask;
            }
        }
    }
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

int viewer_show(const struct FitsImage *image, const char *title, char *error_text)
{
    struct Screen *screen;
    struct Window *window;
    struct NewScreen new_screen;
    struct NewWindow new_window;
    float low;
    float high;

    debug_step("viewer_show entered");
    debug_step("calculating percentile contrast");
    if (fits_percentile_bounds(image, &low, &high) != 0) {
        sprintf(error_text, "not enough memory for contrast scale");
        return -1;
    }
    printf("amigafits: debug: contrast low=%ld high=%ld\n", (long)low, (long)high);
    fflush(stdout);

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
    new_screen.Depth = VIEW_DEPTH;
    new_screen.DetailPen = 15;
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
    set_colormap_palette(&screen->ViewPort);
    debug_step("calling ScreenToFront");
    ScreenToFront(screen);
    debug_step("ScreenToFront returned");

    debug_step("preparing NewWindow");
    new_window.LeftEdge = 0;
    new_window.TopEdge = 0;
    new_window.Width = VIEW_WIDTH;
    new_window.Height = VIEW_HEIGHT;
    new_window.DetailPen = 15;
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
    debug_step("clearing screen");
    SetAPen(window->RPort, 0);
    RectFill(window->RPort, 0, 0, VIEW_WIDTH - 1, VIEW_HEIGHT - 1);
    debug_step("drawing image");
    draw_image(window->RPort, &screen->BitMap, image, low, high);
    debug_step("image drawn, waiting for input");
    wait_for_exit(window);
    debug_step("input received, closing");

    CloseWindow(window);
    CloseScreen(screen);
    close_runtime_libraries();
    debug_step("viewer_show leaving");
    return 0;
}
