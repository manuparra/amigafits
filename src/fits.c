#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fits.h"

#define FITS_CARD_SIZE 80
#define FITS_BLOCK_SIZE 2880
#define FITS_HISTOGRAM_BINS 512
#define FITS_LOW_PERCENTILE_NUM 10
#define FITS_HIGH_PERCENTILE_NUM 995
#define FITS_PERCENTILE_DEN 1000

static int starts_with(const char *card, const char *key)
{
    return strncmp(card, key, strlen(key)) == 0;
}

static int card_int_value(const char *card, int *value)
{
    const char *equals;

    equals = strchr(card, '=');
    if (equals == 0) {
        return -1;
    }

    *value = atoi(equals + 1);
    return 0;
}

static unsigned long read_u32_be(const unsigned char *bytes)
{
    return ((unsigned long)bytes[0] << 24) |
           ((unsigned long)bytes[1] << 16) |
           ((unsigned long)bytes[2] << 8) |
           (unsigned long)bytes[3];
}

static float read_float_be(const unsigned char *bytes)
{
    union {
        unsigned long bits;
        float value;
    } conv;

    conv.bits = read_u32_be(bytes);
    return conv.value;
}

static int is_finite_bits(unsigned long bits)
{
    return (bits & 0x7F800000UL) != 0x7F800000UL;
}

int fits_load(const char *path, struct FitsImage *image, char *error_text)
{
    FILE *file;
    char card[FITS_CARD_SIZE + 1];
    long header_bytes;
    int card_count;
    int bitpix;
    int naxis;
    int width;
    int height;
    int saw_simple;
    int saw_end;
    long pixel_count;
    long i;
    unsigned char bytes[4];
    float *pixels;

    file = fopen(path, "rb");
    if (file == 0) {
        sprintf(error_text, "cannot open %s", path);
        return -1;
    }

    bitpix = 0;
    naxis = 0;
    width = 0;
    height = 0;
    saw_simple = 0;
    saw_end = 0;
    card_count = 0;

    while (fread(card, 1, FITS_CARD_SIZE, file) == FITS_CARD_SIZE) {
        card[FITS_CARD_SIZE] = '\0';
        card_count++;

        if (starts_with(card, "SIMPLE")) {
            saw_simple = 1;
        } else if (starts_with(card, "BITPIX")) {
            card_int_value(card, &bitpix);
        } else if (starts_with(card, "NAXIS1")) {
            card_int_value(card, &width);
        } else if (starts_with(card, "NAXIS2")) {
            card_int_value(card, &height);
        } else if (starts_with(card, "NAXIS ")) {
            card_int_value(card, &naxis);
        } else if (starts_with(card, "END")) {
            saw_end = 1;
            break;
        }
    }

    if (!saw_end) {
        fclose(file);
        sprintf(error_text, "FITS header has no END card");
        return -1;
    }

    if (!saw_simple || bitpix != -32 || naxis != 2) {
        fclose(file);
        sprintf(error_text, "unsupported FITS format");
        return -1;
    }

    if (width <= 0 || height <= 0 ||
        width > FITS_MAX_WIDTH || height > FITS_MAX_HEIGHT) {
        fclose(file);
        sprintf(error_text, "image dimensions %dx%d do not fit 320x200", width, height);
        return -1;
    }

    header_bytes = ((long)card_count * FITS_CARD_SIZE + FITS_BLOCK_SIZE - 1) /
                   FITS_BLOCK_SIZE * FITS_BLOCK_SIZE;
    if (fseek(file, header_bytes, SEEK_SET) != 0) {
        fclose(file);
        sprintf(error_text, "cannot seek FITS data");
        return -1;
    }

    pixel_count = (long)width * (long)height;
    pixels = (float *)malloc((size_t)pixel_count * sizeof(float));
    if (pixels == 0) {
        fclose(file);
        sprintf(error_text, "not enough memory for image");
        return -1;
    }

    for (i = 0; i < pixel_count; i++) {
        float value;

        if (fread(bytes, 1, 4, file) != 4) {
            free(pixels);
            fclose(file);
            sprintf(error_text, "FITS data is truncated");
            return -1;
        }

        if (!is_finite_bits(read_u32_be(bytes))) {
            free(pixels);
            fclose(file);
            sprintf(error_text, "FITS data contains non-finite pixels");
            return -1;
        }
        value = read_float_be(bytes);
        pixels[i] = value;
    }

    fclose(file);
    image->width = width;
    image->height = height;
    image->pixels = pixels;
    return 0;
}

void fits_free(struct FitsImage *image)
{
    if (image->pixels != 0) {
        free(image->pixels);
    }
    image->pixels = 0;
    image->width = 0;
    image->height = 0;
}

int fits_percentile_bounds(const struct FitsImage *image, float *low, float *high)
{
    long pixel_count;
    long i;
    float min_value;
    float max_value;
    float range;
    unsigned int histogram[FITS_HISTOGRAM_BINS];
    unsigned long low_target;
    unsigned long high_target;
    unsigned long seen;
    int bin;

    pixel_count = (long)image->width * (long)image->height;

    if (pixel_count <= 0) {
        return -1;
    }

    min_value = image->pixels[0];
    max_value = image->pixels[0];
    for (i = 1; i < pixel_count; i++) {
        float value;

        value = image->pixels[i];
        if (value < min_value) {
            min_value = value;
        }
        if (value > max_value) {
            max_value = value;
        }
    }

    if (max_value <= min_value) {
        *low = min_value;
        *high = min_value + 1.0F;
        return 0;
    }

    memset(histogram, 0, sizeof(histogram));
    range = max_value - min_value;

    for (i = 0; i < pixel_count; i++) {
        float scaled;

        scaled = (image->pixels[i] - min_value) *
                 (float)(FITS_HISTOGRAM_BINS - 1) / range;
        bin = (int)scaled;
        if (bin < 0) {
            bin = 0;
        } else if (bin >= FITS_HISTOGRAM_BINS) {
            bin = FITS_HISTOGRAM_BINS - 1;
        }
        histogram[bin]++;
    }

    low_target = ((unsigned long)pixel_count * FITS_LOW_PERCENTILE_NUM) /
                 FITS_PERCENTILE_DEN;
    high_target = ((unsigned long)pixel_count * FITS_HIGH_PERCENTILE_NUM) /
                  FITS_PERCENTILE_DEN;
    if (high_target >= (unsigned long)pixel_count) {
        high_target = (unsigned long)pixel_count - 1;
    }

    seen = 0;
    *low = min_value;
    for (bin = 0; bin < FITS_HISTOGRAM_BINS; bin++) {
        seen += histogram[bin];
        if (seen > low_target) {
            *low = min_value + range * (float)bin /
                   (float)(FITS_HISTOGRAM_BINS - 1);
            break;
        }
    }

    seen = 0;
    *high = max_value;
    for (bin = 0; bin < FITS_HISTOGRAM_BINS; bin++) {
        seen += histogram[bin];
        if (seen > high_target) {
            *high = min_value + range * (float)bin /
                    (float)(FITS_HISTOGRAM_BINS - 1);
            break;
        }
    }

    if (*high <= *low) {
        *high = *low + 1.0F;
    }
    return 0;
}
