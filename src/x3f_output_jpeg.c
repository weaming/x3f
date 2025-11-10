/* X3F_OUTPUT_JPEG.C
 *
 * Library for writing RAW image as JPEG with full processing pipeline:
 * - White balance
 * - Tone mapping (ACES filmic)
 * - Gamma correction (sRGB)
 * - Unsharp mask sharpening
 *
 * Copyright 2025 - weaming garden.yuen@gmail.com
 * BSD-style - see doc/copyright.txt
 *
 */

#include "x3f_output_jpeg.h"
#include "x3f_process.h"
#include "x3f_meta.h"
#include "x3f_matrix.h"
#include "x3f_printf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <jpeglib.h>

/* Helper functions for image processing */

static inline float clamp_float(float v, float min, float max) {
  if (v < min) return min;
  if (v > max) return max;
  return v;
}

static inline uint8_t float_to_uint8(float v) {
  return (uint8_t)(clamp_float(v, 0.0f, 1.0f) * 255.0f);
}

/* Comparison function for qsort */
static int compare_float(const void *a, const void *b) {
  float fa = *(const float*)a;
  float fb = *(const float*)b;
  return (fa > fb) - (fa < fb);
}

/* ACES Filmic tone mapping
 * This is the Academy Color Encoding System tone mapping curve
 * used in film and provides natural-looking results
 */
static float aces_tonemap(float x) {
  float a = 2.51f;
  float b = 0.03f;
  float c = 2.43f;
  float d = 0.59f;
  float e = 0.14f;

  float numerator = x * (a * x + b);
  float denominator = x * (c * x + d) + e;

  return clamp_float(numerator / denominator, 0.0f, 1.0f);
}

/* Calculate auto exposure based on image histogram
 * Target brightness: 0.18 (18% gray, photographic standard)
 */
static float calculate_auto_exposure(float *image, int width, int height) {
  int total_pixels = width * height;
  int i;
  float target_brightness = 0.18f;

  /* Calculate luminance using Rec. 709 coefficients */
  float *luminance = (float *)malloc(total_pixels * sizeof(float));
  if (!luminance) return 1.0f;

  for (i = 0; i < total_pixels; i++) {
    float r = image[i * 3 + 0];
    float g = image[i * 3 + 1];
    float b = image[i * 3 + 2];
    luminance[i] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
  }

  /* Sort luminance values using qsort for efficiency */
  qsort(luminance, total_pixels, sizeof(float), compare_float);

  /* Calculate mean brightness of middle 90% (skip brightest and darkest 5%) */
  int lower_idx = (int)(total_pixels * 0.05f);
  int upper_idx = (int)(total_pixels * 0.95f);
  float sum = 0.0f;
  int count = 0;

  for (i = lower_idx; i < upper_idx; i++) {
    sum += luminance[i];
    count++;
  }

  float current_brightness = count > 0 ? sum / count : 0.18f;

  free(luminance);

  /* Calculate exposure compensation */
  float exposure = current_brightness > 0.001f ? target_brightness / current_brightness : 1.0f;

  /* Clamp to reasonable range */
  if (exposure < 0.3f) exposure = 0.3f;
  if (exposure > 5.0f) exposure = 5.0f;

  x3f_printf(INFO, "Auto exposure: current brightness=%.4f, target=%.4f, exposure=%.3f\n",
             current_brightness, target_brightness, exposure);

  return exposure;
}

/* Apply tone mapping to RGB image */
static void apply_tone_mapping(float *image, int width, int height, float exposure) {
  int total_pixels = width * height;
  int i;

  for (i = 0; i < total_pixels * 3; i++) {
    /* Apply exposure */
    float v = image[i] * exposure;

    /* Apply ACES tone mapping */
    image[i] = aces_tonemap(v);
  }
}

/* Apply gamma correction based on color encoding
 * SRGB: gamma 2.2 (actually sRGB uses a piecewise function, but 2.2 is close)
 * ARGB (Adobe RGB): gamma 2.2
 * PPRGB (ProPhoto RGB): gamma 1.8
 * NONE: gamma 2.2 (default)
 */
static void apply_gamma_correction(float *image, int width, int height, x3f_color_encoding_t color_encoding) {
  int total_pixels = width * height;
  int i;
  float gamma;

  /* Select gamma based on color encoding */
  if (color_encoding == PPRGB) {
    gamma = 1.0f / 1.8f;  /* ProPhoto RGB uses gamma 1.8 */
  } else {
    gamma = 1.0f / 2.2f;  /* sRGB, Adobe RGB, and others use gamma 2.2 */
  }

  for (i = 0; i < total_pixels * 3; i++) {
    image[i] = powf(image[i], gamma);
  }
}

/* Gaussian blur for sharpening */
static float* gaussian_blur(float *input, int width, int height, int channel, float sigma) {
  float *output = (float *)malloc(width * height * sizeof(float));
  int kernel_size = (int)(sigma * 3.0f) * 2 + 1;
  int kernel_half = kernel_size / 2;
  float *kernel = (float *)malloc(kernel_size * sizeof(float));
  float sum = 0.0f;
  int i, j, x, y;

  /* Generate 1D Gaussian kernel */
  for (i = 0; i < kernel_size; i++) {
    int offset = i - kernel_half;
    kernel[i] = expf(-(offset * offset) / (2.0f * sigma * sigma));
    sum += kernel[i];
  }

  /* Normalize kernel */
  for (i = 0; i < kernel_size; i++) {
    kernel[i] /= sum;
  }

  /* Horizontal pass */
  float *temp = (float *)malloc(width * height * sizeof(float));
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      float val = 0.0f;
      for (i = 0; i < kernel_size; i++) {
        int xx = x + i - kernel_half;
        if (xx < 0) xx = 0;
        if (xx >= width) xx = width - 1;
        val += input[(y * width + xx) * 3 + channel] * kernel[i];
      }
      temp[y * width + x] = val;
    }
  }

  /* Vertical pass */
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      float val = 0.0f;
      for (j = 0; j < kernel_size; j++) {
        int yy = y + j - kernel_half;
        if (yy < 0) yy = 0;
        if (yy >= height) yy = height - 1;
        val += temp[yy * width + x] * kernel[j];
      }
      output[y * width + x] = val;
    }
  }

  free(kernel);
  free(temp);
  return output;
}

/* Apply unsharp mask sharpening */
static void apply_sharpening(float *image, int width, int height, float strength) {
  float sigma = 1.0f;
  int c;

  for (c = 0; c < 3; c++) {
    float *blurred = gaussian_blur(image, width, height, c, sigma);
    int i;

    for (i = 0; i < width * height; i++) {
      float original = image[i * 3 + c];
      float blur = blurred[i];
      float high_freq = original - blur;
      float sharpened = original + strength * high_freq;
      image[i * 3 + c] = clamp_float(sharpened, 0.0f, 1.0f);
    }

    free(blurred);
  }
}

/* Main function to convert RAW to JPEG */
x3f_return_t x3f_dump_raw_data_as_jpeg(x3f_t *x3f,
					char *outfilename,
					int fix_bad,
					int denoise,
					int apply_sgain,
					char *wb,
					int linear_srgb,
					x3f_color_encoding_t color_encoding)
{
  x3f_area16_t image;
  x3f_image_levels_t ilevels;
  float *float_image;
  uint8_t *output_image;
  int i;
  FILE *outfile;
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  JSAMPROW row_pointer[1];
  const char *colorspace_name;

  /* Get colorspace name for logging */
  switch (color_encoding) {
    case SRGB: colorspace_name = "sRGB"; break;
    case ARGB: colorspace_name = "AdobeRGB"; break;
    case PPRGB: colorspace_name = "ProPhotoRGB"; break;
    case NONE: colorspace_name = "none"; break;
    default: colorspace_name = "sRGB"; break;
  }

  x3f_printf(INFO, "Using color encoding: %s\n", colorspace_name);

  /* Get white balance */
  if (wb == NULL) wb = x3f_get_wb(x3f);

  /* Get RAW image data (linear, no color conversion yet) */
  if (!x3f_get_image(x3f, &image, &ilevels, NONE, 1,
		     fix_bad, denoise, apply_sgain, wb) ||
      image.channels != 3) {
    x3f_printf(ERR, "Could not get image\n");
    return X3F_ARGUMENT_ERROR;
  }

  x3f_printf(INFO, "Image size: %dx%d\n", image.columns, image.rows);

  /* Apply white balance and color conversion via matrix */
  {
    int row, col, color;
    double raw_to_xyz[9], raw_to_target[9];

    x3f_printf(INFO, "Applying white balance and color conversion (linear space)\n");

    /* Get raw_to_xyz matrix which includes white balance gain */
    if (!x3f_get_raw_to_xyz(x3f, wb, raw_to_xyz)) {
      x3f_printf(ERR, "Could not get raw_to_xyz for white balance: %s\n", wb);
      free(image.buf);
      return X3F_ARGUMENT_ERROR;
    }

    /* Direct conversion: raw -> XYZ(D65) -> target color space */
    if (color_encoding == SRGB) {
      double xyz_to_srgb[9];
      x3f_XYZ_to_sRGB(xyz_to_srgb);
      x3f_3x3_3x3_mul(xyz_to_srgb, raw_to_xyz, raw_to_target);
      x3f_printf(INFO, "Converting raw -> XYZ(D65) -> sRGB\n");
    } else if (color_encoding == ARGB) {
      double xyz_to_adobe[9];
      x3f_XYZ_to_AdobeRGB(xyz_to_adobe);
      x3f_3x3_3x3_mul(xyz_to_adobe, raw_to_xyz, raw_to_target);
      x3f_printf(INFO, "Converting raw -> XYZ(D65) -> Adobe RGB\n");
    } else {
      /* ProPhoto RGB: need to go through D50 */
      double xyz_to_prophoto[9], d65_to_d50[9], xyz_d50_to_prophoto[9];
      x3f_XYZ_to_ProPhotoRGB(xyz_to_prophoto);
      x3f_Bradford_D65_to_D50(d65_to_d50);
      x3f_3x3_3x3_mul(xyz_to_prophoto, d65_to_d50, xyz_d50_to_prophoto);
      x3f_3x3_3x3_mul(xyz_d50_to_prophoto, raw_to_xyz, raw_to_target);
      x3f_printf(INFO, "Converting raw -> XYZ(D65) -> XYZ(D50) -> ProPhoto RGB\n");
    }

    /* Allocate float image buffer */
    float_image = (float *)malloc(image.rows * image.columns * 3 * sizeof(float));
    if (!float_image) {
      x3f_printf(ERR, "Could not allocate memory for float image\n");
      free(image.buf);
      return X3F_INTERNAL_ERROR;
    }

    /* Apply color matrix to each pixel and convert to float [0, 1] */
    for (row = 0; row < image.rows; row++) {
      for (col = 0; col < image.columns; col++) {
        uint16_t *src_pixel = &image.data[image.row_stride*row + image.channels*col];
        float *dst_pixel = &float_image[(row * image.columns + col) * 3];
        double input[3], output[3];

        /* Normalize to [0, 1] based on black and white levels */
        for (color = 0; color < 3; color++) {
          input[color] = (src_pixel[color] - ilevels.black[color]) /
                        (ilevels.white[color] - ilevels.black[color]);
        }

        /* Apply color matrix: raw -> target color space */
        x3f_3x3_3x1_mul(raw_to_target, input, output);

        /* Store as float, clamping to [0, 1] */
        for (color = 0; color < 3; color++) {
          dst_pixel[color] = clamp_float(output[color], 0.0f, 1.0f);
        }
      }
    }

    x3f_printf(INFO, "Color matrix applied - data is now linear %s with WB\n", colorspace_name);
  }

  /* Calculate auto exposure */
  x3f_printf(INFO, "Calculating auto exposure...\n");
  float auto_exposure = calculate_auto_exposure(float_image, image.columns, image.rows);

  /* Apply tone mapping (ACES filmic) with auto exposure */
  x3f_printf(INFO, "Applying ACES tone mapping with exposure %.3f...\n", auto_exposure);
  apply_tone_mapping(float_image, image.columns, image.rows, auto_exposure);

  /* Apply gamma correction based on color encoding */
  x3f_printf(INFO, "Applying gamma correction for %s...\n", colorspace_name);
  apply_gamma_correction(float_image, image.columns, image.rows, color_encoding);

  /* Apply sharpening */
  x3f_printf(INFO, "Applying sharpening...\n");
  apply_sharpening(float_image, image.columns, image.rows, 1.2f);

  /* Convert to 8-bit */
  output_image = (uint8_t *)malloc(image.rows * image.columns * 3);
  if (!output_image) {
    x3f_printf(ERR, "Could not allocate memory for output image\n");
    free(float_image);
    free(image.buf);
    return X3F_INTERNAL_ERROR;
  }

  for (i = 0; i < image.rows * image.columns * 3; i++) {
    output_image[i] = float_to_uint8(float_image[i]);
  }

  /* Write JPEG */
  outfile = fopen(outfilename, "wb");
  if (!outfile) {
    x3f_printf(ERR, "Could not open output file: %s\n", outfilename);
    free(output_image);
    free(float_image);
    free(image.buf);
    return X3F_OUTFILE_ERROR;
  }

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, outfile);

  cinfo.image_width = image.columns;
  cinfo.image_height = image.rows;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 98, TRUE);

  jpeg_start_compress(&cinfo, TRUE);

  while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer[0] = &output_image[cinfo.next_scanline * image.columns * 3];
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  fclose(outfile);

  /* Clean up */
  free(output_image);
  free(float_image);
  free(image.buf);

  x3f_printf(INFO, "JPEG written successfully\n");

  return X3F_OK;
}
