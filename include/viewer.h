#ifndef AMIGAFITS_VIEWER_H
#define AMIGAFITS_VIEWER_H

#include "fits.h"

int viewer_show(const struct FitsImage *image, const char *title, char *error_text);

#endif
