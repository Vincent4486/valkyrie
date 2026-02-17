// SPDX-License-Identifier: BSD-3-Clause
// Valkyrie OS C build driver (SCons replacement).

#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CFG_PATH ".config"
#define MAX_FIELD 256
#define MAX_CMD 16384

extern int builder_menu_main(void);
extern int builder_build_image(const char *variantRoot, const char *kernelName,
                               const char *outputFile, const char *config,
                               const char *arch, const char *outputFormat,
                               const char *imageSize, const char *imageFS);

typedef struct
{
   char config[MAX_FIELD];
   char arch[MAX_FIELD];
   char imageFS[MAX_FIELD];
   char buildType[MAX_FIELD];
   char imageSize[MAX_FIELD];
   char toolchain[MAX_FIELD];
   char outputFile[MAX_FIELD];
   char outputFormat[MAX_FIELD];
   char kernelName[MAX_FIELD];
} BuildConfig;

typedef struct
{
   char **items;
   size_t len;
   size_t cap;
} StringList;

typedef struct
{
   const char *arch;
   const char *targetTriple;
   const char *toolPrefix;
   const char *defineName;
   const char *archFlags;
} ArchConfig;

static ArchConfig ARCHES[] = {
    {"i686", "i686-linux-musl", "i686-linux-musl-", "I686", "-m32"},
    {NULL, NULL, NULL, NULL, NULL}};

static void safe_copy(char *dst, const char *src)
{
   if (!src)
   {
      dst[0] = '\0';
      return;
   }
   strncpy(dst, src, MAX_FIELD - 1);
   dst[MAX_FIELD - 1] = '\0';
}

static char *trim(char *s)
{
   while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
   char *end = s + strlen(s);
   while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' ||
                      end[-1] == '\r'))
   {
      *--end = '\0';
   }
   if (*s == '\'' || *s == '"') s++;
   end = s + strlen(s);
   while (end > s && (end[-1] == '\'' || end[-1] == '"'))
   {
      *--end = '\0';
   }
   return s;
}

static void set_defaults(BuildConfig *cfg)
{
   safe_copy(cfg->config, "debug");
   safe_copy(cfg->arch, "i686");
   safe_copy(cfg->imageFS, "fat32");
   safe_copy(cfg->buildType, "full");
   safe_copy(cfg->imageSize, "250m");
   safe_copy(cfg->toolchain, "../os_toolchain");
   safe_copy(cfg->outputFile, "valkyrieos");
   safe_copy(cfg->outputFormat, "img");
   safe_copy(cfg->kernelName, "valkyrix");
}

static void load_config(BuildConfig *cfg)
{
   set_defaults(cfg);
   FILE *f = fopen(CFG_PATH, "r");
   if (!f) return;

   char line[512];
   while (fgets(line, sizeof(line), f))
   {
      char *eq = strchr(line, '=');
      if (!eq) continue;
      *eq = '\0';
      char *key = trim(line);
      char *val = trim(eq + 1);

      if (strcmp(key, "config") == 0)
         safe_copy(cfg->config, val);
      else if (strcmp(key, "arch") == 0)
         safe_copy(cfg->arch, val);
      else if (strcmp(key, "imageFS") == 0)
         safe_copy(cfg->imageFS, val);
      else if (strcmp(key, "buildType") == 0)
         safe_copy(cfg->buildType, val);
      else if (strcmp(key, "imageSize") == 0)
         safe_copy(cfg->imageSize, val);
      else if (strcmp(key, "toolchain") == 0)
         safe_copy(cfg->toolchain, val);
      else if (strcmp(key, "outputFile") == 0)
         safe_copy(cfg->outputFile, val);
      else if (strcmp(key, "outputFormat") == 0)
         safe_copy(cfg->outputFormat, val);
      else if (strcmp(key, "kernelName") == 0)
         safe_copy(cfg->kernelName, val);
   }
   (void)fclose(f);
}

static void list_init(StringList *list)
{
   list->items = NULL;
   list->len = 0;
   list->cap = 0;
}

static void list_free(StringList *list)
{
   if (!list) return;
   for (size_t i = 0; i < list->len; ++i) free(list->items[i]);
   free(list->items);
   list->items = NULL;
   list->len = list->cap = 0;
}

static int list_push(StringList *list, const char *value)
{
   if (list->len == list->cap)
   {
      size_t next = list->cap ? list->cap * 2 : 32;
      char **items = (char **)realloc(list->items, next * sizeof(char *));
      if (!items) return -1;
      list->items = items;
      list->cap = next;
   }
   list->items[list->len] = strdup(value);
   if (!list->items[list->len]) return -1;
   list->len++;
   return 0;
}

static bool has_suffix(const char *s, const char *suffix)
{
   size_t slen = strlen(s);
   size_t tlen = strlen(suffix);
   if (slen < tlen) return false;
   return strcmp(s + slen - tlen, suffix) == 0;
}

static bool path_exists(const char *path)
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

static int commandf(const char *fmt, ...)
{
   char cmd[MAX_CMD];
   va_list args;
   va_start(args, fmt);
   int n = vsnprintf(cmd, sizeof(cmd), fmt, args);
   va_end(args);
   if (n < 0 || n >= (int)sizeof(cmd))
   {
      fprintf(stderr, "Command too long\n");
      return -1;
   }
   printf("%s\n", cmd);
   int rc = system(cmd);
   if (rc == -1) return -1;
   if (WIFEXITED(rc)) return WEXITSTATUS(rc);
   return rc;
}

static int run_capture(const char *cmd, char *output, size_t outSize)
{
   FILE *pipe = popen(cmd, "r");
   if (!pipe) return -1;
   if (!fgets(output, (int)outSize, pipe))
   {
      output[0] = '\0';
      (void)pclose(pipe);
      return -1;
   }
   int rc = pclose(pipe);
   if (rc == -1 || !WIFEXITED(rc) || WEXITSTATUS(rc) != 0) return -1;
   output[strcspn(output, "\r\n")] = '\0';
   return 0;
}

static const ArchConfig *get_arch(const char *name)
{
   for (int i = 0; ARCHES[i].arch; ++i)
   {
      if (strcmp(name, ARCHES[i].arch) == 0) return &ARCHES[i];
   }
   return NULL;
}

static int resolve_compiler(const BuildConfig *cfg, const ArchConfig *arch,
                            char *out, size_t outSize)
{
   char candidate[PATH_MAX];
   int n = snprintf(candidate, sizeof(candidate), "%s/bin/%sgcc",
                    cfg->toolchain, arch->toolPrefix);
   if (n > 0 && n < (int)sizeof(candidate) && access(candidate, X_OK) == 0)
   {
      if (snprintf(out, outSize, "%s", candidate) >= (int)outSize) return -1;
      return 0;
   }
   if (snprintf(out, outSize, "gcc") >= (int)outSize) return -1;
   return 0;
}

static int collect_sources_recursive(const char *base, const char *current,
                                     StringList *sources)
{
   DIR *dir = opendir(current);
   if (!dir) return -1;

   struct dirent *ent;
   while ((ent = readdir(dir)) != NULL)
   {
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
         continue;
      char path[PATH_MAX];
      if (snprintf(path, sizeof(path), "%s/%s", current, ent->d_name) >=
          (int)sizeof(path))
         continue;

      struct stat st;
      if (stat(path, &st) != 0) continue;

      if (S_ISDIR(st.st_mode))
      {
         if (collect_sources_recursive(base, path, sources) != 0)
         {
            closedir(dir);
            return -1;
         }
      }
      else if (S_ISREG(st.st_mode))
      {
         if (has_suffix(ent->d_name, ".c") || has_suffix(ent->d_name, ".cpp") ||
             has_suffix(ent->d_name, ".S"))
         {
            char rel[PATH_MAX];
            const char *start = path + strlen(base);
            if (*start == '/') start++;
            if (snprintf(rel, sizeof(rel), "%s", start) >= (int)sizeof(rel))
               continue;
            if (list_push(sources, rel) != 0)
            {
               closedir(dir);
               return -1;
            }
         }
      }
   }

   closedir(dir);
   return 0;
}

static int collect_sources(const char *root, StringList *sources)
{
   list_init(sources);
   return collect_sources_recursive(root, root, sources);
}

static void replace_extension_with_o(char *path)
{
   char *dot = strrchr(path, '.');
   if (!dot) return;
   strcpy(dot, ".o");
}

static int ensure_parent_dir(const char *path)
{
   char tmp[PATH_MAX];
   if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) return -1;
   char *slash = strrchr(tmp, '/');
   if (!slash) return 0;
   *slash = '\0';
   return mkdir_p(tmp);
}

static const char *config_flags(const BuildConfig *cfg)
{
   return strcmp(cfg->config, "release") == 0 ? "-O3 -DRELEASE -s"
                                              : "-O0 -DDEBUG -g";
}

static int append_str(char *dst, size_t cap, const char *src)
{
   size_t used = strlen(dst);
   size_t need = strlen(src);
   if (used + need + 1 >= cap) return -1;
   memcpy(dst + used, src, need + 1);
   return 0;
}

static int build_objects(const char *compiler, const char *sourceRoot,
                         const char *objectRoot, const char *extraFlags,
                         StringList *objects)
{
   StringList sources;
   if (collect_sources(sourceRoot, &sources) != 0)
   {
      fprintf(stderr, "Failed to enumerate sources in %s\n", sourceRoot);
      return 1;
   }

   list_init(objects);
   for (size_t i = 0; i < sources.len; ++i)
   {
      char src[PATH_MAX];
      char obj[PATH_MAX];
      if (snprintf(src, sizeof(src), "%s/%s", sourceRoot, sources.items[i]) >=
              (int)sizeof(src) ||
          snprintf(obj, sizeof(obj), "%s/%s", objectRoot, sources.items[i]) >=
              (int)sizeof(obj))
      {
         list_free(&sources);
         list_free(objects);
         return 1;
      }
      replace_extension_with_o(obj);
      if (ensure_parent_dir(obj) != 0)
      {
         fprintf(stderr, "Failed to create output dir for %s\n", obj);
         list_free(&sources);
         list_free(objects);
         return 1;
      }

      int rc = commandf("%s -c %s -o %s %s", compiler, src, obj, extraFlags);
      if (rc != 0)
      {
         list_free(&sources);
         list_free(objects);
         return rc;
      }
      if (list_push(objects, obj) != 0)
      {
         list_free(&sources);
         list_free(objects);
         return 1;
      }
   }

   list_free(&sources);
   return 0;
}

static int find_object_by_name(const StringList *objects, const char *name)
{
   for (size_t i = 0; i < objects->len; ++i)
   {
      const char *base = strrchr(objects->items[i], '/');
      base = base ? base + 1 : objects->items[i];
      if (strcmp(base, name) == 0) return (int)i;
   }
   return -1;
}

static int build_kernel(const BuildConfig *cfg, const ArchConfig *arch,
                        const char *compiler, const char *variantRoot)
{
   char linker[PATH_MAX];
   if (snprintf(linker, sizeof(linker), "kernel/arch/%s/boot/linker.ld",
                cfg->arch) >= (int)sizeof(linker))
      return 1;
   if (!path_exists(linker))
   {
      fprintf(stderr, "Missing linker script for arch '%s': %s\n", cfg->arch,
              linker);
      return 1;
   }

   char objRoot[PATH_MAX];
   char kernelOut[PATH_MAX];
   char mapOut[PATH_MAX];
   if (snprintf(objRoot, sizeof(objRoot), "%s/kernel", variantRoot) >=
           (int)sizeof(objRoot) ||
       snprintf(kernelOut, sizeof(kernelOut), "%s/kernel/%s", variantRoot,
                cfg->kernelName) >= (int)sizeof(kernelOut) ||
       snprintf(mapOut, sizeof(mapOut), "%s/kernel/core.map", variantRoot) >=
           (int)sizeof(mapOut))
   {
      return 1;
   }
   if (mkdir_p(objRoot) != 0) return 1;

   char flags[2048] = {0};
   if (append_str(flags, sizeof(flags), config_flags(cfg)) != 0 ||
       append_str(flags, sizeof(flags), " ") != 0 ||
       append_str(flags, sizeof(flags), arch->archFlags) != 0 ||
       append_str(flags, sizeof(flags), " -D") != 0 ||
       append_str(flags, sizeof(flags), arch->defineName) != 0 ||
       append_str(flags, sizeof(flags),
                  " -ffreestanding -nostdlib -fno-stack-protector -fno-builtin "
                  "-Wall -Wextra -Ikernel -Iinclude") != 0)
   {
      return 1;
   }

   StringList objects;
   int rc = build_objects(compiler, "kernel", objRoot, flags, &objects);
   if (rc != 0) return rc;

   int crtiIdx = find_object_by_name(&objects, "crti.o");
   int crtnIdx = find_object_by_name(&objects, "crtn.o");
   if (crtiIdx < 0 || crtnIdx < 0)
   {
      fprintf(stderr,
              "Required startup objects crti.o/crtn.o were not produced\n");
      list_free(&objects);
      return 1;
   }

   char crtbegin[PATH_MAX] = {0};
   char crtend[PATH_MAX] = {0};
   char query[MAX_CMD];
   if (snprintf(query, sizeof(query), "%s -print-file-name=crtbegin.o",
                compiler) >= (int)sizeof(query) ||
       run_capture(query, crtbegin, sizeof(crtbegin)) != 0 ||
       snprintf(query, sizeof(query), "%s -print-file-name=crtend.o",
                compiler) >= (int)sizeof(query) ||
       run_capture(query, crtend, sizeof(crtend)) != 0)
   {
      fprintf(stderr,
              "Unable to resolve crtbegin.o/crtend.o from compiler: %s\n",
              compiler);
      list_free(&objects);
      return 1;
   }

   char objectList[MAX_CMD] = {0};
   if (append_str(objectList, sizeof(objectList), objects.items[crtiIdx]) !=
           0 ||
       append_str(objectList, sizeof(objectList), " ") != 0 ||
       append_str(objectList, sizeof(objectList), crtbegin) != 0 ||
       append_str(objectList, sizeof(objectList), " ") != 0)
   {
      list_free(&objects);
      return 1;
   }

   for (size_t i = 0; i < objects.len; ++i)
   {
      if ((int)i == crtiIdx || (int)i == crtnIdx) continue;
      if (append_str(objectList, sizeof(objectList), objects.items[i]) != 0 ||
          append_str(objectList, sizeof(objectList), " ") != 0)
      {
         list_free(&objects);
         return 1;
      }
   }

   if (append_str(objectList, sizeof(objectList), crtend) != 0 ||
       append_str(objectList, sizeof(objectList), " ") != 0 ||
       append_str(objectList, sizeof(objectList), objects.items[crtnIdx]) != 0)
   {
      list_free(&objects);
      return 1;
   }

   rc = commandf(
       "%s %s -nostdlib -Wl,-T,%s -Wl,-Map=%s -Wl,-z,relro,-z,now "
       "-Wl,-z,noexecstack -Wl,--as-needed -Wl,--export-dynamic -o %s %s -lgcc",
       compiler, arch->archFlags, linker, mapOut, kernelOut, objectList);

   list_free(&objects);
   return rc;
}

static void build_sysroot_flag(const BuildConfig *cfg, const ArchConfig *arch,
                               char *out, size_t outSize)
{
   char sysroot[PATH_MAX];
   if (snprintf(sysroot, sizeof(sysroot), "%s/%s/sysroot", cfg->toolchain,
                arch->targetTriple) < (int)sizeof(sysroot) &&
       path_exists(sysroot))
   {
      snprintf(out, outSize, "--sysroot=%s", sysroot);
      return;
   }
   out[0] = '\0';
}

static int build_user_libmath(const BuildConfig *cfg, const ArchConfig *arch,
                              const char *compiler, const char *variantRoot)
{
   char outRoot[PATH_MAX];
   if (snprintf(outRoot, sizeof(outRoot), "%s/usr/libmath_build",
                variantRoot) >= (int)sizeof(outRoot))
      return 1;
   if (mkdir_p(outRoot) != 0) return 1;

   char sysrootFlag[PATH_MAX] = {0};
   build_sysroot_flag(cfg, arch, sysrootFlag, sizeof(sysrootFlag));

   char flags[2048] = {0};
   if (append_str(flags, sizeof(flags), config_flags(cfg)) != 0 ||
       append_str(flags, sizeof(flags), " ") != 0 ||
       append_str(flags, sizeof(flags), arch->archFlags) != 0 ||
       append_str(flags, sizeof(flags), " -fPIC -Iusr/libmath ") != 0 ||
       append_str(flags, sizeof(flags), sysrootFlag) != 0)
   {
      return 1;
   }

   StringList objects;
   int rc = build_objects(compiler, "usr/libmath", outRoot, flags, &objects);
   if (rc != 0) return rc;

   char objList[MAX_CMD] = {0};
   for (size_t i = 0; i < objects.len; ++i)
   {
      if (append_str(objList, sizeof(objList), objects.items[i]) != 0 ||
          append_str(objList, sizeof(objList), " ") != 0)
      {
         list_free(&objects);
         return 1;
      }
   }

   char outLib[PATH_MAX];
   if (snprintf(outLib, sizeof(outLib), "%s/libmath.so", outRoot) >=
       (int)sizeof(outLib))
   {
      list_free(&objects);
      return 1;
   }

   rc = commandf("%s %s %s -shared -Wl,-soname,libmath.so -o %s %s", compiler,
                 arch->archFlags, sysrootFlag, outLib, objList);
   list_free(&objects);
   return rc;
}

static int build_user_shell(const BuildConfig *cfg, const ArchConfig *arch,
                            const char *compiler, const char *variantRoot)
{
   char outRoot[PATH_MAX];
   if (snprintf(outRoot, sizeof(outRoot), "%s/usr/sh_build", variantRoot) >=
       (int)sizeof(outRoot))
      return 1;
   if (mkdir_p(outRoot) != 0) return 1;

   char sysrootFlag[PATH_MAX] = {0};
   build_sysroot_flag(cfg, arch, sysrootFlag, sizeof(sysrootFlag));

   char flags[2048] = {0};
   if (append_str(flags, sizeof(flags), config_flags(cfg)) != 0 ||
       append_str(flags, sizeof(flags), " ") != 0 ||
       append_str(flags, sizeof(flags), arch->archFlags) != 0 ||
       append_str(flags, sizeof(flags),
                  " -Iusr/sh -D_POSIX_C_SOURCE=200809L ") != 0 ||
       append_str(flags, sizeof(flags), sysrootFlag) != 0)
   {
      return 1;
   }

   StringList objects;
   int rc = build_objects(compiler, "usr/sh", outRoot, flags, &objects);
   if (rc != 0) return rc;

   char objList[MAX_CMD] = {0};
   for (size_t i = 0; i < objects.len; ++i)
   {
      if (append_str(objList, sizeof(objList), objects.items[i]) != 0 ||
          append_str(objList, sizeof(objList), " ") != 0)
      {
         list_free(&objects);
         return 1;
      }
   }

   char outBin[PATH_MAX];
   if (snprintf(outBin, sizeof(outBin), "%s/sh", outRoot) >=
       (int)sizeof(outBin))
   {
      list_free(&objects);
      return 1;
   }

   rc = commandf("%s %s %s -o %s %s", compiler, arch->archFlags, sysrootFlag,
                 outBin, objList);
   list_free(&objects);
   return rc;
}

static int build_userspace(const BuildConfig *cfg, const ArchConfig *arch,
                           const char *compiler, const char *variantRoot)
{
   int rc = build_user_libmath(cfg, arch, compiler, variantRoot);
   if (rc != 0) return rc;
   return build_user_shell(cfg, arch, compiler, variantRoot);
}

static int remove_cb(const char *path, const struct stat *st, int typeflag,
                     struct FTW *ftwbuf)
{
   (void)st;
   (void)typeflag;
   (void)ftwbuf;
   return remove(path);
}

static int clean_variant(const char *variantRoot)
{
   if (!path_exists(variantRoot)) return 0;
   return nftw(variantRoot, remove_cb, 32, FTW_DEPTH | FTW_PHYS);
}

static int action_target(const BuildConfig *cfg, const ArchConfig *arch,
                         const char *target)
{
   char imagePath[PATH_MAX];
   if (snprintf(imagePath, sizeof(imagePath), "build/%s_%s/%s_%s_%s.%s",
                cfg->arch, cfg->config, cfg->outputFile, cfg->config, cfg->arch,
                cfg->outputFormat) >= (int)sizeof(imagePath))
   {
      return 1;
   }

   if (strcmp(target, "run") == 0)
   {
      return commandf("python3 ./scripts/base/qemu.py -a %s disk %s", cfg->arch,
                      imagePath);
   }
   if (strcmp(target, "debug") == 0)
   {
      return commandf("python3 ./scripts/base/gdb.py -a %s disk %s", cfg->arch,
                      imagePath);
   }
   if (strcmp(target, "bochs") == 0)
   {
      return commandf("python3 ./scripts/base/bochs.py disk %s", imagePath);
   }
   if (strcmp(target, "toolchain") == 0)
   {
      return commandf("python3 ./scripts/base/toolchain.py %s -t %s",
                      cfg->toolchain, arch->targetTriple);
   }
   if (strcmp(target, "fformat") == 0)
   {
      return commandf("python3 ./scripts/base/format.py");
   }
   if (strcmp(target, "deps") == 0)
   {
      return commandf("python3 ./scripts/base/dependencies.py");
   }

   fprintf(stderr, "Unknown action target: %s\n", target);
   return 1;
}

static void usage(const char *prog)
{
   fprintf(stderr,
      "Usage: %s [--menu] [--build] [--clean] [--target "
           "run|debug|bochs|toolchain|fformat|deps] [--set key=val ...]\n"
           "Defaults to --build when no action is specified.\n",
           prog);
}

int main(int argc, char **argv)
{
   BuildConfig cfg;
   set_defaults(&cfg);

   bool do_build = true;
   bool do_clean = false;
   bool do_menu = false;
   const char *target = NULL;

   for (int i = 1; i < argc; ++i)
   {
      if (strcmp(argv[i], "--build") == 0)
      {
         do_build = true;
      }
      else if (strcmp(argv[i], "--menu") == 0)
      {
         do_menu = true;
         do_build = false;
      }
      else if (strcmp(argv[i], "--clean") == 0)
      {
         do_clean = true;
         do_build = false;
      }
      else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc)
      {
         target = argv[++i];
         do_build = false;
      }
      else if (strcmp(argv[i], "--set") == 0 && i + 1 < argc)
      {
         char *kv = argv[++i];
         char *eq = strchr(kv, '=');
         if (!eq) continue;
         *eq = '\0';
         const char *key = kv;
         const char *val = eq + 1;
         if (strcmp(key, "config") == 0)
            safe_copy(cfg.config, val);
         else if (strcmp(key, "arch") == 0)
            safe_copy(cfg.arch, val);
         else if (strcmp(key, "imageFS") == 0)
            safe_copy(cfg.imageFS, val);
         else if (strcmp(key, "buildType") == 0)
            safe_copy(cfg.buildType, val);
         else if (strcmp(key, "imageSize") == 0)
            safe_copy(cfg.imageSize, val);
         else if (strcmp(key, "toolchain") == 0)
            safe_copy(cfg.toolchain, val);
         else if (strcmp(key, "outputFile") == 0)
            safe_copy(cfg.outputFile, val);
         else if (strcmp(key, "outputFormat") == 0)
            safe_copy(cfg.outputFormat, val);
         else if (strcmp(key, "kernelName") == 0)
            safe_copy(cfg.kernelName, val);
      }
      else
      {
         usage(argv[0]);
         return 1;
      }
   }

   if (do_menu)
   {
      return builder_menu_main();
   }

   if (!path_exists(CFG_PATH))
   {
      fprintf(stderr, "Error: %s not found. Run './build/builder --menu' first.\n", CFG_PATH);
      return 1;
   }

   load_config(&cfg);

   char variantRoot[PATH_MAX];
   if (snprintf(variantRoot, sizeof(variantRoot), "build/%s_%s", cfg.arch,
                cfg.config) >= (int)sizeof(variantRoot))
   {
      return 1;
   }

   if (do_clean)
   {
      return clean_variant(variantRoot);
   }

   const ArchConfig *arch = get_arch(cfg.arch);
   if (!arch)
   {
      fprintf(stderr, "Unsupported arch in .config: %s\n", cfg.arch);
      return 1;
   }

   if (target) return action_target(&cfg, arch, target);

   if (!do_build)
   {
      usage(argv[0]);
      return 1;
   }

   char compiler[PATH_MAX];
   if (resolve_compiler(&cfg, arch, compiler, sizeof(compiler)) != 0)
   {
      fprintf(stderr, "Failed to resolve compiler\n");
      return 1;
   }

   if (mkdir_p(variantRoot) != 0)
   {
      fprintf(stderr, "Failed to create variant directory: %s\n", variantRoot);
      return 1;
   }

   if (strcmp(cfg.buildType, "kernel") == 0)
   {
      return build_kernel(&cfg, arch, compiler, variantRoot);
   }
   if (strcmp(cfg.buildType, "usr") == 0)
   {
      return build_userspace(&cfg, arch, compiler, variantRoot);
   }
   if (strcmp(cfg.buildType, "image") == 0)
   {
      return builder_build_image(variantRoot, cfg.kernelName, cfg.outputFile,
                                 cfg.config, cfg.arch, cfg.outputFormat,
                                 cfg.imageSize, cfg.imageFS);
   }
   if (strcmp(cfg.buildType, "full") == 0)
   {
      int rc = build_userspace(&cfg, arch, compiler, variantRoot);
      if (rc != 0) return rc;
      rc = build_kernel(&cfg, arch, compiler, variantRoot);
      if (rc != 0) return rc;
      return builder_build_image(variantRoot, cfg.kernelName, cfg.outputFile,
                                 cfg.config, cfg.arch, cfg.outputFormat,
                                 cfg.imageSize, cfg.imageFS);
   }

   fprintf(stderr, "Unsupported buildType: %s\n", cfg.buildType);
   return 1;
}
