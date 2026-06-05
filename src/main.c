#include <stdio.h>

#include "fits.h"
#include "viewer.h"

int main(int argc, char **argv)
{
    const char *path;
    struct FitsImage image;
    char error_text[FITS_STATUS_LEN];
    int result;

    path = "img.fits";
    if (argc > 1 && argv != 0 && argv[1] != 0) {
        path = argv[1];
    }

    image.width = 0;
    image.height = 0;
    image.pixels = 0;
    error_text[0] = '\0';

    result = fits_load(path, &image, error_text);
    if (result != 0) {
        printf("amigafits: %s\n", error_text);
        return 1;
    }

    result = viewer_show(&image, path, error_text);
    fits_free(&image);

    if (result != 0) {
        printf("amigafits: %s\n", error_text);
        return 1;
    }

    return 0;
}
