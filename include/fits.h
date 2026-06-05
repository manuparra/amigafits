#ifndef AMIGAFITS_FITS_H
#define AMIGAFITS_FITS_H

#define FITS_MAX_WIDTH 320
#define FITS_MAX_HEIGHT 200
#define FITS_STATUS_LEN 160

struct FitsImage {
    int width;
    int height;
    float *pixels;
};

int fits_load(const char *path, struct FitsImage *image, char *error_text);
void fits_free(struct FitsImage *image);
int fits_percentile_bounds(const struct FitsImage *image, float *low, float *high);

#endif
