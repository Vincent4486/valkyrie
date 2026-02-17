// SPDX-License-Identifier: BSD-3-Clause

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int path_exists(const char *path)
{
   struct stat st;
   return stat(path, &st) == 0;
}

static int mkdir_p(const char *path)
{
   char tmp[PATH_MAX];
   char *p = NULL;
   size_t len;

   if (!path || !*path) return -1;
   if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) return -1;
   len = strlen(tmp);
   if (len == 0) return -1;
   if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

   for (p = tmp + 1; *p; ++p)
   {
      if (*p == '/')
      {
         *p = '\0';
         if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
         *p = '/';
      }
   }

   if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
   return 0;
}

static int parse_size_bytes(const char *sizeText, uint64_t *bytes)
{
   if (!sizeText || !*sizeText || !bytes) return -1;

   char *end = NULL;
   double value = strtod(sizeText, &end);
   if (end == sizeText || value <= 0.0) return -1;

   uint64_t mul = 1;
   if (*end == 'k' || *end == 'K') mul = 1024ULL;
   else if (*end == 'm' || *end == 'M') mul = 1024ULL * 1024ULL;
   else if (*end == 'g' || *end == 'G') mul = 1024ULL * 1024ULL * 1024ULL;
   else if (*end != '\0') return -1;

   *bytes = (uint64_t)(value * (double)mul);
   return 0;
}

static int copy_file(const char *src, const char *dst)
{
   int inFd = open(src, O_RDONLY);
   if (inFd < 0) return -1;

   char parent[PATH_MAX];
   if (snprintf(parent, sizeof(parent), "%s", dst) >= (int)sizeof(parent))
   {
      close(inFd);
      return -1;
   }
   char *slash = strrchr(parent, '/');
   if (slash)
   {
      *slash = '\0';
      if (mkdir_p(parent) != 0)
      {
         close(inFd);
         return -1;
      }
   }

   int outFd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   if (outFd < 0)
   {
      close(inFd);
      return -1;
   }

   char buf[65536];
   ssize_t rd;
   while ((rd = read(inFd, buf, sizeof(buf))) > 0)
   {
      ssize_t off = 0;
      while (off < rd)
      {
         ssize_t wr = write(outFd, buf + off, (size_t)(rd - off));
         if (wr < 0)
         {
            close(inFd);
            close(outFd);
            return -1;
         }
         off += wr;
      }
   }

   close(inFd);
   close(outFd);
   return rd < 0 ? -1 : 0;
}

int builder_build_image(const char *variantRoot, const char *kernelName,
                        const char *outputFile, const char *config,
                        const char *arch, const char *outputFormat,
                        const char *imageSize, const char *imageFS)
{
   (void)imageFS;

   char kernelPath[PATH_MAX];
   char libPath[PATH_MAX];
   char shPath[PATH_MAX];
   char outPath[PATH_MAX];
   char stageRoot[PATH_MAX];

   if (snprintf(kernelPath, sizeof(kernelPath), "%s/kernel/%s", variantRoot,
                kernelName) >= (int)sizeof(kernelPath) ||
       snprintf(libPath, sizeof(libPath), "%s/usr/libmath_build/libmath.so",
                variantRoot) >= (int)sizeof(libPath) ||
       snprintf(shPath, sizeof(shPath), "%s/usr/sh_build/sh", variantRoot) >=
           (int)sizeof(shPath) ||
       snprintf(outPath, sizeof(outPath), "%s/%s_%s_%s.%s", variantRoot,
                outputFile, config, arch, outputFormat) >= (int)sizeof(outPath) ||
       snprintf(stageRoot, sizeof(stageRoot), "%s/image_root", variantRoot) >=
           (int)sizeof(stageRoot))
   {
      return 1;
   }

   if (!path_exists(kernelPath))
   {
      fprintf(stderr, "kernel artifact not found: %s\n", kernelPath);
      return 1;
   }

   char cleanCmd[PATH_MAX * 2];
   if (snprintf(cleanCmd, sizeof(cleanCmd), "rm -rf %s", stageRoot) >=
       (int)sizeof(cleanCmd))
   {
      return 1;
   }
   (void)system(cleanCmd);

   if (mkdir_p(stageRoot) != 0)
   {
      fprintf(stderr, "failed to create staging root: %s\n", stageRoot);
      return 1;
   }

   char copyRootCmd[PATH_MAX * 2];
   if (snprintf(copyRootCmd, sizeof(copyRootCmd),
                "cp -R image/root/. %s 2>/dev/null", stageRoot) >=
       (int)sizeof(copyRootCmd))
   {
      return 1;
   }
   (void)system(copyRootCmd);

   char stageKernel[PATH_MAX];
   char stageLib[PATH_MAX];
   char stageSh[PATH_MAX];
   if (snprintf(stageKernel, sizeof(stageKernel), "%s/boot/%s", stageRoot,
                kernelName) >= (int)sizeof(stageKernel) ||
       snprintf(stageLib, sizeof(stageLib), "%s/usr/lib/libmath.so", stageRoot) >=
           (int)sizeof(stageLib) ||
       snprintf(stageSh, sizeof(stageSh), "%s/usr/bin/sh", stageRoot) >=
           (int)sizeof(stageSh))
   {
      return 1;
   }

   if (copy_file(kernelPath, stageKernel) != 0)
   {
      fprintf(stderr, "failed to stage kernel\n");
      return 1;
   }
   if (path_exists(libPath) && copy_file(libPath, stageLib) != 0)
   {
      fprintf(stderr, "failed to stage libmath\n");
      return 1;
   }
   if (path_exists(shPath) && copy_file(shPath, stageSh) != 0)
   {
      fprintf(stderr, "failed to stage shell\n");
      return 1;
   }

   uint64_t imageBytes = 0;
   if (parse_size_bytes(imageSize, &imageBytes) != 0)
   {
      fprintf(stderr, "invalid imageSize: %s\n", imageSize);
      return 1;
   }

   int imgFd = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   if (imgFd < 0)
   {
      fprintf(stderr, "failed to create image: %s\n", outPath);
      return 1;
   }
   if (ftruncate(imgFd, (off_t)imageBytes) != 0)
   {
      close(imgFd);
      fprintf(stderr, "failed to size image: %s\n", outPath);
      return 1;
   }
   close(imgFd);

   char manifestPath[PATH_MAX];
   if (snprintf(manifestPath, sizeof(manifestPath), "%s/IMAGE_CONTENTS.txt",
                stageRoot) < (int)sizeof(manifestPath))
   {
      FILE *mf = fopen(manifestPath, "w");
      if (mf)
      {
         fprintf(mf, "kernel=%s\n", stageKernel);
         fprintf(mf, "libmath=%s\n", stageLib);
         fprintf(mf, "shell=%s\n", stageSh);
         fprintf(mf, "image=%s\n", outPath);
         fclose(mf);
      }
   }

   printf("staged image root: %s\n", stageRoot);
   printf("created image file: %s (%llu bytes)\n", outPath,
          (unsigned long long)imageBytes);
   return 0;
}
