#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fits.h"

#define FITS_CARD_SIZE 80
#define FITS_BLOCK_SIZE 2880
#define FITS_FIXED_LOW 190.0F
#define FITS_FIXED_HIGH 650.0F

static void progress_tick(long index, long total)
{
    long step;

    step = total / 20;
    if (step < 1) {
        step = 1;
    }
    if (index == 0 || (index + 1) % step == 0 || index + 1 == total) {
        putchar('.');
        fflush(stdout);
    }
}

static unsigned char value_to_pen(float value, int depth)
{
    float scaled;
    int pen;
    int max_pen;

    max_pen = (1 << depth) - 1;

    if (value <= FITS_FIXED_LOW) {
        return 0;
    }
    if (value >= FITS_FIXED_HIGH) {
        return (unsigned char)max_pen;
    }

    scaled = (value - FITS_FIXED_LOW) * (float)max_pen /
             (FITS_FIXED_HIGH - FITS_FIXED_LOW);
    pen = (int)(scaled + 0.5F);
    if (pen < 0) {
        pen = 0;
    } else if (pen > max_pen) {
        pen = max_pen;
    }
    return (unsigned char)pen;
}

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

int fits_load(const char *path, int depth, struct FitsImage *image, char *error_text)
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
    unsigned char *pens;

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
        (long)width * (long)height > FITS_MAX_PIXELS) {
        fclose(file);
        sprintf(error_text, "image dimensions %dx%d exceed MVP memory limit", width, height);
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
    pens = (unsigned char *)malloc((size_t)pixel_count);
    if (pens == 0) {
        fclose(file);
        sprintf(error_text, "not enough memory for image");
        return -1;
    }

    for (i = 0; i < pixel_count; i++) {
        float value;

        if (fread(bytes, 1, 4, file) != 4) {
            free(pens);
            fclose(file);
            sprintf(error_text, "FITS data is truncated");
            return -1;
        }

        if (!is_finite_bits(read_u32_be(bytes))) {
            free(pens);
            fclose(file);
            sprintf(error_text, "FITS data contains non-finite pixels");
            return -1;
        }
        value = read_float_be(bytes);
        pens[i] = value_to_pen(value, depth);
        progress_tick(i, pixel_count);
    }

    fclose(file);
    image->width = width;
    image->height = height;
    image->depth = depth;
    image->pens = pens;
    return 0;
}

void fits_free(struct FitsImage *image)
{
    if (image->pens != 0) {
        free(image->pens);
    }
    image->pens = 0;
    image->width = 0;
    image->height = 0;
    image->depth = 0;
}
