#include <stdio.h>
#include <stdlib.h>

#include "fits.h"
#include "viewer.h"

int main(int argc, char **argv)
{
    const char *path;
    struct FitsImage image;
    char error_text[FITS_STATUS_LEN];
    int result;
    int depth;

    path = "img.fits";
    depth = 4;
    if (argc > 1 && argv != 0 && argv[1] != 0) {
        path = argv[1];
    }
    if (argc > 2 && argv != 0 && argv[2] != 0) {
        depth = atoi(argv[2]);
    }
    if (depth < 1 || depth > 4) {
        printf("amigafits: depth must be between 1 and 4\n");
        return 1;
    }

    image.width = 0;
    image.height = 0;
    image.depth = depth;
    image.pens = 0;
    error_text[0] = '\0';

    printf("amigafits: loading %s at depth %d [", path, depth);
    fflush(stdout);
    result = fits_load(path, depth, &image, error_text);
    if (result != 0) {
        printf("]\n");
        printf("amigafits: %s\n", error_text);
        return 1;
    }
    printf("]\n");

    printf("amigafits: loaded %dx%d from %s, %d colors\n",
           image.width, image.height, path, 1 << image.depth);
    printf("amigafits: opening graphics screen; press any key or mouse button to exit\n");

    result = viewer_show(&image, path, error_text);
    fits_free(&image);

    if (result != 0) {
        printf("amigafits: %s\n", error_text);
        return 1;
    }

    return 0;
}
