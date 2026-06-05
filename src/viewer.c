#include <stdio.h>
#include <stdlib.h>

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

static int open_runtime_libraries(char *error_text)
{
    opened_intuition = 0;
    opened_graphics = 0;

    if (IntuitionBase == 0) {
        IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 0L);
        opened_intuition = IntuitionBase != 0;
    }
    if (IntuitionBase == 0) {
        sprintf(error_text, "cannot open intuition.library");
        return -1;
    }

    if (GfxBase == 0) {
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

static void set_gray_palette(struct ViewPort *vp)
{
    int i;

    for (i = 0; i < 16; i++) {
        SetRGB4(vp, (LONG)i, (LONG)i, (LONG)i, (LONG)i);
    }
}

static void draw_image(struct RastPort *rp, const struct FitsImage *image,
                       float low, float high)
{
    int x;
    int y;
    int left;
    int top;

    left = (VIEW_WIDTH - image->width) / 2;
    top = (VIEW_HEIGHT - image->height) / 2;

    for (y = 0; y < image->height; y++) {
        for (x = 0; x < image->width; x++) {
            UBYTE pen;

            pen = pixel_to_pen(image->pixels[(long)y * image->width + x], low, high);
            SetAPen(rp, (LONG)pen);
            WritePixel(rp, (LONG)(left + x), (LONG)(top + y));
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

    if (fits_percentile_bounds(image, &low, &high) != 0) {
        sprintf(error_text, "not enough memory for contrast scale");
        return -1;
    }

    if (open_runtime_libraries(error_text) != 0) {
        return -1;
    }

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

    screen = OpenScreen(&new_screen);
    if (screen == 0) {
        sprintf(error_text, "cannot open 320x200x4 custom screen");
        close_runtime_libraries();
        return -1;
    }

    set_gray_palette(&screen->ViewPort);
    ScreenToFront(screen);

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

    window = OpenWindow(&new_window);
    if (window == 0) {
        CloseScreen(screen);
        sprintf(error_text, "cannot open viewer window");
        close_runtime_libraries();
        return -1;
    }

    WindowToFront(window);
    ActivateWindow(window);
    SetAPen(window->RPort, 0);
    RectFill(window->RPort, 0, 0, VIEW_WIDTH - 1, VIEW_HEIGHT - 1);
    draw_image(window->RPort, image, low, high);
    wait_for_exit(window);

    CloseWindow(window);
    CloseScreen(screen);
    close_runtime_libraries();
    return 0;
}
