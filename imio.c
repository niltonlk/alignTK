/*
 * imio.c  - image and map I/O routines
 *
 *  Copyright (c) 2006-2015 Pittsburgh Supercomputing Center,
 *                          Carnegie Mellon University
 *
 *  Distribution of this code is prohibited without the prior written
 *  consent of the National Resource for Biomedical Supercomputing.
 *
 *  Acknowledgements:
 *     Development of this code was supported in part by
 *       NIH NCRR grant 5P41RR006009 and
 *       NIH NIGMS grant P41GM103712
 *
 *  This program is distributed in the hope that it will
 *  be useful, but WITHOUT ANY WARRANTY; without even the
 *  implied warranty of MERCHANTABILITY or FITNESS FOR A
 *  PARTICULAR PURPOSE.  Neither Carnegie Mellon University
 *  nor any of the authors assume any liability for
 *  damages, incidental or otherwise, caused by the
 *  installation or use of this software.
 *
 *  CLINICAL APPLICATIONS ARE NOT RECOMMENDED, AND THIS
 *  SOFTWARE HAS NOT BEEN EVALUATED BY THE UNITED STATES
 *  FDA FOR ANY CLINICAL USE.
 *
 *  HISTORY
 *    2023-2023  Added bmp related read funtions Nilton L Kamiji (nilton@nips.ac.jp)
 *    2006-2015  Written by Greg Hood (ghood@psc.edu)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <zlib.h>
#include <tiffio.h>
#include <jpeglib.h>
#include <stdarg.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

#include "imio.h"

#define N_EXTENSIONS	14 // change from 12 to 14 to add bmp files [Nilton]
static char *extensions[N_EXTENSIONS] = { ".tif", ".tiff", ".TIF", ".TIFF",
					  ".pgm", ".PGM", ".ppm", ".PPM",
					  ".jpg", ".jpeg", ".JPG", ".JPEG",
					  ".bmp", ".BMP"}; // add .bmp and .BMP [Nilton]
static int extension = 0;

#define N_BITMAP_EXTENSIONS	2
static char *bitmapExtensions[N_BITMAP_EXTENSIONS] = { ".pbm", ".pbm.gz"};
static int bitmapExtension = 0;
// define BMP file header structures [Nilton]
#define BMP_FILE_TYPE      0x4D42  // "BM" in little endian
#define BMP_FILE_HEADER_SIZE   14  // BMP file header size
#define BMP_INFO_HEADER_SIZE   40  // BMP info header size
#define BMP_DEFAULT_HEADER_SIZE (BMP_FILE_HEADER_SIZE + BMP_INFO_HEADER_SIZE)
typedef struct BMPFILEHEADER {
  uint16_t bfType;     // BMP file type, usualy "BM"
  uint32_t bfSize;     // BMP file size (bytes)
  uint32_t bfReserved; // Reserved
  uint32_t bfOffset;   // Offset to Data
} BMPFILEHEADER;
typedef struct BMPINFOHEADER {
  uint32_t biSize;           // Size of header fixed to 40
  int32_t  biWidth;          // Image Width
  int32_t  biHeight;         // Image Height
  uint16_t biPlanes;         // Number of image planes 1
  uint16_t biBitCount;       // Number of bits per pixel
  uint32_t biCompression;    // Image compression method
  uint32_t biImageSize;      // Image Size; Can be zero when compressed
  int32_t  biPixPerMeterX;   // Resolution in X direction in Pixels per meter (3780 indicates 96 dpi)
  int32_t  biPixPerMeterY;   // Resolution in Y direction in Pixels per meter (3780 indicates 96 dpi)
  uint32_t biColorUsed;      // Number of colors used from pallete
  uint32_t biColorImportant; // Number of important colors
} BMPINFOHEADER;
// define BMP file header structures [Nilton]
int ReadTiffImageSize (const char *filename, int *width, int *height,
		       char *error);
int ReadPgmImageSize (const char *filename, int *width, int *height,
		      char *error);
int ReadPpmImageSize (const char *filename, int *width, int *height,
		      char *error);
int ReadJpgImageSize (const char *filename, int *width, int *height,
		      char *error);
int ReadBmpImageSize (const char *filename, int *width, int *height,
		      char *error); // add ReadBmpImageSize [Nilton]
int ReadTiffImage (char *filename, unsigned char **buffer,
		   int *width, int *height,
		   char *error);
int ReadPgmImage (char *filename, unsigned char **buffer,
		  int *width, int *height,
		  char *error);
int ReadJpgImage (char *filename, unsigned char **buffer,
		  int *width, int *height,
		  char *error);
int ReadBmpImage (char *filename, unsigned char **buffer,
		  int *width, int *height, char *error); // add ReadBmpImage [Nilton]
int ReadHeader (FILE *f, char *tc, uint32 *w, uint32 *h, int *m);

int ReadPbmBitmapSize (const char *filename, int *width, int *height,
		       char *error);
int ReadPbmgzBitmapSize (const char *filename, int *width, int *height,
			 char *error);
int ReadPbmBitmap (char *filename, unsigned char **buffer,
		   int *width, int *height,
		   char *error);
int ReadPbmgzBitmap (char *filename, unsigned char **buffer,
		     int *width, int *height,
		     char *error);

int
ReadImageSize (char *filename,
	       int *width, int *height,
	       char *error)
{
  int len;
  int i, j, k;
  char fn[PATH_MAX];
  struct stat sb;

  len = strlen(filename);
  if (len == 0)
    {
      sprintf(error, "Image filename is empty.\n");
      return(0);
    }
  if (len > 4 && strcasecmp(&filename[len-4], ".tif") == 0 ||
      len > 5 && strcasecmp(&filename[len-5], ".tiff") == 0)
    return(ReadTiffImageSize(filename, width, height, error));
  else if (len > 4 && strcasecmp(&filename[len-4], ".pgm") == 0)
    return(ReadPgmImageSize(filename, width, height, error));
  else if (len > 4 && strcasecmp(&filename[len-4], ".ppm") == 0)
    return(ReadPpmImageSize(filename, width, height, error));
  else if (len > 4 && strcasecmp(&filename[len-4], ".jpg") == 0 ||
	   len > 5 && strcasecmp(&filename[len-5], ".jpeg") == 0)
    return(ReadJpgImageSize(filename, width, height, error));
  else if (len > 4 && strcasecmp(&filename[len-4], ".bmp") == 0)
    return(ReadBmpImageSize(filename, width, height, error)); // Add ReadBmpImageSize [Nilton]
  else
    {
      j = extension;
      for (i = 0; i < N_EXTENSIONS; ++i)
	{
	  k = (j + i) % N_EXTENSIONS;
	  sprintf(fn, "%s%s", filename, extensions[k]);
	  if (stat(fn, &sb) != 0)
	    continue;
	  switch (k)
	    {
	    case 0:
	    case 1:
	    case 2:
	    case 3:
	      if (!ReadTiffImageSize(fn, width, height, error))
		return(0);
	      break;

	    case 4:
	    case 5:
	      if (!ReadPgmImageSize(fn, width, height, error))
		return(0);
	      break;

	    case 6:
	    case 7:
	      if (!ReadPpmImageSize(fn, width, height, error))
		return(0);
	      break;

	    case 8:
	    case 9:
	    case 10:
	    case 11:
	      if (!ReadJpgImageSize(fn, width, height, error))
		return(0);
	      break;
	    case 12:
	    case 13:
	      if (!ReadBmpImageSize(fn, width, height, error)) // Add ReadBmpImageSize [Nilton]
		return(0);
	      break;
	    }
	  extension = k;
	  break;
	}
      if (i >= N_EXTENSIONS)
	{
	  sprintf(error, "Cannot find image file with basename %s\n",
		  filename);
	  return(0);
	}
    }
  return(1);
}

int
ReadTiffImageSize (const char *filename, int *width, int *height, char *error)
{
  TIFF *image;
  uint32 iw, ih;

  // Open the TIFF image
  if ((image = TIFFOpen(filename, "r")) == NULL)
    {
      sprintf(error, "Could not open TIFF image: %s\n", filename);
      return(0);
    }
  if (TIFFGetField(image, TIFFTAG_IMAGEWIDTH, &iw) == 0)
    {
      sprintf(error, "TIFF image %s does not define its width\n",
	      filename);
      return(0);
    }
  if (TIFFGetField(image, TIFFTAG_IMAGELENGTH, &ih) == 0)
    {
      sprintf(error, "TIFF image %s does not define its height (length)\n",
	      filename);
      return(0);
    }
  TIFFClose(image);
  *width = iw;
  *height = ih;
  return(1);
}

int
ReadPgmImageSize (const char *filename, int *width, int *height, char *error)
{
  FILE *f;
  char tc;
  int m;
  uint32 iw, ih;

  f = fopen(filename, "rb");
  if (f == NULL)
    {
      sprintf(error, "Could not open file %s for reading\n",
		  filename);
      return(0);
    }
  if (!ReadHeader(f, &tc, &iw, &ih, &m))
    {
      sprintf(error, "Image file %s not binary pgm.\n", filename);
      return(0);
    }
  fclose(f);
  *width = iw;
  *height = ih;
  return(1);
}

int
ReadPpmImageSize (const char *filename, int *width, int *height, char *error)
{
  FILE *f;
  char tc;
  int m;
  uint32 iw, ih;

  f = fopen(filename, "rb");
  if (f == NULL)
    {
      sprintf(error, "Could not open file %s for reading\n",
		  filename);
      return(0);
    }
  if (!ReadHeader(f, &tc, &iw, &ih, &m))
    {
      sprintf(error, "Image file %s not binary ppm.\n", filename);
      return(0);
    }
  fclose(f);
  *width = iw;
  *height = ih;
  return(1);
}

int
ReadJpgImageSize (const char *filename, int *width, int *height, char *error)
{
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;
  FILE *f;

  if ((f = fopen(filename, "rb")) == NULL)
    {
      sprintf(error, "Could not open file %s for reading\n",
		  filename);
      return(0);
    }
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, f);
  jpeg_read_header(&cinfo, 1);
  fclose(f);
  *width = cinfo.image_width;
  *height = cinfo.image_height;
  jpeg_destroy_decompress(&cinfo);
  return(1);
}

// Add ReadBmpImageSize [Nilton]
int ReadBmpImageSize (const char *filename, int *width, int *height, char *error)
{
  struct BMPFILEHEADER file;
  struct BMPINFOHEADER info;
  FILE *f;
  
  f = fopen(filename, "rb");
  if (f == NULL)
    {
      sprintf(error, "Could not open file %s for reading\n",
		  filename);
      return(0);
    }

    if (fread(&file.bfType, sizeof(file.bfType), 1, f) != 1 ||
    file.bfType != BMP_FILE_TYPE)
    {
      sprintf(error, "Image file %s not bmp file.bfType.\n", filename);
      return(0);
    }
  if (fread(&file.bfSize, sizeof(file.bfSize), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp file.bfSize.\n", filename);
      return(0);
    }
  if (fread(&file.bfReserved, sizeof(file.bfReserved), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp file.bfReserved.\n", filename);
      return(0);
    }
  if (fread(&file.bfOffset, sizeof(file.bfOffset), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp file.bfOffset.\n", filename);
      return(0);
    }

  if (fread(&info.biSize, sizeof(info.biSize), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biSize.\n", filename);
      return(0);
    }
  if (fread(&info.biWidth, sizeof(info.biWidth), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biWidth.\n", filename);
      return(0);
    }
  if (fread(&info.biHeight, sizeof(info.biHeight), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biHeight.\n", filename);
      return(0);
    }
  if (fread(&info.biPlanes, sizeof(info.biPlanes), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biPlanes.\n", filename);
      return(0);
    }
  if (fread(&info.biBitCount, sizeof(info.biBitCount), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biBitCount.\n", filename);
      return(0);
    }
  if (fread(&info.biCompression, sizeof(info.biCompression), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biCompression.\n", filename);
      return(0);
    }
  if (fread(&info.biImageSize, sizeof(info.biImageSize), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biImageSize.\n", filename);
      return(0);
    }
  if (fread(&info.biPixPerMeterX, sizeof(info.biPixPerMeterX), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biPixPerMeterX.\n", filename);
      return(0);
    }
  if (fread(&info.biPixPerMeterY, sizeof(info.biPixPerMeterY), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biPixPerMeterY.\n", filename);
      return(0);
    }
  if (fread(&info.biColorUsed, sizeof(info.biColorUsed), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biColorUsed.\n", filename);
      return(0);
    }
  if (fread(&info.biColorImportant, sizeof(info.biColorImportant), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biColorImportant.\n", filename);
      return(0);
    }

  *width = info.biWidth;
  *height = info.biHeight;
  fclose(f);
  return(1);
}
// Add ReadBmpImageSize [Nilton]

int
ReadImage (char *filename, unsigned char **pixels,
	   int *width, int *height,
	   int minX, int maxX, int minY, int maxY,
	   char *error)
{
  int len;
  unsigned char *buffer;
  unsigned long bufferSize, count;
  unsigned char *ptr;
  int iw, ih;
  uint32 xmin, xmax, ymin, ymax;
  int w, h;
  int y;
  char fn[PATH_MAX];
  int i, j, k;
  struct stat sb;

  len = strlen(filename);
  if (len == 0)
    {
      sprintf(error, "Image filename is empty.\n");
      return(0);
    }
  if (len > 4 && strcasecmp(&filename[len-4], ".tif") == 0 ||
      len > 5 && strcasecmp(&filename[len-5], ".tiff") == 0)
    {
      if (!ReadTiffImage(filename, &buffer, &iw, &ih, error))
	return(0);
    }
  else if (len > 4 && strcasecmp(&filename[len-4], ".pgm") == 0)
    {
      if (!ReadPgmImage(filename, &buffer, &iw, &ih, error))
	return(0);
    }
  else if (len > 4 && strcasecmp(&filename[len-4], ".jpg") == 0 ||
	   len > 5 && strcasecmp(&filename[len-5], ".jpeg") == 0)
    {
      if (!ReadJpgImage(filename, &buffer, &iw, &ih, error))
	return(0);
    }
  else if (len > 4 && strcasecmp(&filename[len-4], ".bmp") == 0)
    {
      if (!ReadBmpImage(filename, &buffer, &iw, &ih, error)) // Add ReadBmpImage [Nilton]
	return(0);
    }
  else
    {
      j = extension;
      for (i = 0; i < N_EXTENSIONS; ++i)
	{
	  k = (j + i) % N_EXTENSIONS;
	  sprintf(fn, "%s%s", filename, extensions[k]);
	  if (stat(fn, &sb) != 0)
	    continue;
	  switch (k)
	    {
	    case 0:
	    case 1:
	    case 2:
	    case 3:
	      if (!ReadTiffImage(fn, &buffer, &iw, &ih, error))
		return(0);
	      break;
	    case 4:
	    case 5:
	      if (!ReadPgmImage(fn, &buffer, &iw, &ih, error))
		return(0);
	      break;
	    case 6:
	    case 7:
	      if (!ReadPpmImage(fn, &buffer, &iw, &ih, error))
		return(0);
	      break;
	    case 8:
	    case 9:
	    case 10:
	    case 11:
	      if (!ReadJpgImage(fn, &buffer, &iw, &ih, error))
		return(0);
	      break;
	    case 12:
	    case 13:
	      if (!ReadBmpImage(fn, &buffer, &iw, &ih, error)) // Add ReadBmpImage [Nilton]
		return(0);
	      break;
	    }
	  extension = k;
	  break;
	}
      if (i >= N_EXTENSIONS)
	{
	  sprintf(error, "Cannot find image file with basename %s\n",
		  filename);
	  return(0);
	}
    }

  if (minX <= 0 && (maxX < 0 || maxX == iw-1) &&
      minY <= 0 && (maxY < 0 || maxY == ih-1))
    {
      *width = iw;
      *height = ih;
      *pixels = buffer;
      return(1);
    }

  if (minX < 0)
    xmin = 0;
  else
    xmin = minX;
  if (maxX < 0)
    xmax = iw-1;
  else
    xmax = maxX;
  if (minY < 0)
    ymin = 0;
  else
    ymin = minY;
  if (maxY < 0)
    ymax = ih-1;
  else
    ymax = maxY;
  w = xmax - xmin + 1;
  h = ymax - ymin + 1;
  *width = w;
  *height = h;

  //  Log("resizing from %d %d to %d %d (%d) %d %d %d %d\n", iw, ih, w, h, w * h, xmin, xmax,
  //      ymin, ymax);
  if ((ptr = (unsigned char *) malloc(w * h)) == NULL)
    {
      free(buffer);
      sprintf(error, "Could not allocate enough memory for requested image subregion of %s\n", filename);
      return(0);
    }
  *pixels = ptr;
  for (y = ymin; y <= ymax; ++y)
    {
      if (y >= ih || xmin >= iw)
	memset(ptr, 0, w);
      else if (xmax < iw)
	memcpy(ptr, buffer + y * iw + xmin, w);
      else if (w < iw - xmin)
	memcpy(ptr, buffer + y * iw + xmin, w);
      else
	{
	  memcpy(ptr, buffer + y * iw + xmin, iw - xmin);
	  memset(ptr+iw-xmin, 0, xmax - iw + 1);
	}
      ptr += w;
    }
  free(buffer);
  return(1);
}

int
ReadTiffImage (char *filename, unsigned char **buffer,
	       int *width, int *height,
	       char *error)
{
  TIFF *image;
  uint32 iw, ih;
  uint16 photo, bps, spp, fillorder;
  tsize_t stripSize;
  unsigned long imageOffset, result;
  int stripMax, stripCount;
  unsigned long bufferSize, count;
  unsigned char *b;

  // Open the TIFF image
  if ((image = TIFFOpen(filename, "r")) == NULL)
    {
      sprintf(error, "Could not open TIFF image: %s\n", filename);
      return(0);
    }
  
  // Check that it is of a type that we support
  if (TIFFGetField(image, TIFFTAG_BITSPERSAMPLE, &bps) == 0 || bps != 8)
    {
      sprintf(error, "Either undefined or unsupported number of bits per sample (bps = %d) in tiff image %s\n", bps, filename);
      return(0);
    }
  
  if (TIFFGetField(image, TIFFTAG_SAMPLESPERPIXEL, &spp) != 0 && spp != 1)
    {
      sprintf(error, "Unsupported number of samples per pixel (spp = %d) in tiff image %s\n", spp, filename);
      return(0);
    }

  if (TIFFGetField(image, TIFFTAG_IMAGEWIDTH, &iw) == 0)
    {
      sprintf(error, "TIFF image %s does not define its width\n",
	      filename);
      return(0);
    }
  
  if (TIFFGetField(image, TIFFTAG_IMAGELENGTH, &ih) == 0)
    {
      sprintf(error, "Image %s does not define its height (length)\n",
	      filename);
      return(0);
    }
  
  // Read in the possibly multiple strips
  stripSize = TIFFStripSize(image);
  stripMax = TIFFNumberOfStrips(image);
  imageOffset = 0;

  bufferSize = stripMax * stripSize;
  //      Log("TIFF: %ld %ld %ld %d %d %d %d\n",
  //	  (long) stripSize, (long) stripMax, (long) bufferSize,
  //	  (int) bps, (int) spp, (int) iw, (int) ih);

  if ((b = (unsigned char *) malloc(bufferSize)) == NULL)
    {
      sprintf(error, "Could not allocate enough memory for the uncompressed image (bytes = %lu) of TIFF file %s\n", bufferSize, filename);
      return(0);
    }

  for (stripCount = 0; stripCount < stripMax; ++stripCount)
    {
      if ((result = TIFFReadEncodedStrip(image, stripCount,
					 b + imageOffset,
					 stripSize)) == -1)
	{
	  sprintf(error, "Read error on input strip number %d in TIFF file %s\n",
		  stripCount, filename);
	  return(0);
	}
      imageOffset += result;
    }
  
  // Deal with photometric interpretations
  if (TIFFGetField(image, TIFFTAG_PHOTOMETRIC, &photo) == 0)
    {
      sprintf(error, "TIFF file %s has an undefined photometric interpretation\n",
	      filename);
      return(0);
    }
  
  if (photo != PHOTOMETRIC_MINISBLACK)
    {
      // flip bits
      for (count = 0; count < bufferSize; ++count)
	b[count] = ~b[count];
    }
  
  TIFFClose(image);

  *buffer = b;
  *width = iw;
  *height = ih;
  return(1);
}

int
ReadPgmImage (char *filename, unsigned char **buffer,
	      int *width, int *height,
	      char *error)
{
  FILE *f;
  char tc;
  int m;
  uint32 iw, ih;
  unsigned char *b;

  f = fopen(filename, "rb");
  if (f == NULL)
    {
      sprintf(error, "Could not open file %s for reading\n",
	      filename);
      return(0);
    }
  if (!ReadHeader(f, &tc, &iw, &ih, &m))
    {
      sprintf(error, "Image file %s not binary pgm.\n", filename);
      return(0);
    }
  if ((b = (unsigned char *) malloc(iw * ih)) == NULL)
    {
      sprintf(error, "Could not allocate enough memory for image in file %s\n", filename);
      return(0);
    }
  if (fread(b, 1, iw * ih, f) != iw * ih)
    {
      sprintf(error, "Image file %s apparently truncated.\n",
	      filename);
      return(0);
    }
  fclose(f);

  *buffer = b;
  *width = iw;
  *height = ih;
  return(1);
}

int
ReadPpmImage (char *filename, unsigned char **buffer,
	      int *width, int *height,
	      char *error)
{
  FILE *f;
  char tc;
  int m;
  uint32 iw, ih;
  unsigned char *b;

  f = fopen(filename, "rb");
  if (f == NULL)
    {
      sprintf(error, "Could not open file %s for reading\n",
	      filename);
      return(0);
    }
  if (!ReadHeader(f, &tc, &iw, &ih, &m))
    {
      sprintf(error, "Image file %s not binary ppm.\n", filename);
      return(0);
    }
  if ((b = (unsigned char *) malloc(iw * ih)) == NULL)
    {
      sprintf(error, "Could not allocate enough memory for image in file %s\n", filename);
      return(0);
    }
  if (fread(b, 1, iw * ih, f) != iw * ih)
    {
      sprintf(error, "Image file %s apparently truncated.\n",
	      filename);
      return(0);
    }
  fclose(f);

  *buffer = b;
  *width = iw;
  *height = ih;
  return(1);
}

int
ReadJpgImage (char *filename, unsigned char **buffer,
	      int *width, int *height,
	      char *error)
{
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;
  FILE *f;
  uint32 iw, ih;
  unsigned char **b;
  uint32 i;
  
  if ((f = fopen(filename, "rb")) == NULL)
    {
      sprintf(error, "Could not open file %s for reading\n",
		  filename);
      return(0);
    }
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, f);
  jpeg_read_header(&cinfo, 1);
  cinfo.out_color_space = JCS_GRAYSCALE;
  jpeg_calc_output_dimensions(&cinfo);
  iw = cinfo.output_width;
  ih = cinfo.output_height;
  b = (unsigned char **) malloc(ih * sizeof(unsigned char *));
  b[0] = (unsigned char *) malloc(iw * ih * sizeof(unsigned char));
  for (i = 0; i < ih; ++i)
    b[i] = b[0] + i * iw;
  jpeg_start_decompress(&cinfo);
  if (cinfo.output_components != 1)
    {
      free(b);
      fclose(f);
      jpeg_destroy_decompress(&cinfo);
      sprintf(error, "Image file %s is not a grayscale jpg.\n", filename);
      return(0);
    }
  while (cinfo.output_scanline < cinfo.output_height)
    jpeg_read_scanlines(&cinfo, &b[cinfo.output_scanline],
			cinfo.output_height - cinfo.output_scanline);
  jpeg_finish_decompress(&cinfo);
  fclose(f);
  jpeg_destroy_decompress(&cinfo);

  *buffer = b[0];
  free(b);
  *width = iw;
  *height =  ih;
  return(1);
}

// Add ReadBmpImage [Nilton]
int
ReadBmpImage (char *filename, unsigned char **buffer,
	      int *width, int *height, char *error)
{
  FILE *f;
  BMPFILEHEADER file;
  BMPINFOHEADER info;
  int rowsize;
  unsigned char *b;
  
  f = fopen(filename, "rb");
  if (f == NULL)
    {
      sprintf(error, "Could not open file %s for reading\n",
		  filename);
      return(0);
    }
  
  if (fread(&file.bfType, sizeof(file.bfType), 1, f) != 1 ||
    file.bfType != BMP_FILE_TYPE)
    {
      sprintf(error, "Image file %s not bmp file.bfType.\n", filename);
      return(0);
    }
  if (fread(&file.bfSize, sizeof(file.bfSize), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp file.bfSize.\n", filename);
      return(0);
    }
  if (fread(&file.bfReserved, sizeof(file.bfReserved), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp file.bfReserved.\n", filename);
      return(0);
    }
  if (fread(&file.bfOffset, sizeof(file.bfOffset), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp file.bfOffset.\n", filename);
      return(0);
    }

  if (fread(&info.biSize, sizeof(info.biSize), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biSize.\n", filename);
      return(0);
    }
  if (fread(&info.biWidth, sizeof(info.biWidth), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biWidth.\n", filename);
      return(0);
    }
  if (fread(&info.biHeight, sizeof(info.biHeight), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biHeight.\n", filename);
      return(0);
    }
  if (fread(&info.biPlanes, sizeof(info.biPlanes), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biPlanes.\n", filename);
      return(0);
    }
  if (fread(&info.biBitCount, sizeof(info.biBitCount), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biBitCount.\n", filename);
      return(0);
    }
  if (info.biBitCount != 8)
    {
      sprintf(error, "Unsupported pixel size (%d) for file %s. Convert to grayscale image\n",
	      info.biBitCount, filename);
      return(0);
    }
  if (fread(&info.biCompression, sizeof(info.biCompression), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biCompression.\n", filename);
      return(0);
    }
  if (fread(&info.biImageSize, sizeof(info.biImageSize), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biImageSize.\n", filename);
      return(0);
    }
  if (fread(&info.biPixPerMeterX, sizeof(info.biPixPerMeterX), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biPixPerMeterX.\n", filename);
      return(0);
    }
  if (fread(&info.biPixPerMeterY, sizeof(info.biPixPerMeterY), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biPixPerMeterY.\n", filename);
      return(0);
    }
  if (fread(&info.biColorUsed, sizeof(info.biColorUsed), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biColorUsed.\n", filename);
      return(0);
    }
  if (fread(&info.biColorImportant, sizeof(info.biColorImportant), 1, f) != 1)
    {
      sprintf(error, "Image file %s not bmp info.biColorImportant.\n", filename);
      return(0);
    }
  /*
  if (file.bfOffset != (BMP_DEFAULT_HEADER_SIZE + info.biColorImportant * 4) ||
      info.biHeight <= 0)
    {
      printf("\n Offset = %d\n", file.bfOffset);
      printf("Color Important = %d\n", info.biColorImportant);
      printf("Compression + %d\n", info.biCompression);
      sprintf(error, "Image file %s not bmp Offset error.\n", filename);
      return(0);
    }
  */
  *width = info.biWidth;
  *height = info.biHeight;
  rowsize = (int)(ceil((info.biBitCount*(*width)/32)*4));
  
  if ((b = (unsigned char *) malloc((*width) * (*height))) == NULL)
    {
      sprintf(error, "Could not allocate enough memory for image in file %s\n", filename);
      return(0);
    }
  //printf("Offset: %d\n", file.bfOffset);
  if (fseek(f, file.bfOffset, SEEK_SET) != 0)
    {
      sprintf(error, "Could not reach image data in file %s\n", filename);
      return(0);
    }
  //printf("width: %d, height: %d, rowsize: %d\n", *width, *height, rowsize);
  for (int i=*height; i>0; i--)
    {
      if (fread(b + (i-1)*(*width), 1, (*width), f) != (*width))
	{
	  sprintf(error, "Image file %s apparently truncated.\n",
		  filename);
	  return(0);
	}
      if (fseek(f, rowsize-(*width), SEEK_CUR) != 0)
	{
	  sprintf(error, "Could not reach image data in file %s, row %d\n",
		  filename, i);
	  return(0);
	}
    }
  
  fclose(f);

  *buffer = b;
  return(1);
}
// Add ReadBmpImage [Nilton]


int
WriteImage (char *filename, unsigned char *pixels,
	    int width, int height,
	    enum ImageCompression compressionMethod,
	    char *error)
{
  int len;
  int quality;
  
  len = strlen(filename);
  if (len < 5)
    {
      sprintf(error, "Image filename is too short: %s\n", filename);
      return(0);
    }
  if (strcasecmp(&filename[len-4], ".tif") == 0 ||
      len > 5 && strcasecmp(&filename[len-5], ".tiff") == 0)
    {
      TIFF *image;
      uint32 rowsperstrip = (uint32) -1;
      int row;

      // open the TIFF output file
      if ((image = TIFFOpen(filename, "w")) == NULL)
	{
	  sprintf(error, "Could not open file %s for writing\n", filename);
	  return(0);
	}

      TIFFSetField(image, TIFFTAG_IMAGEWIDTH, (uint32) width);
      TIFFSetField(image, TIFFTAG_IMAGELENGTH, (uint32) height);
      TIFFSetField(image, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
      TIFFSetField(image, TIFFTAG_SAMPLESPERPIXEL, 1);
      TIFFSetField(image, TIFFTAG_BITSPERSAMPLE, 8);
      TIFFSetField(image, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
      TIFFSetField(image, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
      switch (compressionMethod)
	{
	case UncompressedImage:
	  TIFFSetField(image, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
	  break;
	case HDiffDeflateImage:
	  TIFFSetField(image, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE);
	  TIFFSetField(image, TIFFTAG_PREDICTOR, PREDICTOR_HORIZONTAL);
	  break;
	default:
	  sprintf(error, "Unsupported compression method: %d\n",
		  (int) compressionMethod);
	  return(0);
	}
      TIFFSetField(image, TIFFTAG_ROWSPERSTRIP,
                   TIFFDefaultStripSize(image, rowsperstrip));
      for (row = 0; row < height; ++row)
	if (TIFFWriteScanline(image, &pixels[row*width], row, 0)  <  0)
	  {
	    sprintf(error, "Could not write to tif file %s\n", filename);
	    return(0);
	  }
      TIFFClose(image);
    }
  else if (strcasecmp(&filename[len-4], ".pgm") == 0)
    {
      FILE *f = fopen(filename, "wb");
      if (f == NULL)
	{
	  sprintf(error, "Could not open file %s for writing\n",
		  filename);
	  return(0);
	}
      fprintf(f, "P5\n%d %d\n255\n", width, height);
      if (fwrite(pixels, ((size_t) width)*height, 1, f) != 1)
	{
	  sprintf(error, "Could not write to file %s\n", filename);
	  return(0);
	}
      fclose(f);
    }
  else if (strcasecmp(&filename[len-4], ".jpg") == 0)
    {
      switch (compressionMethod)
	{
	case JpegQuality95:
	  quality = 95;
	  break;
	case JpegQuality90:
	  quality = 90;
	  break;
	case JpegQuality85:
	  quality = 85;
	  break;
	case JpegQuality80:
	  quality = 80;
	  break;
	case JpegQuality75:
	  quality = 75;
	  break;
	case JpegQuality70:
	  quality = 70;
	  break;
	default:
	  sprintf(error, "Unsupported compression method for jpg output: %d\n",
		  (int) compressionMethod);
	  return(0);
	}
      return(WriteJpgImage(filename, pixels, width, height,
			   quality, error));
    }
  else
    {
      sprintf(error, "Unrecognized file extension for image file %s\n", filename);
      return(0);
    }
  return(1);
}

int
WriteJpgImage (char *filename, unsigned char *buffer,
	       int width, int height,
	       int quality,
	       char *error)
{
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  JSAMPROW row_pointer[1];        /* pointer to a single row */
  FILE *f;
  uint32 iw, ih;
  unsigned char **b;
  uint32 i;
  
  if ((f = fopen(filename, "wb")) == NULL)
    {
      sprintf(error, "Could not open file %s for writing\n",
		  filename);
      return(0);
    }
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, f);
  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 1;
  cinfo.in_color_space = JCS_GRAYSCALE;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);

  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height)
    {
      row_pointer[0] = &buffer[cinfo.next_scanline * width];
      (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
  jpeg_finish_compress(&cinfo);
  fclose(f);
  jpeg_destroy_compress(&cinfo);
  return(1);
}

int
ReadBitmapSize (char *filename,
		int *width, int *height,
		char *error)
{
  int len;
  char fn[PATH_MAX];
  int i, j, k;
  struct stat sb;

  len = strlen(filename);
  if (len == 0)
    {
      sprintf(error, "Bitmap filename is empty.\n");
      return(0);
    }
  if (len > 4 && strcasecmp(&filename[len-4], ".pbm") == 0)
    return(ReadPbmBitmapSize(filename, width, height, error));
  else if (len > 7 && strcasecmp(&filename[len-7], ".pbm.gz") == 0)
    return(ReadPbmgzBitmapSize(filename, width, height, error));
  else
    {
      j = bitmapExtension;
      for (i = 0; i < N_EXTENSIONS; ++i)
	{
	  k = (j + i) % N_BITMAP_EXTENSIONS;
	  sprintf(fn, "%s%s", filename, bitmapExtensions[k]);
	  if (stat(fn, &sb) != 0)
	    continue;
	  switch (k)
	    {
	    case 0:
	      if (!ReadPbmBitmapSize(fn, width, height, error))
		return(0);
	    case 1:
	      if (!ReadPbmgzBitmapSize(fn, width, height, error))
		return(0);
	      break;
	    default:
	      break;
	    }
	  bitmapExtension = k;
	  break;
	}
      if (i >= N_BITMAP_EXTENSIONS)
	{
	  sprintf(error, "Cannot find bitmap file with basename %s\n",
		  filename);
	  return(0);
	}
    }
  return(1);
}

int
ReadPbmBitmapSize (const char *filename,
		   int *width, int *height,
		   char *error)
{
  FILE *f;
  int iw, ih;
  char tc;
  int m;
  
  f = fopen(filename, "rb");
  if (f == NULL)
    {
      sprintf(error, "Could not open file %s for reading\n",
	      filename);
      return(0);
    }
  if (!ReadHeader(f, &tc, &iw, &ih, &m) || tc != '4')
    {
      sprintf(error, "Bitmap file not binary pbm: %s\n", filename);
      return(0);
    }
  fclose(f);
  *width = iw;
  *height = ih;
  return(1);
}

int
ReadPbmgzBitmapSize (const char *filename,
		     int *width, int *height,
		     char *error)
{
  gzFile gzf;
  int iw, ih;
  char tc;
  int m;
  
  gzf = gzopen(filename, "rb");
  if (!ReadGZHeader(gzf, &tc, &iw, &ih, &m) || tc != '4')
    {
      sprintf(error, "Mask file not binary pbm: %s\n", filename);
      return(0);
    }
  gzclose(gzf);

  *width = iw;
  *height = ih;
  return(1);
}

int
ReadBitmap (char *filename, unsigned char **bitmap,
	    int *width, int *height,
	    int minX, int maxX, int minY, int maxY,
	    char *error)
{
  int len;
  int iw, ih;
  int ibpl;
  unsigned char *buffer;
  unsigned char *ptr;
  int rbpl;
  int m;
  uint32 xmin, xmax, ymin, ymax;
  int w, h;
  int x, y;
  int i, j, k;
  char fn[PATH_MAX];
  struct stat sb;

  len = strlen(filename);
  if (len == 0)
    {
      sprintf(error, "Bitmap filename is empty.\n");
      return(0);
    }

  if (len > 4 && strcasecmp(&filename[len-4], ".pbm") == 0)
    {
      if (!ReadPbmBitmap(filename, &buffer, &iw, &ih, error))
	return(0);
    }
  else if (len > 7 && strcasecmp(&filename[len-7], ".pbm.gz") == 0)
    {
      if (!ReadPbmgzBitmap(filename, &buffer, &iw, &ih, error))
	return(0);
    }
  else
    {
      j = bitmapExtension;
      for (i = 0; i < N_BITMAP_EXTENSIONS; ++i)
	{
	  k = (j + i) % N_BITMAP_EXTENSIONS;
	  sprintf(fn, "%s%s", filename, bitmapExtensions[k]);
	  if (stat(fn, &sb) != 0)
	    continue;
	  switch (k)
	    {
	    case 0:
	      if (!ReadPbmBitmap(fn, &buffer, &iw, &ih, error))
		return(0);
	      break;
	    case 1:
	      if (!ReadPbmgzBitmap(fn, &buffer, &iw, &ih, error))
		return(0);
	      break;
	    }
	  bitmapExtension = k;
	  break;
	}
      if (i >= N_BITMAP_EXTENSIONS)
	{
	  sprintf(error, "Cannot find bitmap file with basename %s\n",
		  filename);
	  return(0);
	}
    }

  if (minX <= 0 && (maxX < 0 || maxX == iw-1) &&
      minY <= 0 && (maxY < 0 || maxY == ih-1))
    {
      *bitmap = buffer;
      *width = iw;
      *height = ih;
      return(1);
    }

  if (minX < 0)
    xmin = 0;
  else
    xmin = minX;
  if (maxX < 0)
    xmax = iw-1;
  else
    xmax = maxX;
  if (minY < 0)
    ymin = 0;
  else
    ymin = minY;
  if (maxY < 0)
    ymax = ih-1;
  else
    ymax = maxY;
  w = xmax - xmin + 1;
  h = ymax - ymin + 1;
  *width = w;
  *height = h;
  rbpl = (w + 7) >> 3;

  if ((ptr = (unsigned char *) malloc(h * rbpl)) == NULL)
    {
      free(buffer);
      sprintf(error, "Could not allocate enough memory for requested bitmap subregion of %s\n", filename);
      return(0);
    }
  *bitmap = ptr;
  ibpl = (iw + 7) >> 3;
  for (y = ymin; y < ymax; ++y)
    {
      if (y >= ih)
	memset(ptr, 0, rbpl);
      else if (xmax < iw && (xmin & 7) == 0)
	memcpy(ptr, buffer + y * ibpl + (xmin >> 3), rbpl);
      else
	{
	  memset(ptr, 0, rbpl);
	  for (x = xmin; x <= xmax; ++x)
	    if (x < iw && (buffer[y*ibpl+(x >> 3)] & (0x80 >> (x & 7))) != 0)
	      ptr[(x - xmin) >> 3] |= (0x80 >> ((x - xmin) & 7));
	}
      ptr += rbpl;
    }
  free(buffer);
  return(1);
}

int
ReadPbmBitmap (char *filename, unsigned char **buffer,
	       int *width, int *height,
	       char *error)
{
  FILE *f;
  int iw, ih;
  int m;
  char tc;
  int ibpl;

  f = fopen(filename, "rb");
  if (f == NULL)
    {
      sprintf(error, "Could not open file %s for reading\n",
	      filename);
      return(0);
    }
  if (!ReadHeader(f, &tc, &iw, &ih, &m) || tc != '4')
    {
      sprintf(error, "Mask file not binary pbm: %s\n", filename);
      return(0);
    }

  ibpl = (iw + 7) >> 3;
  if ((*buffer = (unsigned char *) malloc(ih * ibpl)) == NULL)
    {
      sprintf(error, "Could not allocate enough memory for image in file %s\n", filename);
      return(0);
    }
  if (fread(*buffer, 1, ih*ibpl, f) != ih * ibpl)
    {
      fclose(f);
      sprintf(error, "Image file %s apparently truncated.\n", filename);
      return(0);
    }
  fclose(f);
  *width = iw;
  *height = ih;
  return(1);
}

int
ReadPbmgzBitmap (char *filename, unsigned char **buffer,
		 int *width, int *height,
		 char *error)
{
  gzFile gzf;
  int iw, ih;
  int m;
  char tc;
  int ibpl;

  gzf = gzopen(filename, "rb");
  if (!ReadGZHeader(gzf, &tc, &iw, &ih, &m) || tc != '4')
    {
      sprintf(error, "Mask file not binary pbm: %s\n", filename);
      return(0);
    }

  ibpl = (iw + 7) >> 3;
  if ((*buffer = (unsigned char *) malloc(ih * ibpl)) == NULL)
    {
      sprintf(error, "Could not allocate enough memory for image in file %s\n", filename);
      return(0);
    }
  if (gzread(gzf, *buffer, ih*ibpl) != ih * ibpl)
    {
      gzclose(gzf);
      sprintf(error, "Image file %s apparently truncated.\n", filename);
      return(0);
    }
  gzclose(gzf);
  *width = iw;
  *height = ih;
  return(1);
}

int
ReadHeader (FILE *f, char *tc, uint32 *w, uint32 *h, int *m)
{
  int c;
  int v;

  c = fgetc(f);
  while (c == '#')
    {
      while ((c = fgetc(f)) != EOF && c != '\n') ;
    }
  if (c != 'P')
    return(0);
  c = fgetc(f);
  if (c != '4' && c != '5' && c != '6')
    return(0);
  *tc = c;
  c = fgetc(f);

  while (c == ' ' || c == '\t' || c == '\n' || c == '#')
    if (c == '#')
      {
	while ((c = fgetc(f)) != EOF && c != '\n') ;
      }
    else
      c = fgetc(f);
  if (c >= '0' && c <= '9')
    {
      v = 0;
      while (c != EOF && c >= '0' && c <= '9')
	{
	  v = 10 * v + (c - '0');
	  c = fgetc(f);
	}
      *w = v;
    }
  else
    return(0);

  while (c == ' ' || c == '\t' || c == '\n' || c == '#')
    if (c == '#')
      {
	while ((c = fgetc(f)) != EOF && c != '\n') ;
      }
    else
      c = fgetc(f);
  if (c >= '0' && c <= '9')
    {
      v = 0;
      while (c != EOF && c >= '0' && c <= '9')
	{
	  v = 10 * v + (c - '0');
	  c = fgetc(f);
	}
      *h = v;
    }
  else
    return(0);

  if (*tc == '4')
    return(1);

  while (c == ' ' || c == '\t' || c == '\n' || c == '#')
    if (c == '#')
      while ((c = fgetc(f)) != EOF && c != '\n') ;
    else
      c = fgetc(f);
  if (c >= '0' && c <= '9')
    {
      v = 0;
      while (c != EOF && c >= '0' && c <= '9')
	{
	  v = 10 * v + (c - '0');
	  c = fgetc(f);
	}
      *m = v;
    }
  else
    return(0);

  return(1);
}

int
ReadGZHeader (gzFile f, char *tc, unsigned int *w, unsigned int *h, int *m)
{
  int c;
  int v;

  c = gzgetc(f);
  while (c == '#')
    {
      while ((c = gzgetc(f)) != EOF && c != '\n') ;
    }
  if (c != 'P')
    return(0);
  c = gzgetc(f);
  if (c != '4' && c != '5' && c != '6')
    return(0);
  *tc = c;
  c = gzgetc(f);

  while (c == ' ' || c == '\t' || c == '\n' || c == '#')
    if (c == '#')
      {
	while ((c = gzgetc(f)) != EOF && c != '\n') ;
      }
    else
      c = gzgetc(f);
  if (c >= '0' && c <= '9')
    {
      v = 0;
      while (c != EOF && c >= '0' && c <= '9')
	{
	  v = 10 * v + (c - '0');
	  c = gzgetc(f);
	}
      *w = v;
    }
  else
    return(0);

  while (c == ' ' || c == '\t' || c == '\n' || c == '#')
    if (c == '#')
      {
	while ((c = gzgetc(f)) != EOF && c != '\n') ;
      }
    else
      c = gzgetc(f);
  if (c >= '0' && c <= '9')
    {
      v = 0;
      while (c != EOF && c >= '0' && c <= '9')
	{
	  v = 10 * v + (c - '0');
	  c = gzgetc(f);
	}
      *h = v;
    }
  else
    return(0);

  if (*tc == '4')
    return(1);

  while (c == ' ' || c == '\t' || c == '\n' || c == '#')
    if (c == '#')
      while ((c = gzgetc(f)) != EOF && c != '\n') ;
    else
      c = gzgetc(f);
  if (c >= '0' && c <= '9')
    {
      v = 0;
      while (c != EOF && c >= '0' && c <= '9')
	{
	  v = 10 * v + (c - '0');
	  c = gzgetc(f);
	}
      *m = v;
    }
  else
    return(0);

  return(1);
}

int
WriteBitmap (char *filename, unsigned char *bitmap,
	     int width, int height,
	     enum BitmapCompression compressionMethod,
	     char *error)
{
  int len;
  size_t ibpl;
  char fullFilename[PATH_MAX];
  char *dotPos;
  char *slashPos;

  len = strlen(filename);
  if (len < 1)
    {
      sprintf(error, "Bitmap filename is too short: %s\n", filename);
      return(0);
    }

  dotPos = strrchr(filename, '.');
  slashPos = strrchr(filename, '/');
  strcpy(fullFilename, filename);
  if (dotPos == NULL ||
      slashPos != NULL && dotPos < slashPos)
    strcat(fullFilename, ".pbm");
  len = strlen(fullFilename);

  if (strcasecmp(&fullFilename[len-4], ".pbm") == 0)
    {
      FILE *f;
      f = fopen(fullFilename, "wb");
      if (f == NULL)
	{
	  sprintf(error, "Could not open file %s for writing\n",
		  fullFilename);
	  return(0);
	}
      fprintf(f, "P4\n%d %d\n", width, height);
      ibpl = (width + 7) >> 3;
      if (fwrite(bitmap, ibpl * height, 1, f) != 1)
	{
	  sprintf(error, "Could not write to file %s\n", fullFilename);
	  return(0);
	}
      fclose(f);
    }
  else if (len > 7 && strcasecmp(&fullFilename[len-7], ".pbm.gz") == 0)
    {
      gzFile gzf;
      gzf = gzopen(fullFilename, "wb");

      gzprintf(gzf, "P4\n%d %d\n", width, height);
      ibpl = (width + 7) >> 3;
      if (gzwrite(gzf, bitmap, ibpl * height) != ibpl * height)
	{
	  sprintf(error, "Could not write to file %s\n", fullFilename);
	  return(0);
	}
      gzclose(gzf);
    }
  else
    {
      sprintf(error, "Unrecognized file extension for bitmap file %s\n", fullFilename);
      return(0);
    }
  return(1);
}


int ReadMap (char *filename,
	     MapElement** map,
	     int *level,
	     int *width, int *height,
	     int *xMin, int *yMin,
	     char *imageName, char *referenceName,
	     char *error)
{
  char imgName[PATH_MAX], refName[PATH_MAX];
  int mapWidth, mapHeight;

  FILE *f = fopen(filename, "rb");
  if (f == NULL)
    {
      sprintf(error, "Cannot open file %s\n", filename);
      return(0);
    }
  if (fgetc(f) != 'M' || fgetc(f) != '1' || fgetc(f) != '\n' ||
      fscanf(f, "%d%d%d%d%d%s%s",
	     level,
	     &mapWidth, &mapHeight,
	     xMin, yMin,
	     imgName, refName) != 7 ||
      fgetc(f) != '\n')
    {
      sprintf(error, "Cannot read header of map file %s\n", filename);
      return(0);
    }
  if (imageName != NULL)
    strcpy(imageName, imgName);
  if (referenceName != NULL)
    strcpy(referenceName, refName);
  *map = (MapElement *) malloc(mapWidth * mapHeight * sizeof(MapElement));
  if (*map == NULL)
    {
      sprintf(error, "Could not allocate space for map %s\n", filename);
      return(0);
    }
  *width = mapWidth;
  *height = mapHeight;
  if (fread(*map, sizeof(MapElement), mapWidth * mapHeight, f) != mapWidth * mapHeight)
    {
      sprintf(error, "Could not read map from file %s\n", filename);
      return(0);
    }
  fclose(f);
  return(1);
}

int
WriteMap (char *filename, MapElement *map,
	  int level,
	  int width, int height,
	  int xMin, int yMin,
	  char *imageName, char *referenceName,
	  enum MapCompression compressionMethod,
	  char *error)
{
  if (compressionMethod != UncompressedMap)
    {
      strcpy(error, "WriteMap: unsupported compression method\n");
      return(0);
    }

  FILE *f = fopen(filename, "wb");
  if (f == NULL)
    {
      sprintf(error, "Cannot open file %s for writing\n", filename);
      return(0);
    }
  fprintf(f, "M1\n");
  fprintf(f, "%d\n", level);
  fprintf(f, "%d %d\n", width, height);
  fprintf(f, "%d %d\n", xMin, yMin);
  fprintf(f, "%s %s\n", imageName, referenceName);
  if (fwrite(map, ((size_t) width) * height * sizeof(MapElement), 1, f) != 1)
    {
      sprintf(error, "Could not write to file %s\n", filename);
      return(0);
    }
  fclose(f);
  return(1);
}
