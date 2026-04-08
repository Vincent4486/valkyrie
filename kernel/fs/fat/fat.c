// SPDX-License-Identifier: GPL-3.0-only

#include "fat.h"
#include <crypto/crypto.h>
#include <drivers/ata/ata.h>
#include <drivers/fdc/fdc.h>
#include <fs/vfs/vfs.h>
#include <mem/mm_kernel.h>
#include <std/ctype.h>
#include <std/minmax.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stddef.h>
#include <sys/sys.h>
#include <valecium/fs.h>

#define SECTOR_SIZE 512
#define MAX_PATH_SIZE 256
#define MAX_FILE_HANDLES 10
#define ROOT_DIRECTORY_HANDLE -1
#define FAT_CACHE_SIZE 5
#define FAT_METADATA_PATH "/.vkmeta"
#define FAT_METADATA_FLAG_VALID 0x01
#define FAT_METADATA_FLAG_DELETED 0x02

typedef struct
{
   uint8_t Hash[SHA1_DIGEST_SIZE];
   uint16_t Mode;
   uint32_t Uid;
   uint32_t Gid;
   uint8_t Flags;
   uint8_t _Reserved[5];
} __attribute__((packed)) FAT_MetadataRecord;

typedef struct
{
   // extended boot record
   uint8_t DriveNumber;
   uint8_t _Reserved;
   uint8_t Signature;
   uint32_t VolumeId;       // serial number, value doesn't matter
   uint8_t VolumeLabel[11]; // 11 bytes, padded with spaces
   uint8_t SystemId[8];
} __attribute__((packed)) FAT_ExtendedBootRecord;

typedef struct
{
   uint32_t SectorsPerFat;
   uint16_t Flags;
   uint16_t FatVersionNumber;
   uint32_t RootDirectoryCluster;
   uint16_t FSInfoSector;
   uint16_t BackupBootSector;
   uint8_t _Reserved[12];
   FAT_ExtendedBootRecord EBR;
} __attribute__((packed)) FAT32_ExtendedBootRecord;

typedef struct
{
   uint8_t BootJumpInstruction[3];
   uint8_t OemIdentifier[8];
   uint16_t BytesPerSector;
   uint8_t SectorsPerCluster;
   uint16_t ReservedSectors;
   uint8_t FatCount;
   uint16_t DirEntryCount;
   uint16_t TotalSectors;
   uint8_t MediaDescriptorType;
   uint16_t SectorsPerFat;
   uint16_t SectorsPerTrack;
   uint16_t Heads;
   uint32_t HiddenSectors;
   uint32_t LargeSectorCount;

   union
   {
      FAT_ExtendedBootRecord EBR1216;
      FAT32_ExtendedBootRecord EBR32;
   } ExtendedBootRecord;
} __attribute__((packed)) FAT_BootSector;

typedef struct
{
   uint8_t Buffer[SECTOR_SIZE];
   FAT_File Public;
   bool Opened;
   bool Truncated; // Track if file has been truncated for writing
   uint32_t FirstCluster;
   uint32_t CurrentCluster;
   uint32_t CurrentSectorInCluster;

   // Track parent directory so we can update the owning directory entry.
   uint32_t ParentCluster;
   bool ParentIsRoot;

} FAT_FileData;

/* ============================================================================
 * FAT_Instance — encapsulates ALL per-volume state.
 * Replaces the former static g_Data pointer and all g_* scalar globals.
 * One instance is allocated by FAT_Initialize() per mounted partition and
 * stored in Partition->fs->private_data.
 * ============================================================================
 */
struct FAT_Instance
{
   /* Boot sector (formerly FAT_Data::BS) */
   union
   {
      FAT_BootSector BootSector;
      uint8_t BootSectorBytes[SECTOR_SIZE];
   } BS;

   /* Root-directory pseudo-handle (always open, always index -1) */
   FAT_FileData RootDirectory;

   /* Per-handle open-file table */
   FAT_FileData OpenedFiles[MAX_FILE_HANDLES];

   /* FAT sector cache */
   uint8_t FatCache[FAT_CACHE_SIZE * SECTOR_SIZE];
   uint32_t FatCachePos;

   /* Derived filesystem geometry (formerly g_* globals) */
   uint32_t DataSectionLba;
   uint8_t FatType; /* 12, 16, or 32 */
   uint32_t TotalSectors;
   uint32_t SectorsPerFat;
   uint32_t RootDirLba;     /* FAT12/16 fixed-root start LBA (0 for FAT32) */
   uint32_t RootDirSectors; /* FAT12/16 fixed-root sector count (0 for FAT32) */
};

static uint16_t fat_normalize_mode(uint16_t mode)
{
   uint16_t masked = mode & 0777u;
   if (masked == 0) return 0644u;
   return masked;
}

static bool fat_is_metadata_path(const char *path)
{
   return path && strcmp(path, FAT_METADATA_PATH) == 0;
}

static void fat_normalize_absolute_path(const char *path, char *out,
                                        size_t outSize)
{
   if (!out || outSize == 0) return;

   if (!path || path[0] == '\0')
   {
      out[0] = '/';
      if (outSize > 1) out[1] = '\0';
      return;
   }

   if (path[0] == '/')
   {
      strncpy(out, path, outSize - 1);
      out[outSize - 1] = '\0';
      return;
   }

   out[0] = '/';
   strncpy(out + 1, path, outSize - 2);
   out[outSize - 1] = '\0';
}

/* Retrieve the FAT_Instance stored in a Partition's filesystem slot. */
static inline FAT_Instance *fat_inst(const Partition *disk)
{
   if (!disk || !disk->fs) return NULL;
   return (FAT_Instance *)disk->fs->private_data;
}

/* Forward declarations for internal helpers */
static uint32_t FAT_ClusterToLba(const FAT_Instance *inst, uint32_t cluster);
static bool FAT_ReadFat(FAT_Instance *inst, Partition *disk, size_t LBAIndex);

static bool FAT_ReadFat(FAT_Instance *inst, Partition *disk, size_t LBAIndex)
{
   return Partition_ReadSectors(disk,
                                inst->BS.BootSector.ReservedSectors + LBAIndex,
                                FAT_CACHE_SIZE, inst->FatCache);
}

static void FAT_Detect(FAT_Instance *inst)
{
   uint32_t dataClusters = (inst->TotalSectors - inst->DataSectionLba) /
                           inst->BS.BootSector.SectorsPerCluster;
   if (dataClusters < 0xFF5)
      inst->FatType = 12;
   else if (inst->BS.BootSector.SectorsPerFat != 0)
      inst->FatType = 16;
   else
      inst->FatType = 32;
}

FAT_Instance *FAT_Initialize(Partition *disk)
{
   /* Allocate and zero-initialise the per-volume instance. */
   FAT_Instance *inst = (FAT_Instance *)kmalloc(sizeof(FAT_Instance));
   if (!inst)
   {
      logfmt(LOG_ERROR, "[FAT] Failed to allocate FAT_Instance\n");
      return NULL;
   }
   memset(inst, 0, sizeof(FAT_Instance));

   // Read boot sector from partition
   uint8_t *bootSector = (uint8_t *)kmalloc(512);
   if (!bootSector)
   {
      logfmt(LOG_ERROR, "[FAT] Failed to allocate boot sector buffer\n");
      free(inst);
      return NULL;
   }
   if (!Partition_ReadSectors(disk, 0, 1, bootSector))
   {
      logfmt(LOG_ERROR, "[FAT] Failed to read boot sector\n");
      free(bootSector);
      free(inst);
      return NULL;
   }

   // Check for valid FAT signature (0x55AA at bytes 510-511)
   if (bootSector[510] != 0x55 || bootSector[511] != 0xAA)
   {
      logfmt(LOG_ERROR, "[FAT] Invalid boot sector signature\n");
      free(bootSector);
      free(inst);
      return NULL;
   }

   // Copy boot sector into instance
   memcpy(inst->BS.BootSectorBytes, bootSector, SECTOR_SIZE);
   free(bootSector);

   // Debug: print BPB values
   logfmt(LOG_INFO, "[FAT] BPB BytesPerSector=%u, SectorsPerCluster=%u\n",
          inst->BS.BootSector.BytesPerSector,
          inst->BS.BootSector.SectorsPerCluster);

   // Validate critical BPB values to prevent divide-by-zero later
   if (inst->BS.BootSector.BytesPerSector == 0 ||
       inst->BS.BootSector.SectorsPerCluster == 0)
   {
      logfmt(LOG_ERROR,
             "[FAT] Invalid BPB (BytesPerSector=%u, "
             "SectorsPerCluster=%u)\n",
             inst->BS.BootSector.BytesPerSector,
             inst->BS.BootSector.SectorsPerCluster);
      free(inst);
      return NULL;
   }

   // Initialise FAT cache as invalid
   inst->FatCachePos = 0xFFFFFFFF;

   inst->TotalSectors = inst->BS.BootSector.TotalSectors;
   if (inst->TotalSectors == 0)
   { // fat32
      inst->TotalSectors = inst->BS.BootSector.LargeSectorCount;
   }

   bool isFat32 = false;
   inst->SectorsPerFat = inst->BS.BootSector.SectorsPerFat;
   uint32_t rootDirCluster = 0;

   if (inst->SectorsPerFat == 0)
   { // fat32
      isFat32 = true;
      rootDirCluster =
          inst->BS.BootSector.ExtendedBootRecord.EBR32.RootDirectoryCluster;
      inst->SectorsPerFat =
          inst->BS.BootSector.ExtendedBootRecord.EBR32.SectorsPerFat;
   }

   // open root directory file
   uint32_t rootDirLba;
   uint32_t rootDirSize;
   if (isFat32)
   {
      // Data section starts after reserved + FAT areas
      inst->DataSectionLba = inst->BS.BootSector.ReservedSectors +
                             inst->SectorsPerFat * inst->BS.BootSector.FatCount;

      // For FAT32 the root directory is a normal cluster chain starting at
      // RootDirectoryCluster. Keep cluster number in
      // RootDirectory.FirstCluster. RootDirLba/RootDirSectors = 0 indicates a
      // clustered root.
      inst->RootDirLba = 0;
      inst->RootDirSectors = 0;
      rootDirLba = FAT_ClusterToLba(inst, rootDirCluster); // first cluster LBA
      rootDirSize = 0;
   }
   else
   {
      // FAT12/16: root directory stored in a fixed area (immediately after
      // FATs)
      rootDirLba = inst->BS.BootSector.ReservedSectors +
                   inst->SectorsPerFat * inst->BS.BootSector.FatCount;
      rootDirSize =
          sizeof(FAT_DirectoryEntry) * inst->BS.BootSector.DirEntryCount;
      uint32_t rootDirSectors =
          (rootDirSize + inst->BS.BootSector.BytesPerSector - 1) /
          inst->BS.BootSector.BytesPerSector;
      // Data section starts AFTER the root directory
      inst->DataSectionLba = rootDirLba + rootDirSectors;

      inst->RootDirLba = rootDirLba;
      inst->RootDirSectors = rootDirSectors;
   }

   inst->RootDirectory.Public.Handle = ROOT_DIRECTORY_HANDLE;
   inst->RootDirectory.Public.IsDirectory = true;
   inst->RootDirectory.Public.Position = 0;
   inst->RootDirectory.Public.instance = inst; // backpointer
   inst->RootDirectory.Opened = true;
   inst->RootDirectory.Truncated = false; // Root directory cannot be truncated
   if (isFat32)
      // For FAT32, root is a cluster chain; use a large safe size
      inst->RootDirectory.Public.Size = 0x1000000; // 16 MiB max
   else
      inst->RootDirectory.Public.Size =
          sizeof(FAT_DirectoryEntry) * inst->BS.BootSector.DirEntryCount;
   if (isFat32)
   {
      inst->RootDirectory.FirstCluster = rootDirCluster;
      inst->RootDirectory.CurrentCluster = rootDirCluster;
      inst->RootDirectory.CurrentSectorInCluster = 0;

      // Read first sector of root cluster into buffer
      if (!Partition_ReadSectors(disk, FAT_ClusterToLba(inst, rootDirCluster),
                                 1, inst->RootDirectory.Buffer))
      {
         logfmt(LOG_WARNING,
                "[FAT] Warning: could not pre-load FAT32 root dir sector\n");
      }
   }
   else
   {
      // For FAT12/16 we treat FirstCluster/CurrentCluster as the starting LBA
      inst->RootDirectory.FirstCluster = rootDirLba;
      inst->RootDirectory.CurrentCluster = rootDirLba;
      inst->RootDirectory.CurrentSectorInCluster = 0;

      // Read first sector of root directory from disk
      if (!Partition_ReadSectors(disk, rootDirLba, 1,
                                 inst->RootDirectory.Buffer))
      {
         logfmt(LOG_WARNING,
                "[FAT] Warning: could not pre-load FAT12/16 root dir sector\n");
      }
   }

   inst->RootDirectory.ParentCluster = inst->RootDirectory.FirstCluster;
   inst->RootDirectory.ParentIsRoot = true;

   // Detect FAT type (12 / 16 / 32)
   FAT_Detect(inst);

   // reset opened files
   for (int i = 0; i < MAX_FILE_HANDLES; i++)
   {
      inst->OpenedFiles[i].Opened = false;
      inst->OpenedFiles[i].Truncated = false;
   }

   return inst;
}

/* $LBA_{cluster} = DataSectionLba + (cluster - 2) \times SectorsPerCluster$ */
static uint32_t FAT_ClusterToLba(const FAT_Instance *inst, uint32_t cluster)
{
   return inst->DataSectionLba +
          (cluster - 2) * inst->BS.BootSector.SectorsPerCluster;
}

// Write a FAT entry for the given cluster across all FAT copies and update the
// in-memory cache if it covers the written sector. Value should be masked to
// the appropriate width by caller (e.g., EOF marker or 0 for free).
static bool FAT_WriteFatEntry(FAT_Instance *inst, Partition *disk,
                              uint32_t cluster, uint32_t value)
{
   if (!inst || !disk)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_WriteFatEntry: inst or disk is NULL\n");
      return false;
   }

   uint32_t fatByteOffset;
   if (inst->FatType == 12)
      fatByteOffset = cluster * 3 / 2;
   else if (inst->FatType == 16)
      fatByteOffset = cluster * 2;
   else
      fatByteOffset = cluster * 4;

   uint32_t fatSectorOffset = fatByteOffset / SECTOR_SIZE;
   uint32_t fatByteOffsetInSector = fatByteOffset % SECTOR_SIZE;

   // Iterate over all FAT copies
   for (uint32_t fatIdx = 0; fatIdx < inst->BS.BootSector.FatCount; fatIdx++)
   {
      uint32_t fatSectorLba = inst->BS.BootSector.ReservedSectors +
                              fatIdx * inst->SectorsPerFat + fatSectorOffset;

      uint8_t fatBuffer[SECTOR_SIZE * 2];
      if (!Partition_ReadSectors(disk, fatSectorLba, 1, fatBuffer))
         return false;

      bool crossBoundary =
          (inst->FatType == 12 && fatByteOffsetInSector == SECTOR_SIZE - 1);
      if (crossBoundary)
      {
         if (!Partition_ReadSectors(disk, fatSectorLba + 1, 1,
                                    fatBuffer + SECTOR_SIZE))
            return false;
      }

      if (inst->FatType == 12)
      {
         uint16_t *p = (uint16_t *)(fatBuffer + fatByteOffsetInSector);
         if (cluster % 2 == 0)
            *p = (*p & 0xF000) | (value & 0x0FFF);
         else
            *p = (*p & 0x000F) | ((value & 0x0FFF) << 4);
      }
      else if (inst->FatType == 16)
      {
         *(uint16_t *)(fatBuffer + fatByteOffsetInSector) = (uint16_t)value;
      }
      else // FAT32
      {
         uint32_t *entry = (uint32_t *)(fatBuffer + fatByteOffsetInSector);
         uint32_t oldValue = *entry;
         // Preserve top 4 bits, set lower 28 bits
         *entry = (*entry & 0xF0000000) | (value & 0x0FFFFFFF);
         if (cluster >= 9564 && cluster <= 9580)
         {
            logfmt(LOG_INFO,
                   "[FAT] FAT_WriteFatEntry: cluster=%u, oldValue=0x%08x, "
                   "newValue=0x%08x, LBA=%u, offset=%u\n",
                   cluster, oldValue, *entry, fatSectorLba,
                   fatByteOffsetInSector);
         }
      }

      if (!Partition_WriteSectors(disk, fatSectorLba, 1, fatBuffer))
         return false;

      if (crossBoundary)
      {
         if (!Partition_WriteSectors(disk, fatSectorLba + 1, 1,
                                     fatBuffer + SECTOR_SIZE))
            return false;
      }
   }

   // Update cache if this sector is currently cached (cache covers FAT copy 0)
   if (inst->FatCachePos != 0xFFFFFFFF)
   {
      // Check first sector
      if (fatSectorOffset >= inst->FatCachePos &&
          fatSectorOffset < inst->FatCachePos + FAT_CACHE_SIZE)
      {
         uint8_t *cache = inst->FatCache +
                          (fatSectorOffset - inst->FatCachePos) * SECTOR_SIZE;
         if (inst->FatType == 12)
         {
            // Crossing a sector boundary in the cache is complex; just
            // invalidate to stay safe.
            inst->FatCachePos = 0xFFFFFFFF;
         }
         else if (inst->FatType == 16)
         {
            *(uint16_t *)(cache + fatByteOffsetInSector) = (uint16_t)value;
         }
         else // FAT32
         {
            uint32_t *entry = (uint32_t *)(cache + fatByteOffsetInSector);
            // Preserve top 4 bits, set lower 28 bits
            *entry = (*entry & 0xF0000000) | (value & 0x0FFFFFFF);
         }
      }

      // Simpler to just invalidate cache for FAT12 to avoid boundary issues.
      if (inst->FatType == 12) inst->FatCachePos = 0xFFFFFFFF;
   }

   return true;
}

static FAT_File *FAT_OpenEntry(FAT_Instance *inst, Partition *disk,
                               FAT_DirectoryEntry *entry, FAT_FileData *parent)
{
   // find empty handle
   int handle = -1;
   for (int i = 0; i < MAX_FILE_HANDLES && handle < 0; i++)
   {
      if (!inst->OpenedFiles[i].Opened) handle = i;
   }

   // out of handles
   if (handle < 0)
   {
      return NULL;
   }

   // setup vars
   FAT_FileData *fd = &inst->OpenedFiles[handle];
   fd->Public.Handle = handle;
   fd->Public.IsDirectory = (entry->Attributes & FAT_ATTRIBUTE_DIRECTORY) != 0;
   fd->Public.Position = 0;
   fd->Public.Size = entry->Size;
   fd->Public.instance = inst; // backpointer so callers don't need Partition*
   fd->Truncated = false;      // Not yet truncated
   memcpy(fd->Public.Name, entry->Name, 11); // Save the name
   fd->FirstCluster =
       entry->FirstClusterLow + ((uint32_t)entry->FirstClusterHigh << 16);

   // Validate cluster number
   if (fd->FirstCluster != 0 && fd->Public.Size > 0)
   {
      uint32_t maxClusters = (inst->TotalSectors - inst->DataSectionLba) /
                             inst->BS.BootSector.SectorsPerCluster;
      if (fd->FirstCluster < 2 || fd->FirstCluster >= maxClusters + 2)
      {
         logfmt(LOG_ERROR, "[FAT] invalid FirstCluster=%u (max=%u) for file\n",
                fd->FirstCluster, maxClusters + 2);
         return NULL;
      }
   }

   // Record parent directory information for later updates
   if (parent != NULL)
   {
      fd->ParentCluster = parent->FirstCluster;
      fd->ParentIsRoot = (parent == &inst->RootDirectory);
   }
   else
   {
      // Fallback: assume root
      fd->ParentCluster = inst->RootDirectory.FirstCluster;
      fd->ParentIsRoot = true;
   }

   fd->CurrentCluster = fd->FirstCluster;
   fd->CurrentSectorInCluster = 0;

   /* Skip the initial sector read only when the entry has no data cluster.
    * Directories frequently have Size=0 on FAT but still require reading their
    * first cluster for iteration. */
   if (fd->FirstCluster == 0)
   {
      fd->Opened = true;
      return &fd->Public;
   }

   /* Guard against bogus cluster numbers that would underflow LBA math */
   if (fd->FirstCluster < 2)
   {
      logfmt(LOG_ERROR,
             "[FAT] invalid FirstCluster=%u for file, refusing to open\n",
             fd->FirstCluster);
      return NULL;
   }

   uint32_t lba = FAT_ClusterToLba(inst, fd->CurrentCluster);

   if (!Partition_ReadSectors(disk, lba, 1, fd->Buffer))
   {
      logfmt(LOG_ERROR,
             "[FAT] open entry failed - read error cluster=%u lba=%u\n",
             fd->CurrentCluster, lba);
      // Don't open the file if we can't read its data
      return NULL;
   }

   fd->Opened = true;
   return &fd->Public;
}

static uint32_t FAT_NextCluster(FAT_Instance *inst, Partition *disk,
                                uint32_t currentCluster)
{
   uint32_t fatIndex = 0;

   if (inst->FatType == 12)
      fatIndex = currentCluster * 3 / 2;
   else if (inst->FatType == 16)
      fatIndex = currentCluster * 2;
   else /* FAT32 */
      fatIndex = currentCluster * 4;

   uint32_t fatIndexSector = fatIndex / SECTOR_SIZE;
   if (fatIndexSector < inst->FatCachePos ||
       fatIndexSector >= inst->FatCachePos + FAT_CACHE_SIZE)
   {
      if (!FAT_ReadFat(inst, disk, fatIndexSector))
      {
         logfmt(LOG_ERROR,
                "[FAT] FAT_NextCluster: FAT_ReadFat failed for sector %u\n",
                fatIndexSector);
         return 0xFFFFFFFF; // Return EOC marker to stop cluster traversal
      }
      inst->FatCachePos = fatIndexSector;
   }

   fatIndex -= (inst->FatCachePos * SECTOR_SIZE);
   uint32_t nextCluster = 0xFFFFFFFF;
   if (inst->FatType == 12)
   {
      if (currentCluster % 2 == 0)
         nextCluster = (*(uint16_t *)(inst->FatCache + fatIndex)) & 0x0fff;
      else
         nextCluster = (*(uint16_t *)(inst->FatCache + fatIndex)) >> 4;

      if (nextCluster >= 0xff8) nextCluster |= 0xfffff000;
   }
   else if (inst->FatType == 16)
   {
      nextCluster = *(uint16_t *)(inst->FatCache + fatIndex);
      if (nextCluster >= 0xfff8) nextCluster |= 0xffff0000;
   }
   else /* FAT32 */
   {
      uint32_t raw = *(uint32_t *)(inst->FatCache + fatIndex);
      nextCluster = raw & 0x0FFFFFFF;
   }
   return nextCluster;
}

static bool fat_metadata_append_record_full(Partition *disk, const char *path,
                                            uint16_t mode, uint32_t uid,
                                            uint32_t gid, uint8_t flags)
{
   if (!disk || !path) return false;

   char normalizedPath[MAX_PATH_SIZE];
   fat_normalize_absolute_path(path, normalizedPath, sizeof(normalizedPath));
   if (fat_is_metadata_path(normalizedPath)) return true;

   FAT_File *meta = FAT_Open(disk, FAT_METADATA_PATH);
   if (!meta)
   {
      meta = FAT_Create(disk, FAT_METADATA_PATH, 0600u);
      if (!meta) return false;
   }

   if (meta->Handle == ROOT_DIRECTORY_HANDLE || meta->IsDirectory)
   {
      if (meta->Handle != ROOT_DIRECTORY_HANDLE) FAT_Close(meta);
      return false;
   }

   FAT_MetadataRecord record;
   memset(&record, 0, sizeof(record));
   SHA1_Calculate(normalizedPath, strlen(normalizedPath), record.Hash);
   record.Mode = fat_normalize_mode(mode);
   record.Uid = uid;
   record.Gid = gid;
   record.Flags = FAT_METADATA_FLAG_VALID | flags;

   if (!FAT_Seek(disk, meta, meta->Size))
   {
      FAT_Close(meta);
      return false;
   }

   uint32_t written = FAT_Write(disk, meta, sizeof(record), &record);
   FAT_Close(meta);
   return written == sizeof(record);
}

static bool fat_metadata_append_record(Partition *disk, const char *path,
                                       uint16_t mode, uint8_t flags)
{
   return fat_metadata_append_record_full(disk, path, mode, 0, 0, flags);
}

static bool fat_metadata_lookup_latest(Partition *disk, const char *path,
                                       FAT_MetadataRecord *recordOut,
                                       bool *foundOut)
{
   if (!disk || !path)
   {
      if (foundOut) *foundOut = false;
      return false;
   }

   char normalizedPath[MAX_PATH_SIZE];
   fat_normalize_absolute_path(path, normalizedPath, sizeof(normalizedPath));

   uint8_t hash[SHA1_DIGEST_SIZE];
   SHA1_Calculate(normalizedPath, strlen(normalizedPath), hash);

   FAT_File *meta = FAT_Open(disk, FAT_METADATA_PATH);
   if (!meta)
   {
      if (foundOut) *foundOut = false;
      return true;
   }

   if (meta->Handle == ROOT_DIRECTORY_HANDLE || meta->IsDirectory)
   {
      if (meta->Handle != ROOT_DIRECTORY_HANDLE) FAT_Close(meta);
      if (foundOut) *foundOut = false;
      return false;
   }

   if (!FAT_Seek(disk, meta, 0))
   {
      FAT_Close(meta);
      if (foundOut) *foundOut = false;
      return false;
   }

   FAT_MetadataRecord rec;
   bool found = false;
   while (FAT_Read(disk, meta, sizeof(rec), &rec) == sizeof(rec))
   {
      if ((rec.Flags & FAT_METADATA_FLAG_VALID) == 0) continue;
      if (memcmp(rec.Hash, hash, SHA1_DIGEST_SIZE) == 0)
      {
         if (recordOut) *recordOut = rec;
         found = true;
      }
   }

   FAT_Close(meta);
   if (foundOut) *foundOut = found;
   return true;
}

static bool fat_check_access_path(Partition *disk, const char *path,
                                  uint32_t uid, uint32_t gid,
                                  uint8_t accessMask)
{
   if (!disk || !path) return false;
   if (uid == 0) return true;

   FAT_MetadataRecord rec;
   bool found = false;
   if (!fat_metadata_lookup_latest(disk, path, &rec, &found)) return false;
   if (!found) return true;
   if (rec.Flags & FAT_METADATA_FLAG_DELETED) return false;

   uint8_t perm;
   if (uid == rec.Uid)
      perm = (rec.Mode >> 6) & 0x7;
   else if (gid == rec.Gid)
      perm = (rec.Mode >> 3) & 0x7;
   else
      perm = rec.Mode & 0x7;

   if ((accessMask & 0x4) && ((perm & 0x4) == 0)) return false;
   if ((accessMask & 0x2) && ((perm & 0x2) == 0)) return false;
   if ((accessMask & 0x1) && ((perm & 0x1) == 0)) return false;
   return true;
}

static bool fat_chmod_path(Partition *disk, const char *path, uint16_t mode)
{
   if (!disk || !path) return false;

   FAT_MetadataRecord rec;
   bool found = false;
   if (!fat_metadata_lookup_latest(disk, path, &rec, &found)) return false;

   uint32_t uid = found ? rec.Uid : 0;
   uint32_t gid = found ? rec.Gid : 0;
   return fat_metadata_append_record_full(disk, path, mode, uid, gid, 0);
}

static bool fat_chown_path(Partition *disk, const char *path, uint32_t uid,
                           uint32_t gid)
{
   if (!disk || !path) return false;

   FAT_MetadataRecord rec;
   bool found = false;
   if (!fat_metadata_lookup_latest(disk, path, &rec, &found)) return false;

   uint16_t mode = found ? rec.Mode : 0644u;
   return fat_metadata_append_record_full(disk, path, mode, uid, gid, 0);
}

uint32_t FAT_Read(Partition *disk, FAT_File *file, uint32_t byteCount,
                  void *dataOut)
{
   FAT_Instance *inst = fat_inst(disk);
   if (!inst) return 0;

   // Validate file handle before accessing array
   if (!file || !dataOut) return 0;
   if (file->Handle != ROOT_DIRECTORY_HANDLE &&
       (file->Handle < 0 || file->Handle >= MAX_FILE_HANDLES))
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Read: invalid file handle %d\n",
             file->Handle);
      return 0;
   }

   // get file data
   FAT_FileData *fd = (file->Handle == ROOT_DIRECTORY_HANDLE)
                          ? &inst->RootDirectory
                          : &inst->OpenedFiles[file->Handle];

   uint8_t *u8DataOut = (uint8_t *)dataOut;

   // For regular files (not directories), don't read empty files
   if (fd->Public.Size == 0 && !fd->Public.IsDirectory)
   {
      logfmt(LOG_WARNING,
             "[FAT] FAT_Read: file is empty (Size=0), returning 0 bytes, "
             "IsDirectory=%u\n",
             fd->Public.IsDirectory);
      return 0;
   }

   // don't read past the end of the file (once size is known)
   // For directories, Size becomes > 0 only after hitting the end of the cluster chain.
   if (fd->Public.Size > 0)
   {
      if (fd->Public.Position >= fd->Public.Size) return 0;
      byteCount = min(byteCount, fd->Public.Size - fd->Public.Position);
   }

   // For root directory in FAT32, limit reading to a reasonable max size
   if (fd->Public.Handle == ROOT_DIRECTORY_HANDLE && inst->FatType == 32)
   {
      // Root dir should not exceed a few clusters, limit to prevent infinite
      // reads
      uint32_t maxRootSize = 0x1000000; // 16 MiB max (as set in FAT_Initialize)
      if (fd->Public.Position + byteCount > maxRootSize)
      {
         byteCount = min(byteCount, maxRootSize - fd->Public.Position);
      }
   }

   uint32_t loop_counter = 0; // reset per read call

   while (byteCount > 0)
   {
      uint32_t leftInBuffer = SECTOR_SIZE - (fd->Public.Position % SECTOR_SIZE);
      uint32_t take = min(byteCount, leftInBuffer);

      memcpy(u8DataOut, fd->Buffer + fd->Public.Position % SECTOR_SIZE, take);
      u8DataOut += take;
      fd->Public.Position += take;
      byteCount -= take;

      if (leftInBuffer == take ||
          (fd->Public.Position > 0 && fd->Public.Position % SECTOR_SIZE == 0))
      {
         // Prevent infinite loops - safety check (per call)
         if (++loop_counter > 10000)
         {
            logfmt(LOG_ERROR,
                   "[FAT] FAT_Read: infinite loop detected, breaking\n");
            break;
         }
         // Special handling for root directory
         if (fd->Public.Handle == ROOT_DIRECTORY_HANDLE)
         {
            if (inst->FatType == 32)
            {
               // cluster-based root directory (FAT32)
               if (++fd->CurrentSectorInCluster >=
                   inst->BS.BootSector.SectorsPerCluster)
               {
                  fd->CurrentSectorInCluster = 0;
                  uint32_t next =
                      FAT_NextCluster(inst, disk, fd->CurrentCluster);

                  // Treat 0 as end-of-chain to avoid scanning free space
                  if (next < 2)
                  {
                     fd->Public.Size = fd->Public.Position;
                     break;
                  }

                  fd->CurrentCluster = next;
               }

               // Check for end-of-chain
               uint32_t eofMarker = 0xFFFFFFF8;
               if (fd->CurrentCluster >= eofMarker)
               {
                  fd->Public.Size = fd->Public.Position;
                  break;
               }

               if (!Partition_ReadSectors(
                       disk,
                       FAT_ClusterToLba(inst, fd->CurrentCluster) +
                           fd->CurrentSectorInCluster,
                       1, fd->Buffer))
               {
                  logfmt(LOG_ERROR, "[FAT] read error!\n");
                  break;
               }
            }
            else
            {
               // legacy root directory stored in reserved area (sector indexed)
               ++fd->CurrentCluster;

               if (fd->CurrentCluster >=
                   inst->RootDirLba + inst->RootDirSectors)
               {
                  fd->Public.Size = fd->Public.Position;
                  break;
               }

               if (!Partition_ReadSectors(disk, fd->CurrentCluster, 1,
                                          fd->Buffer))
               {
                  logfmt(LOG_ERROR, "[FAT] read error!\n");
                  break;
               }
            }
         }
         else
         {
            // calculate next cluster & sector to read
            if (++fd->CurrentSectorInCluster >=
                inst->BS.BootSector.SectorsPerCluster)
            {
               fd->CurrentSectorInCluster = 0;
               uint32_t next = FAT_NextCluster(inst, disk, fd->CurrentCluster);

               // Treat 0 (free) or invalid as EOF to avoid looping into free
               // space
               if (next < 2)
               {
                  fd->Public.Size = fd->Public.Position;
                  break;
               }

               fd->CurrentCluster = next;
            }

            // Check for end-of-chain based on FAT type
            uint32_t eofMarker = (inst->FatType == 12)   ? 0xFF8
                                 : (inst->FatType == 16) ? 0xFFF8
                                                         : 0x0FFFFFF8;
            if (fd->CurrentCluster >= eofMarker)
            {
               // Mark end of file
               fd->Public.Size = fd->Public.Position;
               break;
            }

            // read next sector
            if (!Partition_ReadSectors(
                    disk,
                    FAT_ClusterToLba(inst, fd->CurrentCluster) +
                        fd->CurrentSectorInCluster,
                    1, fd->Buffer))
            {
               logfmt(LOG_ERROR, "[FAT] read error!\n");
               break;
            }
         }
      }
   }

   return u8DataOut - (uint8_t *)dataOut;
}

bool FAT_ReadEntry(Partition *disk, FAT_File *file,
                   FAT_DirectoryEntry *dirEntry)
{
   uint32_t bytes_read =
       FAT_Read(disk, file, sizeof(FAT_DirectoryEntry), dirEntry);
   return bytes_read == sizeof(FAT_DirectoryEntry);
}

void FAT_Close(FAT_File *file)
{
   if (!file) return;

   FAT_Instance *inst = (FAT_Instance *)file->instance;
   if (!inst) return;

   if (file->Handle == ROOT_DIRECTORY_HANDLE)
   {
      file->Position = 0;
      inst->RootDirectory.CurrentCluster = inst->RootDirectory.FirstCluster;
   }
   else
   {
      // Validate handle before accessing array
      if (file->Handle < 0 || file->Handle >= MAX_FILE_HANDLES)
      {
         logfmt(LOG_ERROR, "[FAT] FAT_Close: invalid file handle %d\n",
                file->Handle);
         return;
      }
      inst->OpenedFiles[file->Handle].Opened = false;
   }
}

bool FAT_FindFile(Partition *disk, FAT_File *file, const char *name,
                  FAT_DirectoryEntry *entryOut)
{
   // Reject paths; this helper expects a single 8.3 component
   if (strchr(name, '/'))
   {
      logfmt(
          LOG_WARNING,
          "[FAT] FAT_FindFile: received path '%s', expected single component\n",
          name);
      return false;
   }

   char fatName[12];
   FAT_DirectoryEntry entry;

   // Reset directory position to start searching from the beginning
   if (!FAT_Seek(disk, file, 0))
   {
      logfmt(LOG_ERROR, "[FAT] FAT_FindFile: FAT_Seek(0) failed for '%s'\n",
             name);
      return false;
   }

   // convert from name to fat name
   memset(fatName, ' ', sizeof(fatName));
   fatName[11] = '\0';

   const char *ext = strchr(name, '.');
   if (ext == NULL)
      ext = name + strlen(name); // Point to end of string if no extension

   // Copy basename (max 8 chars before extension)
   int nameLen = (ext - name > 8) ? 8 : (ext - name);

   for (int i = 0; i < nameLen && name[i] && name[i] != '.'; i++)
      fatName[i] = toupper(name[i]);

   // Copy extension (max 3 chars after the dot)
   if (ext != name + strlen(name) && *ext == '.')
   {
      for (int i = 0; i < 3 && ext[i + 1]; i++)
         fatName[i + 8] = toupper(ext[i + 1]);
   }

   while (FAT_ReadEntry(disk, file, &entry))
   {
      // FAT end marker: empty entry means end of directory
      if (entry.Name[0] == 0x00) break;

      // Skip LFN entries (attribute 0x0F)
      if ((entry.Attributes & 0x0F) == 0x0F) continue;

      if (memcmp(fatName, entry.Name, 11) == 0)
      {
         *entryOut = entry;
         return true;
      }
   }
   return false;
}

FAT_File *FAT_Open(Partition *disk, const char *path)
{
   if (!path) return NULL;

   FAT_Instance *inst = fat_inst(disk);
   if (!inst) return NULL;

   char *normalizedPath = kmalloc(MAX_PATH_SIZE);
   char *name = kmalloc(MAX_PATH_SIZE);
   if (!normalizedPath || !name)
   {
      if (normalizedPath) free(normalizedPath);
      if (name) free(name);
      return NULL;
   }

   strncpy(normalizedPath, path, MAX_PATH_SIZE - 1);
   normalizedPath[MAX_PATH_SIZE - 1] = '\0';

   const char *cursor = normalizedPath;
   if (*cursor == '/') cursor++;

   // If path is empty or just "/", return root directory
   if (*cursor == '\0')
   {
      // Root directory handle is shared; always rewind on open so callers
      // observe deterministic directory iteration.
      FAT_Seek(disk, &inst->RootDirectory.Public, 0);
      free(normalizedPath);
      free(name);
      return &inst->RootDirectory.Public;
   }

   FAT_File *current = &inst->RootDirectory.Public;
   FAT_File *previous = NULL;

   while (*cursor)
   {
      bool isLast = false;
      const char *delim = strchr(cursor, '/');
      if (delim != NULL)
      {
         size_t len = (size_t)(delim - cursor);
         if (len >= MAX_PATH_SIZE) len = MAX_PATH_SIZE - 1;
         memcpy(name, cursor, len);
         name[len] = '\0';
         cursor = delim + 1;
      }
      else
      {
         size_t len = strlen(cursor);
         if (len >= MAX_PATH_SIZE) len = MAX_PATH_SIZE - 1;
         memcpy(name, cursor, len);
         name[len] = '\0';
         cursor += len;
         isLast = true;
      }

      FAT_DirectoryEntry entry;
      if (FAT_FindFile(disk, current, name, &entry))
      {
         if (previous != NULL && previous->Handle != ROOT_DIRECTORY_HANDLE)
         {
            FAT_Close(previous);
         }

         if (!isLast && (entry.Attributes & FAT_ATTRIBUTE_DIRECTORY) == 0)
         {
            logfmt(LOG_ERROR, "[FAT] %s not a directory\n", name);
            if (current != NULL && current->Handle != ROOT_DIRECTORY_HANDLE)
               FAT_Close(current);
            free(normalizedPath);
            free(name);
            return NULL;
         }

         FAT_FileData *parentData = (current->Handle == ROOT_DIRECTORY_HANDLE)
                                        ? &inst->RootDirectory
                                        : &inst->OpenedFiles[current->Handle];

         previous = current;
         current = FAT_OpenEntry(inst, disk, &entry, parentData);
      }
      else
      {
         if (isLast)
         {
            /* File not found.  FAT_Open is strictly read/navigate-only.
             * Callers that need to create a new file must use FAT_Create
             * (or VFS_Create at the VFS layer) explicitly. */
            if (current != NULL && current->Handle != ROOT_DIRECTORY_HANDLE)
            {
               FAT_Close(current);
            }
            if (previous != NULL && previous->Handle != ROOT_DIRECTORY_HANDLE)
            {
               FAT_Close(previous);
            }

            logfmt(LOG_INFO, "[FAT] %s not found\n", name);
            free(normalizedPath);
            free(name);
            return NULL;
         }
         else
         {
            if (previous != NULL && previous->Handle != ROOT_DIRECTORY_HANDLE)
            {
               FAT_Close(previous);
            }
            if (current != NULL && current->Handle != ROOT_DIRECTORY_HANDLE)
            {
               FAT_Close(current);
            }

            logfmt(LOG_WARNING, "[FAT] %s not found\n", name);
            free(normalizedPath);
            free(name);
            return NULL;
         }
      }
   }

   if (previous != NULL && previous != current &&
       previous->Handle != ROOT_DIRECTORY_HANDLE)
   {
      FAT_Close(previous);
   }

   free(normalizedPath);
   free(name);
   return current;
}

bool FAT_Seek(Partition *disk, FAT_File *file, uint32_t position)
{
   if (!disk)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Seek: disk is NULL\n");
      return false;
   }

   FAT_Instance *inst = fat_inst(disk);
   if (!inst) return false;

   if (!file) return false;

   // Validate handle before accessing array
   if (file->Handle != ROOT_DIRECTORY_HANDLE &&
       (file->Handle < 0 || file->Handle >= MAX_FILE_HANDLES))
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Seek: invalid file handle %d\n",
             file->Handle);
      return false;
   }

   FAT_FileData *fd = (file->Handle == ROOT_DIRECTORY_HANDLE)
                          ? &inst->RootDirectory
                          : &inst->OpenedFiles[file->Handle];

   // don't seek past end (but allow seeks in directories since they don't
   // track size)
   if (!fd->Public.IsDirectory && position > fd->Public.Size)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Seek: position %u > size %u\n", position,
             fd->Public.Size);
      return false;
   }

   fd->Public.Position = position;

   // compute cluster/sector for the position
   uint32_t bytesPerSector = inst->BS.BootSector.BytesPerSector;
   uint32_t sectorsPerCluster = inst->BS.BootSector.SectorsPerCluster;

   // Guard against divide-by-zero from invalid FAT parameters
   if (bytesPerSector == 0 || sectorsPerCluster == 0)
   {
      logfmt(LOG_ERROR,
             "[FAT] FAT_Seek: invalid FAT parameters (BytesPerSector=%u, "
             "SectorsPerCluster=%u)\n",
             bytesPerSector, sectorsPerCluster);
      return false;
   }

   uint32_t clusterBytes = bytesPerSector * sectorsPerCluster;

   if (fd->Public.Handle == ROOT_DIRECTORY_HANDLE)
   {
      if (inst->FatType == 32)
      {
         uint32_t clusterIndex = position / clusterBytes;
         uint32_t sectorInCluster = (position % clusterBytes) / bytesPerSector;

         uint32_t cluster = fd->FirstCluster;
         for (uint32_t i = 0; i < clusterIndex; i++)
         {
            cluster = FAT_NextCluster(inst, disk, cluster);
            uint32_t eofMarker = 0xFFFFFFF8;
            if (cluster >= eofMarker)
            {
               fd->Public.Size = fd->Public.Position;
               return false;
            }
         }

         fd->CurrentCluster = cluster;
         fd->CurrentSectorInCluster = sectorInCluster;

         if (!Partition_ReadSectors(disk,
                                    FAT_ClusterToLba(inst, fd->CurrentCluster) +
                                        fd->CurrentSectorInCluster,
                                    1, fd->Buffer))
         {
            logfmt(LOG_ERROR, "[FAT] seek read error (root)\n");
            return false;
         }
      }
      else
      {
         // root directory is organized by sectors (not clusters)
         uint32_t sectorIndex = position / bytesPerSector;
         uint32_t newCluster = fd->FirstCluster + sectorIndex;

         /* Only re-read from disk if moving to a different sector.
          * Seeking back to position 0 when the buffer already holds the
          * first root-dir sector (loaded by FAT_Initialize) avoids a
          * redundant FDC read that could fail if the motor was off. */
         bool needsRead = (fd->CurrentCluster != newCluster);
         fd->CurrentCluster = newCluster;
         fd->CurrentSectorInCluster = 0;

         if (needsRead &&
             !Partition_ReadSectors(disk, fd->CurrentCluster, 1, fd->Buffer))
         {
            logfmt(LOG_ERROR, "[FAT] seek read error (root)\n");
            return false;
         }
      }
   }
   else
   {
      // Empty regular files only support seek to BOF.
      // This allows first read/write at offset 0 without forcing a failure.
      if (fd->Public.Size == 0 && !fd->Public.IsDirectory)
      {
         if (position == 0) return true;

         logfmt(LOG_ERROR,
                "[FAT] FAT_Seek: cannot seek to non-zero offset on empty "
                "regular file\n");
         return false;
      }

      if (fd->FirstCluster == 0)
      {
         logfmt(
             LOG_ERROR,
             "[FAT] FAT_Seek: FirstCluster is 0 for non-empty file (size=%u)\n",
             fd->Public.Size);
         return false;
      }

      uint32_t clusterIndex = position / clusterBytes;
      uint32_t sectorInCluster = (position % clusterBytes) / bytesPerSector;

      // walk cluster chain clusterIndex times from first cluster
      uint32_t c = fd->FirstCluster;
      for (uint32_t i = 0; i < clusterIndex; i++)
      {
         c = FAT_NextCluster(inst, disk, c);
         uint32_t eofMarker = (inst->FatType == 12)   ? 0xFF8
                              : (inst->FatType == 16) ? 0xFFF8
                                                      : 0x0FFFFFF8;
         if (c >= eofMarker)
         {
            // invalid / end of chain
            fd->Public.Size = fd->Public.Position;
            logfmt(LOG_WARNING,
                   "[FAT] FAT_Seek: reached end of cluster chain\n");
            return false;
         }
      }

      fd->CurrentCluster = c;
      fd->CurrentSectorInCluster = sectorInCluster;

      if (!Partition_ReadSectors(disk,
                                 FAT_ClusterToLba(inst, fd->CurrentCluster) +
                                     fd->CurrentSectorInCluster,
                                 1, fd->Buffer))
      {
         logfmt(LOG_ERROR, "[FAT] seek read error (file)\n");
         return false;
      }
   }

   return true;
}

bool FAT_WriteEntry(Partition *disk, FAT_File *file,
                    const FAT_DirectoryEntry *dirEntry)
{
   // Allow writing into root directory as well as opened directory files.
   if (!file) return false;

   FAT_Instance *inst = fat_inst(disk);
   if (!inst) return false;

   FAT_FileData *fd;
   bool isRoot = (file->Handle == ROOT_DIRECTORY_HANDLE);
   if (isRoot)
      fd = &inst->RootDirectory;
   else
   {
      if (file->Handle < 0 || file->Handle >= MAX_FILE_HANDLES) return false;
      fd = &inst->OpenedFiles[file->Handle];
   }

   if (!file->IsDirectory)
   {
      logfmt(LOG_ERROR, "[FAT] WriteEntry called on non-directory file\n");
      return false;
   }

   // Calculate which sector and offset contains the current directory entry
   uint32_t entryOffset = file->Position;
   uint32_t sectorIndex = entryOffset / SECTOR_SIZE;
   uint32_t offsetInSector = entryOffset % SECTOR_SIZE;

   // Determine absolute LBA for this sector
   uint32_t sectorLba = 0;
   if (!isRoot || inst->FatType == 32)
   {
      sectorLba = FAT_ClusterToLba(inst, fd->CurrentCluster) +
                  fd->CurrentSectorInCluster;
   }
   else
   {
      // legacy root - contiguous area
      sectorLba = inst->RootDirLba + sectorIndex;
   }

   // Read the sector to modify it
   uint8_t *sectorBuffer = kmalloc(SECTOR_SIZE);
   if (!sectorBuffer)
   {
      logfmt(LOG_ERROR, "[FAT] WriteEntry kmalloc failed\n");
      return false;
   }
   if (!Partition_ReadSectors(disk, sectorLba, 1, sectorBuffer))
   {
      logfmt(LOG_ERROR, "[FAT] WriteEntry read error\n");
      free(sectorBuffer);
      return false;
   }

   memcpy(&sectorBuffer[offsetInSector], dirEntry, sizeof(FAT_DirectoryEntry));

   if (!Partition_WriteSectors(disk, sectorLba, 1, sectorBuffer))
   {
      logfmt(LOG_ERROR, "[FAT] WriteEntry write error\n");
      free(sectorBuffer);
      return false;
   }

   // Update the file descriptor's buffer with the modified sector
   // so that subsequent reads see the updated entry
   memcpy(fd->Buffer, sectorBuffer, SECTOR_SIZE);
   free(sectorBuffer);

   // Advance position by one directory entry (bytes)
   file->Position += sizeof(FAT_DirectoryEntry);
   return true;
}

uint32_t FAT_Write(Partition *disk, FAT_File *file, uint32_t byteCount,
                   const void *dataIn)
{
   // Don't write to directories or root
   if (!file || file->IsDirectory || file->Handle == ROOT_DIRECTORY_HANDLE)
   {
      logfmt(LOG_ERROR,
             "[FAT] FAT_Write: cannot write to directory or null file\n");
      return 0;
   }

   // Validate file handle BEFORE accessing array
   if (file->Handle < 0 || file->Handle >= MAX_FILE_HANDLES)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Write: invalid file handle %d\n",
             file->Handle);
      return 0;
   }

   FAT_Instance *inst = fat_inst(disk);
   if (!inst) return 0;

   // get file data
   FAT_FileData *fd = &inst->OpenedFiles[file->Handle];

   if (!fd->Opened)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Write: file not opened\n");
      return 0;
   }

   // Validate FAT parameters
   if (inst->BS.BootSector.BytesPerSector == 0 ||
       inst->BS.BootSector.SectorsPerCluster == 0)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Write: invalid BPB parameters\n");
      return 0;
   }

   // Auto-truncate file on first write if it has existing content and hasn't
   // been truncated
   if (!fd->Truncated && fd->Public.Size > 0 && fd->Public.Position == 0)
   {
      logfmt(LOG_INFO,
             "[FAT] FAT_Write: auto-truncating file (Size=%u) before first "
             "write\n",
             fd->Public.Size);
      if (!FAT_Truncate(disk, file))
      {
         logfmt(LOG_ERROR, "[FAT] FAT_Write: auto-truncate failed\n");
         return 0;
      }
      fd->Truncated = true;
   }

   // If writing to a newly created empty file, clear the buffer to avoid stale
   // data
   if (fd->Public.Size == 0 && fd->Public.Position == 0 && !fd->Truncated)
   {
      memset(fd->Buffer, 0, SECTOR_SIZE);
      fd->Truncated = true; // Mark as truncated to avoid re-clearing
   }

   const uint8_t *u8DataIn = (const uint8_t *)dataIn;
   uint32_t bytesWritten = 0;

   while (byteCount > 0)
   {
      // Calculate position within current sector
      uint32_t offsetInSector = fd->Public.Position % SECTOR_SIZE;
      uint32_t spaceInSector = SECTOR_SIZE - offsetInSector;
      uint32_t take = min(byteCount, spaceInSector);

      // Hard guard against buffer overflow: offset must stay within sector
      if (offsetInSector >= SECTOR_SIZE || take > SECTOR_SIZE ||
          offsetInSector + take > SECTOR_SIZE)
      {
         logfmt(LOG_ERROR,
                "[FAT] FAT_Write: offset overflow (pos=%u off=%u take=%u)\n",
                fd->Public.Position, offsetInSector, take);
         return bytesWritten;
      }

      // Copy data to buffer
      memcpy(fd->Buffer + offsetInSector, u8DataIn, take);

      // Update position and counters
      u8DataIn += take;
      fd->Public.Position += take;
      bytesWritten += take;
      byteCount -= take;

      // Update file size if we wrote past the current end
      if (fd->Public.Position > fd->Public.Size)
         fd->Public.Size = fd->Public.Position;

      // Write sector back to disk if we've filled it or reached end of request
      if (offsetInSector + take == SECTOR_SIZE || byteCount == 0)
      {
         uint32_t sectorLba = FAT_ClusterToLba(inst, fd->CurrentCluster) +
                              fd->CurrentSectorInCluster;

         if (!Partition_WriteSectors(disk, sectorLba, 1, fd->Buffer))
         {
            logfmt(LOG_ERROR, "[FAT] FAT_Write: sector write error at LBA %u\n",
                   sectorLba);
            return bytesWritten;
         }

         if (byteCount == 0) break;

         bool needAdvance = (offsetInSector + take == SECTOR_SIZE);

         if (byteCount == 0 && !needAdvance) break;

         // Advance to next sector/cluster if we filled the sector
         if (needAdvance && ++fd->CurrentSectorInCluster >=
                                inst->BS.BootSector.SectorsPerCluster)
         {
            fd->CurrentSectorInCluster = 0;

            // Need next cluster
            uint32_t nextCluster =
                FAT_NextCluster(inst, disk, fd->CurrentCluster);
            uint32_t eofMarker = (inst->FatType == 12)   ? 0xFF8
                                 : (inst->FatType == 16) ? 0xFFF8
                                                         : 0x0FFFFFF8;

            if (nextCluster >= eofMarker)
            {
               // Allocate new cluster
               uint32_t newCluster = 0;
               uint32_t maxClusters =
                   (inst->TotalSectors - inst->DataSectionLba) /
                   inst->BS.BootSector.SectorsPerCluster;

               for (uint32_t testCluster = 2; testCluster < maxClusters;
                    testCluster++)
               {
                  if (FAT_NextCluster(inst, disk, testCluster) == 0)
                  {
                     newCluster = testCluster;
                     break;
                  }
               }

               if (newCluster == 0)
               {
                  logfmt(LOG_ERROR,
                         "[FAT] FAT_Write: no free clusters available\n");
                  return bytesWritten;
               }

               uint32_t eofVal = (inst->FatType == 12)   ? 0x0FFF
                                 : (inst->FatType == 16) ? 0xFFFF
                                                         : 0x0FFFFFFF;

               if (!FAT_WriteFatEntry(inst, disk, fd->CurrentCluster,
                                      newCluster) ||
                   !FAT_WriteFatEntry(inst, disk, newCluster, eofVal))
               {
                  logfmt(LOG_ERROR, "[FAT] FAT_Write: FAT write error "
                                    "linking/marking cluster\n");
                  return bytesWritten;
               }

               // Verify the EOF was actually written
               uint32_t verify = FAT_NextCluster(inst, disk, newCluster);
               if (verify != eofVal)
               {
                  logfmt(LOG_ERROR,
                         "[FAT] FAT_Write: ERROR: linked %u->%u, marked %u as "
                         "EOF (0x%08x), but verify=0x%08x\n",
                         fd->CurrentCluster, newCluster, newCluster, eofVal,
                         verify);
               }

               fd->CurrentCluster = newCluster;
               if (!Partition_ReadSectors(
                       disk, FAT_ClusterToLba(inst, newCluster), 1, fd->Buffer))
               {
                  logfmt(LOG_ERROR,
                         "[FAT] FAT_Write: failed to read new cluster\n");
                  return bytesWritten;
               }
            }
            else
            {
               fd->CurrentCluster = nextCluster;
               if (!Partition_ReadSectors(
                       disk, FAT_ClusterToLba(inst, fd->CurrentCluster), 1,
                       fd->Buffer))
               {
                  logfmt(LOG_ERROR,
                         "[FAT] FAT_Write: failed to read next cluster\n");
                  return bytesWritten;
               }
            }
         }
         else
         {
            if (!Partition_ReadSectors(
                    disk,
                    FAT_ClusterToLba(inst, fd->CurrentCluster) +
                        fd->CurrentSectorInCluster,
                    1, fd->Buffer))
            {
               logfmt(LOG_ERROR,
                      "[FAT] FAT_Write: failed to read next sector\n");
               return bytesWritten;
            }
         }

         if (byteCount == 0) break;
      }
   }

   // Verify the cluster chain integrity
   uint32_t chainLength = 0;
   uint32_t testCluster = fd->FirstCluster;
   uint32_t eofMarker = (inst->FatType == 12)   ? 0xFF8
                        : (inst->FatType == 16) ? 0xFFF8
                                                : 0x0FFFFFF8;

   while (testCluster < eofMarker && chainLength < 100)
   {
      uint32_t next = FAT_NextCluster(inst, disk, testCluster);
      if (next >= eofMarker)
      {
         chainLength++;
         break;
      }
      testCluster = next;
      chainLength++;
      if (next < 2)
      {
         logfmt(
             LOG_ERROR,
             "[FAT] FAT_Write: ERROR - chain broken at cluster %u (next=%u)\n",
             testCluster, next);
         break;
      }
   }

   if (!FAT_UpdateEntry(disk, file))
   {
      char namebuf[12];
      memcpy(namebuf, fd->Public.Name, 11);
      namebuf[11] = '\0';
      logfmt(LOG_ERROR,
             "[FAT] FAT_Write: failed to update directory entry for '%s'\n",
             namebuf);
   }

   return bytesWritten;
}

bool FAT_UpdateEntry(Partition *disk, FAT_File *file)
{
   // Update the directory entry in the *parent* directory of this file.
   if (!file) return false;

   FAT_Instance *inst = fat_inst(disk);
   if (!inst) return false;

   FAT_FileData *fd = (file->Handle == ROOT_DIRECTORY_HANDLE)
                          ? &inst->RootDirectory
                          : &inst->OpenedFiles[file->Handle];

   if (file->Handle != ROOT_DIRECTORY_HANDLE)
   {
      if (file->Handle < 0 || file->Handle >= MAX_FILE_HANDLES) return false;
      if (!fd->Opened) return false;
      inst->RootDirectory.CurrentSectorInCluster = 0;
   }

   // Determine where the parent directory starts
   bool parentIsRoot = fd->ParentIsRoot;
   uint32_t parentCluster = fd->ParentCluster;

   // Guard against bogus parent cluster values (e.g., EOF markers)
   uint32_t eofMarker = (inst->FatType == 12)   ? 0xFF8
                        : (inst->FatType == 16) ? 0xFFF8
                                                : 0x0FFFFFF8;
   if (parentCluster >= eofMarker)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_UpdateEntry: invalid parent cluster %u\n",
             parentCluster);
      return false;
   }

   // Safety caps to avoid runaway loops
   const uint32_t maxSectorsToScan = 4096;
   uint32_t sectorsScanned = 0;

   // Iterate over the parent directory sectors
   if (parentIsRoot && inst->FatType != 32)
   {
      // Legacy FAT12/16 fixed root directory
      for (uint32_t s = 0;
           s < inst->RootDirSectors && sectorsScanned < maxSectorsToScan;
           s++, sectorsScanned++)
      {
         uint8_t *sectorBuffer = kmalloc(SECTOR_SIZE);
         if (!sectorBuffer) return false;
         uint32_t lba = inst->RootDirLba + s;
         if (!Partition_ReadSectors(disk, lba, 1, sectorBuffer))
         {
            logfmt(LOG_ERROR, "[FAT] FAT_UpdateEntry: failed to read root "
                              "directory sector\n");
            free(sectorBuffer);
            return false;
         }

         for (uint32_t i = 0; i < SECTOR_SIZE; i += sizeof(FAT_DirectoryEntry))
         {
            FAT_DirectoryEntry *entry =
                (FAT_DirectoryEntry *)(sectorBuffer + i);
            if ((entry->Attributes & 0x0F) == 0x0F || entry->Name[0] == 0x00)
               continue;
            if (memcmp(entry->Name, fd->Public.Name, 11) == 0)
            {
               FAT_DirectoryEntry updated = *entry;
               updated.Size = fd->Public.Size;
               updated.FirstClusterLow = fd->FirstCluster & 0xFFFF;
               updated.FirstClusterHigh = (fd->FirstCluster >> 16) & 0xFFFF;
               memcpy(sectorBuffer + i, &updated, sizeof(FAT_DirectoryEntry));
               bool result = Partition_WriteSectors(disk, lba, 1, sectorBuffer);
               free(sectorBuffer);
               return result;
            }
         }
         free(sectorBuffer);
      }
   }
   else
   {
      // Cluster-based directory (FAT32 root or any subdirectory)
      uint32_t cluster = parentCluster;

      while (cluster < eofMarker && sectorsScanned < maxSectorsToScan)
      {
         for (uint32_t sec = 0; sec < inst->BS.BootSector.SectorsPerCluster &&
                                sectorsScanned < maxSectorsToScan;
              sec++, sectorsScanned++)
         {
            uint8_t *sectorBuffer = kmalloc(SECTOR_SIZE);
            if (!sectorBuffer) return false;
            uint32_t lba = FAT_ClusterToLba(inst, cluster) + sec;
            if (!Partition_ReadSectors(disk, lba, 1, sectorBuffer))
            {
               logfmt(LOG_ERROR, "[FAT] FAT_UpdateEntry: failed to read "
                                 "directory cluster sector\n");
               free(sectorBuffer);
               return false;
            }

            for (uint32_t i = 0; i < SECTOR_SIZE;
                 i += sizeof(FAT_DirectoryEntry))
            {
               FAT_DirectoryEntry *entry =
                   (FAT_DirectoryEntry *)(sectorBuffer + i);
               if ((entry->Attributes & 0x0F) == 0x0F || entry->Name[0] == 0x00)
                  continue;
               if (memcmp(entry->Name, fd->Public.Name, 11) == 0)
               {
                  FAT_DirectoryEntry updated = *entry;
                  updated.Size = fd->Public.Size;
                  updated.FirstClusterLow = fd->FirstCluster & 0xFFFF;
                  updated.FirstClusterHigh = (fd->FirstCluster >> 16) & 0xFFFF;
                  memcpy(sectorBuffer + i, &updated,
                         sizeof(FAT_DirectoryEntry));
                  bool result =
                      Partition_WriteSectors(disk, lba, 1, sectorBuffer);
                  free(sectorBuffer);
                  return result;
               }
            }
            free(sectorBuffer);
         }

         cluster = FAT_NextCluster(inst, disk, cluster);
      }
   }

   logfmt(LOG_WARNING,
          "[FAT] UpdateEntry - file not found in parent directory\n");
   return false;
}

FAT_File *FAT_Create(Partition *disk, const char *path, uint16_t mode)
{
   if (!disk)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Create: disk is NULL!\n");
      return NULL;
   }

   if (!path) return NULL;

   FAT_Instance *inst = fat_inst(disk);
   if (!inst) return NULL;

   char metadataPath[MAX_PATH_SIZE];
   fat_normalize_absolute_path(path, metadataPath, sizeof(metadataPath));

   // Normalize leading slash
   if (path[0] == '/') path++;

   // Split path into parent + basename (allocate on heap to avoid stack blow)
   char *parentPath = kmalloc(MAX_PATH_SIZE);
   char *baseName = kmalloc(MAX_PATH_SIZE);
   if (!parentPath || !baseName)
   {
      if (parentPath) free(parentPath);
      if (baseName) free(baseName);
      return NULL;
   }

   const char *lastSlash = strrchr(path, '/');
   if (lastSlash)
   {
      size_t parentLen = lastSlash - path;
      if (parentLen >= MAX_PATH_SIZE) parentLen = MAX_PATH_SIZE - 1;
      memcpy(parentPath, path, parentLen);
      parentPath[parentLen] = '\0';
      strncpy(baseName, lastSlash + 1, MAX_PATH_SIZE - 1);
      baseName[MAX_PATH_SIZE - 1] = '\0';
   }
   else
   {
      parentPath[0] = '\0';
      strncpy(baseName, path, MAX_PATH_SIZE - 1);
      baseName[MAX_PATH_SIZE - 1] = '\0';
   }

   if (baseName[0] == '\0')
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Create: empty basename\n");
      free(parentPath);
      free(baseName);
      return NULL;
   }

   // Open parent directory
   FAT_File *parentFile = (parentPath[0] == '\0') ? &inst->RootDirectory.Public
                                                  : FAT_Open(disk, parentPath);
   if (!parentFile || !parentFile->IsDirectory)
   {
      free(parentPath);
      free(baseName);
      return NULL;
   }

   // Convert basename to FAT 8.3
   const char *name = baseName;
   char fatName[12];
   memset(fatName, ' ', sizeof(fatName));
   fatName[11] = '\0';

   const char *ext = strchr(name, '.');
   if (ext == NULL)
      ext = name + strlen(name); // Point to end of string if no extension

   int nameLen = (ext - name > 8) ? 8 : (ext - name);
   for (int i = 0; i < nameLen && name[i] && name[i] != '.'; i++)
      fatName[i] = toupper(name[i]);

   if (ext != name + strlen(name) && *ext == '.')
   {
      for (int i = 0; i < 3 && ext[i + 1]; i++)
         fatName[i + 8] = toupper(ext[i + 1]);
   }

   // Check if file already exists in parent
   FAT_DirectoryEntry existingEntry;

   if (FAT_FindFile(disk, parentFile, baseName, &existingEntry))
   {
      free(parentPath);
      free(baseName);
      return NULL;
   }

   // Find first free cluster for the file
   uint32_t firstFreeCluster = 0;
   uint32_t maxClusters = (inst->TotalSectors - inst->DataSectionLba) /
                          inst->BS.BootSector.SectorsPerCluster;

   // Search all clusters (was previously limited to 1000 and could miss free
   // space on larger images)
   uint32_t maxSearchClusters = maxClusters;

   for (uint32_t testCluster = 2; testCluster < maxSearchClusters;
        testCluster++)
   {
      uint32_t nextClusterVal = FAT_NextCluster(inst, disk, testCluster);
      if (nextClusterVal == 0)
      {
         firstFreeCluster = testCluster;
         break;
      }
   }

   if (firstFreeCluster == 0)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Create: no free clusters available\n");
      free(parentPath);
      free(baseName);
      return NULL;
   }

   // Mark cluster as end-of-chain in all FATs
   uint32_t eofVal = (inst->FatType == 12)   ? 0x0FFF
                     : (inst->FatType == 16) ? 0xFFFF
                                             : 0x0FFFFFFF;
   if (!FAT_WriteFatEntry(inst, disk, firstFreeCluster, eofVal))
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Create: FAT write error\n");
      free(parentPath);
      free(baseName);
      return NULL;
   }

   // Create directory entry
   FAT_DirectoryEntry newEntry;
   memcpy(newEntry.Name, fatName, 11);
   newEntry.Attributes = 0x20; // Archive attribute
   newEntry._Reserved = 0;
   newEntry.CreatedTimeTenths = 0;
   newEntry.CreatedTime = 0;
   newEntry.CreatedDate = 0;
   newEntry.AccessedDate = 0;
   newEntry.FirstClusterHigh = (firstFreeCluster >> 16) & 0xFFFF;
   newEntry.ModifiedTime = 0;
   newEntry.ModifiedDate = 0;
   newEntry.FirstClusterLow = firstFreeCluster & 0xFFFF;
   newEntry.Size = 0; // Start with empty file

   // Find empty slot in parent directory
   FAT_Seek(disk, parentFile, 0);

   FAT_DirectoryEntry dirEntry;
   uint32_t entryPos = 0;
   int entryCount = 0;
   // For FAT32 DirEntryCount is 0; allow scanning until EOF with safety cap.
   int maxEntries = (inst->BS.BootSector.DirEntryCount > 0)
                        ? inst->BS.BootSector.DirEntryCount
                        : 65536;

   while (FAT_ReadEntry(disk, parentFile, &dirEntry) && entryCount < maxEntries)
   {
      entryCount++;
      entryPos = parentFile->Position - sizeof(FAT_DirectoryEntry);
      // Found empty slot (first byte is 0x00) or deleted entry (0xE5)
      if (dirEntry.Name[0] == 0x00 || (uint8_t)dirEntry.Name[0] == 0xE5)
      {
         // Go back to this position
         FAT_Seek(disk, parentFile, entryPos);

         // Write the new entry
         if (!FAT_WriteEntry(disk, parentFile, &newEntry))
         {
            logfmt(LOG_ERROR,
                   "[FAT] FAT_Create: failed to write directory entry\n");
            free(parentPath);
            free(baseName);
            return NULL;
         }

         // Open the file (with parent context)
         FAT_FileData *parentData =
             (parentFile->Handle == ROOT_DIRECTORY_HANDLE)
                 ? &inst->RootDirectory
                 : &inst->OpenedFiles[parentFile->Handle];
         FAT_File *file = FAT_OpenEntry(inst, disk, &newEntry, parentData);

         // Close parent directory if it's not the root
         if (parentFile->Handle != ROOT_DIRECTORY_HANDLE)
         {
            FAT_Close(parentFile);
         }

         if (file != NULL)
         {
            if (!fat_metadata_append_record(disk, metadataPath, mode, 0))
            {
               logfmt(LOG_WARNING,
                      "[FAT] FAT_Create: failed to append metadata for '%s'\n",
                      metadataPath);
            }
         }
         free(parentPath);
         free(baseName);
         return file;
      }
   }

   logfmt(LOG_ERROR,
          "[FAT] FAT_Create: no space in root directory (checked %u entries)\n",
          entryCount);
   free(parentPath);
   free(baseName);
   return NULL;
}

bool FAT_Delete(Partition *disk, const char *name)
{
   if (!name) return false;

   char metadataPath[MAX_PATH_SIZE];
   fat_normalize_absolute_path(name, metadataPath, sizeof(metadataPath));

   FAT_Instance *inst = fat_inst(disk);
   if (!inst) return false;

   // Normalize path and split into parent + basename
   if (name[0] == '/') name++;

   char *parentPath = kmalloc(MAX_PATH_SIZE);
   char *baseName = kmalloc(MAX_PATH_SIZE);
   if (!parentPath || !baseName)
   {
      if (parentPath) free(parentPath);
      if (baseName) free(baseName);
      return false;
   }

   const char *lastSlash = strrchr(name, '/');
   if (lastSlash)
   {
      size_t parentLen = lastSlash - name;
      if (parentLen >= MAX_PATH_SIZE) parentLen = MAX_PATH_SIZE - 1;
      memcpy(parentPath, name, parentLen);
      parentPath[parentLen] = '\0';
      strncpy(baseName, lastSlash + 1, MAX_PATH_SIZE - 1);
      baseName[MAX_PATH_SIZE - 1] = '\0';
   }
   else
   {
      parentPath[0] = '\0';
      strncpy(baseName, name, MAX_PATH_SIZE - 1);
      baseName[MAX_PATH_SIZE - 1] = '\0';
   }

   if (baseName[0] == '\0')
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Delete: empty basename in path\n");
      free(parentPath);
      free(baseName);
      return false;
   }

   FAT_File *parentDir = (parentPath[0] == '\0') ? &inst->RootDirectory.Public
                                                 : FAT_Open(disk, parentPath);
   if (!parentDir || !parentDir->IsDirectory)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Delete: parent directory '%s' not found\n",
             parentPath);
      free(parentPath);
      free(baseName);
      return false;
   }

   FAT_DirectoryEntry entry;
   if (!FAT_FindFile(disk, parentDir, baseName, &entry))
   {
      logfmt(LOG_WARNING, "[FAT] FAT_Delete: file '%s' not found in '%s'\n",
             baseName, parentPath[0] ? parentPath : "/");
      free(parentPath);
      free(baseName);
      return false;
   }

   uint32_t firstCluster =
       entry.FirstClusterLow + ((uint32_t)entry.FirstClusterHigh << 16);

   // If it's a directory, delete its contents best-effort
   if (entry.Attributes & FAT_ATTRIBUTE_DIRECTORY)
   {
      FAT_FileData *parentData = (parentDir->Handle == ROOT_DIRECTORY_HANDLE)
                                     ? &inst->RootDirectory
                                     : &inst->OpenedFiles[parentDir->Handle];

      FAT_File *dir = FAT_OpenEntry(inst, disk, &entry, parentData);
      if (dir)
      {
         FAT_DirectoryEntry subEntry;
         while (FAT_ReadEntry(disk, dir, &subEntry))
         {
            if ((subEntry.Attributes & 0x0F) == 0x0F ||
                subEntry.Name[0] == 0x00 || (uint8_t)subEntry.Name[0] == 0xE5)
               continue;

            if ((subEntry.Name[0] == '.' && subEntry.Name[1] == ' ') ||
                (subEntry.Name[0] == '.' && subEntry.Name[1] == '.' &&
                 subEntry.Name[2] == ' '))
               continue;

            char tempName[12];
            memcpy(tempName, subEntry.Name, 11);
            tempName[11] = '\0';
            FAT_Delete(disk, tempName);
         }
         FAT_Close(dir);
      }
   }

   // Free all clusters in the chain
   uint32_t currentCluster = firstCluster;
   uint32_t eofMarker = (inst->FatType == 12)   ? 0xFF8
                        : (inst->FatType == 16) ? 0xFFF8
                                                : 0x0FFFFFF8;
   const uint32_t largeClusterThreshold = 0x0FFFFF00;

   if (inst->BS.BootSector.SectorsPerCluster == 0 ||
       inst->BS.BootSector.BytesPerSector == 0)
   {
      logfmt(
          LOG_ERROR,
          "[FAT] FAT_Delete: invalid FAT parameters, skipping cluster free\n");
      currentCluster = 0;
   }

   int clusterCount = 0;
   if (currentCluster >= 2 && currentCluster < eofMarker &&
       currentCluster < largeClusterThreshold)
   {
      while (currentCluster < eofMarker &&
             currentCluster < largeClusterThreshold && clusterCount < 10000)
      {
         clusterCount++;

         // Zero out the cluster data
         uint32_t sectorsPerCluster = inst->BS.BootSector.SectorsPerCluster;
         uint32_t clusterLba = FAT_ClusterToLba(inst, currentCluster);
         uint8_t zeroBuffer[SECTOR_SIZE];
         memset(zeroBuffer, 0, SECTOR_SIZE);

         for (uint32_t s = 0; s < sectorsPerCluster; s++)
         {
            Partition_WriteSectors(disk, clusterLba + s, 1, zeroBuffer);
         }

         uint32_t nextCluster = FAT_NextCluster(inst, disk, currentCluster);
         if (!FAT_WriteFatEntry(inst, disk, currentCluster, 0))
         {
            logfmt(LOG_ERROR,
                   "[FAT] FAT_Delete: FAT write error freeing cluster %u\n",
                   currentCluster);
            break;
         }

         currentCluster = nextCluster;
      }
   }

   // Mark directory entry as deleted within the parent directory
   FAT_FileData *parentData = (parentDir->Handle == ROOT_DIRECTORY_HANDLE)
                                  ? &inst->RootDirectory
                                  : &inst->OpenedFiles[parentDir->Handle];

   uint32_t sectorsPerCluster = inst->BS.BootSector.SectorsPerCluster;
   if (parentData == &inst->RootDirectory && inst->FatType != 32)
   {
      for (uint32_t s = 0; s < inst->RootDirSectors; s++)
      {
         uint8_t *sectorBuffer = kmalloc(SECTOR_SIZE);
         if (!sectorBuffer) continue;
         uint32_t lba = inst->RootDirLba + s;
         if (!Partition_ReadSectors(disk, lba, 1, sectorBuffer))
         {
            free(sectorBuffer);
            continue;
         }
         for (uint32_t off = 0; off < SECTOR_SIZE;
              off += sizeof(FAT_DirectoryEntry))
         {
            FAT_DirectoryEntry *e = (FAT_DirectoryEntry *)(sectorBuffer + off);
            if ((e->Attributes & 0x0F) == 0x0F) continue;
            if (e->Name[0] == 0x00)
            {
               free(sectorBuffer);
               break;
            }
            if (memcmp(e->Name, entry.Name, 11) == 0)
            {
               sectorBuffer[off] = 0xE5;
               Partition_WriteSectors(disk, lba, 1, sectorBuffer);
               free(sectorBuffer);
               if (!fat_metadata_append_record(disk, metadataPath, 0,
                                               FAT_METADATA_FLAG_DELETED))
               {
                  logfmt(
                      LOG_WARNING,
                      "[FAT] FAT_Delete: metadata tombstone failed for '%s'\n",
                      metadataPath);
               }
               logfmt(LOG_INFO, "[FAT] FAT_Delete: deleted '%s'\n", name);
               free(parentPath);
               free(baseName);
               return true;
            }
         }
         free(sectorBuffer);
      }
   }
   else
   {
      uint32_t cluster = parentData->FirstCluster;
      uint32_t eofMarkerDel = (inst->FatType == 12)   ? 0xFF8
                              : (inst->FatType == 16) ? 0xFFF8
                                                      : 0x0FFFFFF8;
      uint32_t scanned = 0;
      while (cluster < eofMarkerDel && scanned < 10000)
      {
         for (uint32_t sec = 0; sec < sectorsPerCluster; sec++)
         {
            uint8_t *sectorBuffer = kmalloc(SECTOR_SIZE);
            if (!sectorBuffer) continue;
            uint32_t lba = FAT_ClusterToLba(inst, cluster) + sec;
            if (!Partition_ReadSectors(disk, lba, 1, sectorBuffer))
            {
               free(sectorBuffer);
               continue;
            }
            for (uint32_t off = 0; off < SECTOR_SIZE;
                 off += sizeof(FAT_DirectoryEntry))
            {
               FAT_DirectoryEntry *e =
                   (FAT_DirectoryEntry *)(sectorBuffer + off);
               if ((e->Attributes & 0x0F) == 0x0F) continue;
               if (e->Name[0] == 0x00)
               {
                  free(sectorBuffer);
                  break;
               }
               if (memcmp(e->Name, entry.Name, 11) == 0)
               {
                  sectorBuffer[off] = 0xE5;
                  Partition_WriteSectors(disk, lba, 1, sectorBuffer);
                  free(sectorBuffer);
                  if (!fat_metadata_append_record(disk, metadataPath, 0,
                                                  FAT_METADATA_FLAG_DELETED))
                  {
                     logfmt(LOG_WARNING,
                            "[FAT] FAT_Delete: metadata tombstone failed for "
                            "'%s'\n",
                            metadataPath);
                  }
                  logfmt(LOG_INFO, "[FAT] FAT_Delete: deleted '%s'\n", name);
                  free(parentPath);
                  free(baseName);
                  return true;
               }
            }
            free(sectorBuffer);
         }
         cluster = FAT_NextCluster(inst, disk, cluster);
         scanned++;
      }
   }

   logfmt(LOG_WARNING,
          "[FAT] FAT_Delete: entry not found during mark phase for '%s'\n",
          name);
   free(parentPath);
   free(baseName);
   return false;
}

bool FAT_Truncate(Partition *disk, FAT_File *file)
{
   if (!file)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Truncate: file is NULL\n");
      return false;
   }

   if (file->Handle == ROOT_DIRECTORY_HANDLE)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Truncate: cannot truncate root directory\n");
      return false;
   }

   if (file->Handle < 0 || file->Handle >= MAX_FILE_HANDLES)
   {
      logfmt(LOG_ERROR, "[FAT] FAT_Truncate: invalid file handle %d\n",
             file->Handle);
      return false;
   }

   logfmt(LOG_INFO, "[FAT] FAT_Truncate: called, file=%p, Handle=%d\n", file,
          file->Handle);

   FAT_Instance *inst = fat_inst(disk);
   if (!inst) return false;

   FAT_FileData *fd = &inst->OpenedFiles[file->Handle];
   logfmt(LOG_INFO, "[FAT] FAT_Truncate: fd=%p, Opened=%d\n", fd, fd->Opened);
   if (!fd->Opened) return false;

   // Validate FAT parameters to avoid divide-by-zero
   if (inst->BS.BootSector.SectorsPerCluster == 0 ||
       inst->BS.BootSector.BytesPerSector == 0)
   {
      logfmt(LOG_ERROR,
             "[FAT] FAT_Truncate: invalid FAT parameters "
             "(SectorsPerCluster=%u, BytesPerSector=%u)\n",
             inst->BS.BootSector.SectorsPerCluster,
             inst->BS.BootSector.BytesPerSector);
      fd->FirstCluster = 0;
      fd->CurrentCluster = 0;
      fd->CurrentSectorInCluster = 0;
      fd->Public.Position = 0;
      fd->Public.Size = 0;
      return false;
   }

   uint32_t currentCluster = fd->FirstCluster;
   uint32_t eofMarker = (inst->FatType == 12)   ? 0xFF8
                        : (inst->FatType == 16) ? 0xFFF8
                                                : 0x0FFFFFF8;

   if (currentCluster < 2 || currentCluster >= eofMarker)
   {
      fd->FirstCluster = 0;
      fd->CurrentCluster = 0;
      fd->CurrentSectorInCluster = 0;
      fd->Public.Position = 0;
      fd->Public.Size = 0;
      return true;
   }

   int clusterCount = 0;

   logfmt(LOG_INFO,
          "[FAT] FAT_Truncate: starting cluster chain cleanup, "
          "FirstCluster=%u, FatType=%u\n",
          fd->FirstCluster, inst->FatType);
   logfmt(LOG_INFO, "[FAT] FAT_Truncate: eofMarker=%u (0x%x)\n", eofMarker,
          eofMarker);

   // Get the next cluster BEFORE freeing anything
   uint32_t nextCluster = FAT_NextCluster(inst, disk, currentCluster);
   logfmt(LOG_INFO,
          "[FAT] FAT_Truncate: FirstCluster nextCluster=%u, eofMarker=%u\n",
          nextCluster, eofMarker);

   // Free all clusters EXCEPT the first one (we want to keep that for potential
   // writes)
   if (nextCluster < eofMarker)
   {
      currentCluster = nextCluster;
      while (currentCluster >= 2 && currentCluster < eofMarker &&
             clusterCount < 5000)
      {
         logfmt(LOG_INFO, "[FAT] FAT_Truncate: freeing cluster %u\n",
                currentCluster);
         clusterCount++;

         uint32_t tempNextCluster = FAT_NextCluster(inst, disk, currentCluster);
         if (!FAT_WriteFatEntry(inst, disk, currentCluster, 0))
         {
            logfmt(LOG_ERROR,
                   "[FAT] FAT_Truncate: FAT write error freeing cluster %u\n",
                   currentCluster);
            return false;
         }

         currentCluster = tempNextCluster;
      }
   }

   // Now mark the first cluster as EOF (end of chain)
   logfmt(LOG_INFO, "[FAT] FAT_Truncate: marking first cluster %u as EOF\n",
          fd->FirstCluster);
   uint32_t eofVal = (inst->FatType == 12)   ? 0x0FFF
                     : (inst->FatType == 16) ? 0xFFFF
                                             : 0x0FFFFFFF;
   if (!FAT_WriteFatEntry(inst, disk, fd->FirstCluster, eofVal))
   {
      logfmt(
          LOG_ERROR,
          "[FAT] FAT_Truncate: FAT write error marking first cluster as EOF\n");
      return false;
   }

   // Reset file position and size, but keep FirstCluster and CurrentCluster
   // intact
   fd->Public.Position = 0;
   fd->Public.Size = 0;
   fd->Truncated = true; // Mark as truncated
   fd->CurrentSectorInCluster = 0;
   fd->CurrentCluster = fd->FirstCluster;
   memset(fd->Buffer, 0, SECTOR_SIZE);

   // Read the first cluster into buffer for potential writes
   if (!Partition_ReadSectors(disk, FAT_ClusterToLba(inst, fd->FirstCluster), 1,
                              fd->Buffer))
   {
      logfmt(LOG_ERROR,
             "[FAT] FAT_Truncate: failed to read first cluster into buffer\n");
      return false;
   }

   inst->FatCachePos = 0xFFFFFFFF;
   logfmt(LOG_INFO,
          "[FAT] FAT_Truncate: truncate complete, file ready for writes\n");
   return true;
}

/* Invalidate the FAT cache - call after operations that may leave cache
 * inconsistent */
void FAT_InvalidateCache(FAT_Instance *inst)
{
   if (!inst) return;

   /* Invalidate FAT cache to force fresh reads */
   inst->FatCachePos = 0xFFFFFFFF;

   /* Close opened file handles (except root directory which is always open) */
   for (int i = 0; i < MAX_FILE_HANDLES; i++)
   {
      if (inst->OpenedFiles[i].Opened)
      {
         inst->OpenedFiles[i].Opened = false;
      }
   }
}
/* ============================================================================
 * VFS Integration - FAT operations for Linux-style VFS
 * ============================================================================
 */

/* FAT-specific VFS_Open wrapper that creates a VFS_File from FAT_File */
static VFS_File *fat_vfs_open(Partition *partition, const char *path)
{
   if (!partition || !partition->fs || !path) return NULL;

   FAT_File *fat_file = FAT_Open(partition, path);
   if (!fat_file) return NULL;

   VFS_File *vf = (VFS_File *)kmalloc(sizeof(VFS_File));
   if (!vf) return NULL;

   vf->partition = partition;
   vf->type = partition->fs->type;
   vf->fs_file = fat_file;
   vf->is_directory = fat_file->IsDirectory;
   vf->size = fat_file->Size;
   return vf;
}

/* VFS create wrapper: opens a new (non-existing) file via FAT_Create */
static VFS_File *fat_vfs_create(Partition *partition, const char *path,
                                uint16_t mode)
{
   if (!partition || !partition->fs || !path) return NULL;

   FAT_File *fat_file = FAT_Create(partition, path, mode);
   if (!fat_file) return NULL;

   VFS_File *vf = (VFS_File *)kmalloc(sizeof(VFS_File));
   if (!vf) return NULL;

   vf->partition = partition;
   vf->type = partition->fs->type;
   vf->fs_file = fat_file;
   vf->is_directory = fat_file->IsDirectory;
   vf->size = fat_file->Size;
   return vf;
}

/* Small wrapper to extract size from FAT_File */
static uint32_t fat_vfs_get_size(void *fs_file)
{
   if (!fs_file) return 0;
   return ((FAT_File *)fs_file)->Size;
}

static bool fat_vfs_access(Partition *partition, const char *path, uint32_t uid,
                           uint32_t gid, uint8_t accessMask)
{
   return fat_check_access_path(partition, path, uid, gid, accessMask);
}

static void fat_short_name_to_cstr(const uint8_t fatName[11], char *out,
                                   size_t outSize)
{
   if (!out || outSize == 0) return;

   size_t pos = 0;

   for (int i = 0; i < 8 && pos + 1 < outSize; i++)
   {
      if (fatName[i] == ' ') break;
      out[pos++] = (char)fatName[i];
   }

   bool hasExt = false;
   for (int i = 8; i < 11; i++)
   {
      if (fatName[i] != ' ')
      {
         hasExt = true;
         break;
      }
   }

   if (hasExt && pos + 2 < outSize)
   {
      out[pos++] = '.';
      for (int i = 8; i < 11 && pos + 1 < outSize; i++)
      {
         if (fatName[i] == ' ') break;
         out[pos++] = (char)fatName[i];
      }
   }

   out[pos] = '\0';
}

static bool fat_vfs_readdir(Partition *partition, void *fs_file,
                            VFS_DirEntry *entryOut)
{
   if (!partition || !fs_file || !entryOut) return false;

   FAT_File *dir = (FAT_File *)fs_file;
   if (!dir->IsDirectory) return false;

   FAT_DirectoryEntry entry;
   while (FAT_ReadEntry(partition, dir, &entry))
   {
      if (entry.Name[0] == 0x00) return false;
      if ((uint8_t)entry.Name[0] == 0xE5) continue;
      if ((entry.Attributes & FAT_ATTRIBUTE_LFN) == FAT_ATTRIBUTE_LFN) continue;
      if (entry.Attributes & FAT_ATTRIBUTE_VOLUME_ID) continue;

      fat_short_name_to_cstr(entry.Name, entryOut->name,
                             sizeof(entryOut->name));
      entryOut->is_directory =
          (entry.Attributes & FAT_ATTRIBUTE_DIRECTORY) != 0;
      entryOut->size = entry.Size;
      return true;
   }

   return false;
}

static bool fat_vfs_chmod(Partition *partition, const char *path, uint16_t mode)
{
   return fat_chmod_path(partition, path, mode);
}

static bool fat_vfs_chown(Partition *partition, const char *path, uint32_t uid,
                          uint32_t gid)
{
   return fat_chown_path(partition, path, uid, gid);
}

/* FAT operations structure - directly points to FAT functions */
static const VFS_Operations fat_vfs_ops = {
    .open =
        fat_vfs_open, /* Open an existing file (returns NULL if not found) */
    .create = fat_vfs_create, /* Create a new file */
    .readdir = fat_vfs_readdir,
    .read = (uint32_t (*)(Partition *, void *, uint32_t, void *))FAT_Read,
    .write =
        (uint32_t (*)(Partition *, void *, uint32_t, const void *))FAT_Write,
    .seek = (bool (*)(Partition *, void *, uint32_t))FAT_Seek,
    .close = (void (*)(void *))FAT_Close,
    .get_size = fat_vfs_get_size, /* Simple wrapper for size extraction */
    .delete = (bool (*)(Partition *, const char *))FAT_Delete,
    .access = fat_vfs_access,
    .chmod = fat_vfs_chmod,
    .chown = fat_vfs_chown};

/* Public function to get FAT VFS operations */
const VFS_Operations *FAT_GetVFSOperations(void) { return &fat_vfs_ops; }