// SPDX-License-Identifier: GPL-3.0-only

// Kernel module helpers: maintain a loaded-module registry and provide
// insmod/rmmod/lsmod-style operations for ELF modules.
#ifndef KMOD_H
#define KMOD_H

#include <mem/mm_kernel.h>
#include <stdint.h>
#include <valecium/fs.h>

// Maximum dependencies per library
#define DYLIB_MAX_DEPS 16

// Maximum symbols per library
#define DYLIB_MAX_SYMBOLS 256

// Maximum global symbols across all loaded libraries and kernel
#define DYLIB_MAX_GLOBAL_SYMBOLS 1024

// Symbol record - exported function from a library
typedef struct
{
   char name[64];    // Symbol/function name
   uint32_t address; // Memory address of the function
} SymbolRecord;

// Dependency record - tracks which libraries a module depends on
typedef struct
{
   char name[64]; // Name of the dependency
   int resolved;  // 1 if dependency is loaded, 0 if missing
} DependencyRecord;

// Global symbol table entry - maps symbol name to absolute address
// Populated when libraries are loaded; used by relocations
typedef struct
{
   char name[64];     // Symbol name (e.g., "add", "malloc")
   uint32_t address;  // Absolute memory address where symbol is located
   char lib_name[64]; // Which library/module exports this symbol
   int is_kernel;     // 1 if from kernel, 0 if from library
} GlobalSymbolEntry;

// Symbol registration callback - called when a library is loaded
typedef void (*kmod_register_symbols_t)(const char *libname);

// Find a loaded library record by name (basename without extension). Returns
// pointer into the shared registry or NULL if not found.
LibRecord *KMOD_Find(const char *name);

// Call the entry point of a named library if present. Returns 0 on success,
// -1 if not found or dependencies unresolved.
int KMOD_CallIfExists(const char *name);

// Check if all dependencies of a library are resolved. Returns 1 if all
// resolved, 0 if any missing.
int KMOD_CheckDependencies(const char *name);

// Resolve all dependencies for a library. Returns 0 on success, -1 if any
// dependencies are missing.
int KMOD_ResolveDependencies(const char *name);

// Print the current library registry with dependency info (for debugging)
void KMOD_List(void);

// Print dependencies of a specific library
void KMOD_ListDependencies(const char *name);

// Find a symbol (function) by name within a library. Returns the function
// address or 0 if not found.
uint32_t KMOD_FindSymbol(const char *libname, const char *symname);

// Call a symbol (function) within a library by name. Returns the result
// of the function call, or -1 if not found or dependencies unresolved.
int KMOD_CallSymbol(const char *libname, const char *symname);

// List all symbols exported by a library
void KMOD_ListSymbols(const char *name);

// Parse symbols from a pre-loaded library (already in memory via bootloader
// registration) Call this when a library is already registered in LibRecord but
// symbols haven't been parsed yet
int KMOD_ParseSymbols(LibRecord *lib);

// Global symbol table management functions

// Add a symbol to the global registry. Symbols are extracted from .dynsym
// when libraries are loaded and registered here for relocation resolution.
int KMOD_AddGlobalSymbol(const char *name, uint32_t address,
                          const char *lib_name, int is_kernel);

// Look up a symbol in the global registry by name.
// Returns the absolute address or 0 if not found.
// Used by relocation processor to resolve symbol references.
uint32_t KMOD_LookupGlobalSymbol(const char *name);

// Print the global symbol table (for debugging)
void KMOD_PrintGlobalSymtab(void);

// Clear the global symbol table (on reload/shutdown)
void KMOD_ClearGlobalSymtab(void);

// Apply kernel relocations - patches kernel's PLT/GOT entries to point to
// library functions. Must be called after loading libraries and populating
// the global symbol table.
// Returns 0 on success, -1 on unresolved symbols.
int KMOD_ApplyKernelRelocations(void);

// Memory management functions

// Initialize the dylib memory allocator
int KMOD_MemoryInitialize(void);

// Allocate memory for a library. Returns allocated address or 0 on failure.
uint32_t KMOD_MemoryAllocate(const char *lib_name, uint32_t size);

// Deallocate memory for a library. Returns 0 on success, -1 on failure.
int KMOD_MemoryFree(const char *lib_name);

// Load a library from disk into memory. Returns 0 on success, -1 on failure.
// Parameters:
//   name: Library name to load
//   filepath: Path to library file on disk (e.g., "/sys/graphics.so")
int KMOD_LoadFromDisk(const char *name, const char *filepath);

// Load a library from memory image. Returns 0 on success, -1 on failure.
int KMOD_Load(const char *name, const void *image, uint32_t size);

// Remove a library from memory. Returns 0 on success, -1 on failure.
int KMOD_Remove(const char *name);

// Get memory usage statistics
void KMOD_MemoryStatus(void);

// Register a callback to load symbols when library is loaded
void KMOD_RegisterCallback(kmod_register_symbols_t callback);

#define KMOD_OK 0
#define KMOD_EINIT (-1)
#define KMOD_EINVAL (-2)
#define KMOD_ENOSLOT (-3)
#define KMOD_ELOAD (-4)
#define KMOD_EDEPEND (-5)
#define KMOD_EINITCALL (-6)
#define KMOD_ENOTFOUND (-7)

int KMOD_Initialize(void);
int KMOD_Insmod(const char *name, const char *filepath);
int KMOD_Rmmod(const char *name);
int KMOD_IsLoaded(const char *name);
void KMOD_Lsmod(void);

// ============================================================================
// Helper macro for loading function symbols from a library
// Usage: DYLIB_LOAD_SYMBOL(libname, funcname, functype);
//
// Example:
//   typedef int (*mod_op_t)(int, int);
//   DYLIB_LOAD_SYMBOL("mymodule", compute, mod_op_t);
//   result = compute(9, 9);
// ============================================================================

#define DYLIB_LOAD_SYMBOL(libname, funcname, functype)                         \
   functype funcname = (functype)KMOD_FindSymbol(libname, #funcname);         \
   if (!funcname)                                                              \
   {                                                                           \
      logfmt(LOG_ERROR,                                                        \
             "[DYLIB] Failed to resolve: " #libname "::" #funcname "\n");      \
      goto end;                                                                \
   }

#endif