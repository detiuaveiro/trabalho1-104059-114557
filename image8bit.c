#include "image8bit.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "instrumentation.h"

const uint8 PixMax = 255;

// Internal structure for storing 8-bit graymap images
struct image
{
  int width;
  int height;
  int maxval;   // maximum gray value (pixels with maxval are pure WHITE)
  uint8 *pixel; // pixel data (a raster scan)
};

// Variable to preserve errno temporarily
static int errsave = 0;

// Error cause
static char *errCause;
char *ImageErrMsg()
{ ///
  return errCause;
}

// Check a condition and set errCause to failmsg in case of failure.
// This may be used to chain a sequence of operations and verify its success.
// Propagates the condition.
// Preserves global errno!
static int check(int condition, const char *failmsg)
{
  errCause = (char *)(condition ? "" : failmsg);
  return condition;
}

/// Init Image library.  (Call once!)
/// Currently, simply calibrate instrumentation and set names of counters.
void ImageInit(void)
{ ///
  InstrCalibrate();
  InstrName[0] = "pixmem"; // InstrCount[0] will count pixel array acesses
  // Name other counters here...
}

// Macros to simplify accessing instrumentation counters:
#define PIXMEM InstrCount[0]
// Add more macros here...

// TIP: Search for PIXMEM or InstrCount to see where it is incremented!

Image ImageCreate(int width, int height, uint8 maxval)
{
  assert(width >= 0);
  assert(height >= 0);
  assert(0 < maxval && maxval <= PixMax);

  // Aloque memória para a estrutura Image.
  Image image = (Image)malloc(sizeof(struct image));
  if (image == NULL)
  {
    errsave = errno;
    errCause = "Memory allocation for Image structure failed";
    return NULL;
  }

  // Atribua os valores fornecidos à nova imagem.
  image->width = width;
  image->height = height;
  image->maxval = maxval;

  // Calcule o número total de pixels na imagem.
  int numPixels = width * height;

  // Aloque memória para o array de pixels.
  image->pixel = (uint8 *)malloc(numPixels * sizeof(uint8));
  if (image->pixel == NULL)
  {
    errsave = errno;
    free(image); // Libere a memória alocada para a estrutura Image.
    errCause = "Memory allocation for pixel array failed";
    return NULL;
  }

  // Inicialize todos os pixels com o valor zero (preto).
  for (int i = 0; i < numPixels; i++)
  {
    image->pixel[i] = 0;
  }

  return image;
}

void ImageDestroy(Image *imgp)
{
  assert(imgp != NULL);

  if (*imgp == NULL)
  {
    return; // Nenhuma operação é realizada se a imagem já for NULL.
  }

  // Libere a memória alocada para o array de pixels.
  free((*imgp)->pixel);

  // Libere a memória alocada para a estrutura Image.
  free(*imgp);

  // Defina o ponteiro de imagem para NULL.
  *imgp = NULL;
}

/// PGM file operations

// See also:
// PGM format specification: http://netpbm.sourceforge.net/doc/pgm.html

// Match and skip 0 or more comment lines in file f.
// Comments start with a # and continue until the end-of-line, inclusive.
// Returns the number of comments skipped.
static int skipComments(FILE *f)
{
  char c;
  int i = 0;
  while (fscanf(f, "#%*[^\n]%c", &c) == 1 && c == '\n')
  {
    i++;
  }
  return i;
}

/// Load a raw PGM file.
/// Only 8 bit PGM files are accepted.
/// On success, a new image is returned.
/// (The caller is responsible for destroying the returned image!)
/// On failure, returns NULL and errno/errCause are set accordingly.
Image ImageLoad(const char *filename)
{ ///
  int w, h;
  int maxval;
  char c;
  FILE *f = NULL;
  Image img = NULL;

  int success =
      check((f = fopen(filename, "rb")) != NULL, "Open failed") &&
      // Parse PGM header
      check(fscanf(f, "P%c ", &c) == 1 && c == '5', "Invalid file format") &&
      skipComments(f) >= 0 &&
      check(fscanf(f, "%d ", &w) == 1 && w >= 0, "Invalid width") &&
      skipComments(f) >= 0 &&
      check(fscanf(f, "%d ", &h) == 1 && h >= 0, "Invalid height") &&
      skipComments(f) >= 0 &&
      check(fscanf(f, "%d", &maxval) == 1 && 0 < maxval && maxval <= (int)PixMax, "Invalid maxval") &&
      check(fscanf(f, "%c", &c) == 1 && isspace(c), "Whitespace expected") &&
      // Allocate image
      (img = ImageCreate(w, h, (uint8)maxval)) != NULL &&
      // Read pixels
      check(fread(img->pixel, sizeof(uint8), w * h, f) == w * h, "Reading pixels");
  PIXMEM += (unsigned long)(w * h); // count pixel memory accesses

  // Cleanup
  if (!success)
  {
    errsave = errno;
    ImageDestroy(&img);
    errno = errsave;
  }
  if (f != NULL)
    fclose(f);
  return img;
}

/// Save image to PGM file.
/// On success, returns nonzero.
/// On failure, returns 0, errno/errCause are set appropriately, and
/// a partial and invalid file may be left in the system.
int ImageSave(Image img, const char *filename)
{ ///
  assert(img != NULL);
  int w = img->width;
  int h = img->height;
  uint8 maxval = img->maxval;
  FILE *f = NULL;

  int success =
      check((f = fopen(filename, "wb")) != NULL, "Open failed") &&
      check(fprintf(f, "P5\n%d %d\n%u\n", w, h, maxval) > 0, "Writing header failed") &&
      check(fwrite(img->pixel, sizeof(uint8), w * h, f) == w * h, "Writing pixels failed");
  PIXMEM += (unsigned long)(w * h); // count pixel memory accesses

  // Cleanup
  if (f != NULL)
    fclose(f);
  return success;
}

/// Information queries

/// These functions do not modify the image and never fail.

/// Get image width
int ImageWidth(Image img)
{ ///
  assert(img != NULL);
  return img->width;
}

/// Get image height
int ImageHeight(Image img)
{ ///
  assert(img != NULL);
  return img->height;
}

/// Get image maximum gray level
int ImageMaxval(Image img)
{ ///
  assert(img != NULL);
  return img->maxval;
}

void ImageStats(Image img, uint8 *min, uint8 *max)
{
  assert(img != NULL);
  assert(min != NULL);
  assert(max != NULL);

  int width = img->width;
  int height = img->height;

  if (width <= 0 || height <= 0)
  {
    // Se a imagem não tiver dimensões válidas, retorne valores padrão.
    *min = 0;
    *max = 0;
    return;
  }

  *min = img->pixel[0];
  *max = img->pixel[0];

  for (int i = 1; i < width * height; i++)
  {
    uint8 pixel_value = img->pixel[i];
    if (pixel_value < *min)
    {
      *min = pixel_value;
    }
    if (pixel_value > *max)
    {
      *max = pixel_value;
    }
  }
}

/// Check if pixel position (x,y) is inside img.
int ImageValidPos(Image img, int x, int y)
{ ///
  assert(img != NULL);
  return (0 <= x && x < img->width) && (0 <= y && y < img->height);
}

int ImageValidRect(Image img, int x, int y, int w, int h)
{
  assert(img != NULL);
  return (0 <= x && x + w <= img->width) && (0 <= y && y + h <= img->height);
}

static inline int G(Image img, int x, int y)
{
  assert(0 <= x && x < img->width);
  assert(0 <= y && y < img->height);

  int index = y * img->width + x;
  assert(0 <= index && index < img->width * img->height);
  return index;
}

/// Get the pixel (level) at position (x,y).
uint8 ImageGetPixel(Image img, int x, int y)
{ ///
  assert(img != NULL);
  assert(ImageValidPos(img, x, y));
  PIXMEM += 1; // count one pixel access (read)
  return img->pixel[G(img, x, y)];
}

/// Set the pixel at position (x,y) to new level.
void ImageSetPixel(Image img, int x, int y, uint8 level)
{ ///
  assert(img != NULL);
  assert(ImageValidPos(img, x, y));
  PIXMEM += 1; // count one pixel access (store)
  img->pixel[G(img, x, y)] = level;
}

void ImageNegative(Image img)
{ ///
  assert(img != NULL);

  int width = img->width;
  int height = img->height;

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      uint8 currentPixel = ImageGetPixel(img, x, y);
      uint8 negativePixel = PixMax - currentPixel; // Calcula o valor negativo do pixel
      ImageSetPixel(img, x, y, negativePixel);
    }
  }
}

void ImageThreshold(Image img, uint8 thr)
{ ///
  assert(img != NULL);

  int width = img->width;
  int height = img->height;
  uint8 maxval = img->maxval;

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      uint8 currentPixel = ImageGetPixel(img, x, y);
      uint8 newPixel;

      if (currentPixel < thr)
      {
        newPixel = 0; // Transforma em preto
      }
      else
      {
        newPixel = maxval; // Transforma em branco (máximo valor)
      }

      ImageSetPixel(img, x, y, newPixel);
    }
  }
}

void ImageBrighten(Image img, double factor)
{ ///
  assert(img != NULL);
  int width = img->width;
  int height = img->height;
  uint8 maxval = img->maxval;

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      uint8 currentPixel = ImageGetPixel(img, x, y);
      // Calcula o novo valor do pixel multiplicando pelo fator.
      uint8 newPixel = (int)(currentPixel * factor);

      // Satura o valor se ele exceder o valor máximo.
      if (newPixel > maxval)
      {
        newPixel = maxval;
      }

      ImageSetPixel(img, x, y, newPixel);
    }
  }
}



Image ImageRotate(Image img)
{ ///
  assert(img != NULL);

  // Obtenha as dimensões da imagem original.
  int width = img->width;
  int height = img->height;

  // Crie uma nova imagem com dimensões invertidas (90 graus anti-horário).
  Image rotatedImage = ImageCreate(height, width, img->maxval);

  if (rotatedImage == NULL)
  {
    // Falha na alocação de memória para a nova imagem.
    return NULL;
  }

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      // Copie os pixels da imagem original na nova imagem, girando 90 graus.
      uint8 pixel = ImageGetPixel(img, x, y);
      ImageSetPixel(rotatedImage, height - y - 1, x, pixel);
    }
  }

  return rotatedImage;
}

Image ImageMirror(Image img)
{ ///
  assert(img != NULL);

  // Obtenha as dimensões da imagem original.
  int width = img->width;
  int height = img->height;

  // Crie uma nova imagem espelhada com as mesmas dimensões.
  Image mirroredImage = ImageCreate(width, height, img->maxval);

  if (mirroredImage == NULL)
  {
    // Falha na alocação de memória para a nova imagem.
    return NULL;
  }

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      // Copie os pixels da imagem original na nova imagem espelhada.
      uint8 pixel = ImageGetPixel(img, x, y);
      ImageSetPixel(mirroredImage, width - x - 1, y, pixel);
    }
  }

  return mirroredImage;
}

Image ImageCrop(Image img, int x, int y, int w, int h)
{ ///
  assert(img != NULL);
  assert(ImageValidRect(img, x, y, w, h));

  // Crie uma nova imagem com as dimensões especificadas (w x h).
  Image croppedImage = ImageCreate(w, h, img->maxval);

  if (croppedImage == NULL)
  {
    // Falha na alocação de memória para a nova imagem.
    return NULL;
  }

  for (int j = 0; j < h; j++)
  {
    for (int i = 0; i < w; i++)
    {
      // Copie os pixels da imagem original na nova imagem (região recortada).
      uint8 pixel = ImageGetPixel(img, x + i, y + j);
      ImageSetPixel(croppedImage, i, j, pixel);
    }
  }

  return croppedImage;
}

void ImagePaste(Image img1, int x, int y, Image img2)
{ ///
  assert(img1 != NULL);
  assert(img2 != NULL);
  assert (ImageValidRect(img1, x, y, img2->width, img2->height));

  // Copie os pixels de img2 para img1 na posição (x, y).
  for (int j = 0; j < img2->height; j++) {
    for (int i = 0; i < img2->width; i++)
    {
      uint8 pixel = ImageGetPixel(img2, i, j);
      ImageSetPixel(img1, x + i, y + j, pixel);
    }
  }
}

void ImageBlend(Image img1, int x, int y, Image img2, double alpha)
{ ///
  assert(img1 != NULL);
  assert(img2 != NULL);
  assert(ImageValidRect(img1, x, y, img2->width, img2->height));

  // Misture os pixels de img2 com img1 na posição (x, y) usando o valor alfa.
  for (int j = 0; j < img2->height; j++)
  {
    for (int i = 0; i < img2->width; i++)
    {
      uint8 pixel1 = ImageGetPixel(img1, x + i, y + j);
      uint8 pixel2 = ImageGetPixel(img2, i, j);
      // Calcule o novo pixel misturado usando alpha.
      uint8 blendedPixel = (uint8)(alpha * pixel1 + (1.0 - alpha) * pixel2);
      ImageSetPixel(img1, x + i, y + j, blendedPixel);
    }
  }
}

int ImageMatchSubImage(Image img1, int x, int y, Image img2) { ///
  assert (img1 != NULL);
  assert (img2 != NULL);
  assert (ImageValidPos(img1, x, y));

  // Compare os pixels da subimagem img2 com a parte correspondente de img1 na posição (x, y).
  for (int j = 0; j < img2->height; j++) {
    for (int i = 0; i < img2->width; i++) {
      uint8 pixel1 = ImageGetPixel(img1, x + i, y + j);
      uint8 pixel2 = ImageGetPixel(img2, i, j);
      if (pixel1 != pixel2) {
        return 0; // Os pixels não coincidem.
      }
    }
  }

  return 1; // A subimagem coincide com a parte de img1 na posição (x, y).
}

int ImageLocateSubImage(Image img1, int *px, int *py, Image img2)
{ ///
  assert(img1 != NULL);
  assert(img2 != NULL);

  int width1 = img1->width;
  int height1 = img1->height;
  int width2 = img2->width;
  int height2 = img2->height;

  for (int y = 0; y < height1 - height2 + 1; y++)
  {
    for (int x = 0; x < width1 - width2 + 1; x++)
    {
      if (ImageMatchSubImage(img1, x, y, img2))
      {
        *px = x;
        *py = y;
        return 1; // Encontrou uma correspondência.
      }
    }
  }

  return 0; // Nenhuma correspondência encontrada.
}

void ImageBlur(Image img, int dx, int dy)
{ ///
  assert(img != NULL);
  int width = img->width;
  int height = img->height;

  Image originalImage = ImageCreate(width, height, img->maxval);

  if (originalImage == NULL)
  {
    // Falha na alocação de memória para a imagem original.
    return;
  }

  // Copie a imagem original para uma imagem temporária.
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      uint8 pixel = ImageGetPixel(img, x, y);
      ImageSetPixel(originalImage, x, y, pixel);
    }
  }

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      int sum = 0;
      int count = 0;

      for (int j = -dy; j <= dy; j++)
      {
        for (int i = -dx; i <= dx; i++)
        {
          if (ImageValidPos(originalImage, x + i, y + j))
          {
            sum += ImageGetPixel(originalImage, x + i, y + j);
            count++;
          }
        }
      }
    }
  }
}
