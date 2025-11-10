/* X3F_OUTPUT_JPEG.H
 *
 * Library for writing RAW image as JPEG with full processing.
 *
 * Copyright 2025 - weaming garden.yuen@gmail.com
 * BSD-style - see doc/copyright.txt
 *
 */

#ifndef X3F_OUTPUT_JPEG_H
#define X3F_OUTPUT_JPEG_H

#include "x3f_io.h"
#include "x3f_process.h"

extern x3f_return_t x3f_dump_raw_data_as_jpeg(x3f_t *x3f, char *outfilename,
					      int fix_bad,
					      int denoise,
					      int apply_sgain,
					      char *wb,
					      int linear_srgb,
					      x3f_color_encoding_t color_encoding);

#endif
