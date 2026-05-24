# SPDX-License-Identifier: BSD-3-Clause

import os
import shutil
import struct
import subprocess
import textwrap

from scripts.scons.bootloader import (
    CreateElTorito,
    ValidateBootSetup,
)

VolumeLabel = "VALECIUM"
IsoSectorSize = 2048
IsoBootRecordLba = 17
IsoPvdLba = 16
IsoPvdLabelOffset = 0x28
IsoPvdUuidOffset = 0x32D
IsoBootCatalogLbaOffset = 0x47
IsoBootCatalogInitialEntryOffset = 0x20
IsoBootImageSectorCountOffset = 0x06
IsoBootImageLbaOffset = 0x08
CoreFsPatchSignature = b"VLSF"


def ReadIsoPvdFields(IsoPath: str) -> tuple[bytes, bytes]:
    pvd_offset = IsoPvdLba * IsoSectorSize
    with open(IsoPath, "rb") as FileHandle:
        FileHandle.seek(pvd_offset + IsoPvdLabelOffset)
        label = FileHandle.read(32)
        FileHandle.seek(pvd_offset + IsoPvdUuidOffset)
        uuid = FileHandle.read(16)
    if len(label) != 32:
        raise ValueError("Partition label read failed")
    if len(uuid) != 16:
        raise ValueError("Partition UUID read failed")
    return label, uuid


def PatchIsoBootImageCoreFs(IsoPath: str, Label: bytes, Uuid: bytes) -> None:
    if len(Label) != 32:
        raise ValueError(f"Partition label must be 32 bytes, got {len(Label)}")
    if len(Uuid) != 16:
        raise ValueError(f"Partition UUID must be 16 bytes, got {len(Uuid)}")

    with open(IsoPath, "rb") as FileHandle:
        data = bytearray(FileHandle.read())

    boot_record_off = IsoBootRecordLba * IsoSectorSize
    boot_catalog_lba = struct.unpack_from(
        "<I", data, boot_record_off + IsoBootCatalogLbaOffset
    )[0]
    boot_catalog_off = boot_catalog_lba * IsoSectorSize
    initial_entry_off = boot_catalog_off + IsoBootCatalogInitialEntryOffset
    boot_image_sectors = struct.unpack_from(
        "<H", data, initial_entry_off + IsoBootImageSectorCountOffset
    )[0]
    boot_image_lba = struct.unpack_from(
        "<I", data, initial_entry_off + IsoBootImageLbaOffset
    )[0]

    boot_image_off = boot_image_lba * IsoSectorSize
    boot_image_size = boot_image_sectors * 512
    search_end = boot_image_off + boot_image_size if boot_image_size else len(data)

    sig_off = data.find(CoreFsPatchSignature, boot_image_off, search_end)
    if sig_off == -1:
        raise ValueError("CoreFS patch signature not found")
    if data.find(CoreFsPatchSignature, sig_off + 1, search_end) != -1:
        raise ValueError("CoreFS patch signature appears multiple times")

    label_off = sig_off + 8
    uuid_off = label_off + 32
    data[label_off : label_off + 32] = Label
    data[uuid_off : uuid_off + 16] = Uuid

    with open(IsoPath, "wb") as FileHandle:
        FileHandle.write(data)


def RunCommand(Arguments: list, InputText: str = None, **kwargs):
    subprocess.run(
        Arguments,
        check=True,
        input=InputText,
        text=(InputText is not None),
        **kwargs,
    )


def CreateBootableIso(
    StagingDirectory: str,
    OutputIso: str,
    VolumeLabelName: str = VolumeLabel,
    Architecture: str = "i686",
    BootType: str = "bios",
    BootSystem: str = "grub",
    BootloaderComponents: dict = None,
):
    """Create a bootable ISO 9660 image.

    When *BootSystem* is ``'system'`` and *BootloaderComponents* provides Stage1
    and Stage2, the system bootloader is embedded via El Torito "no emulation"
    boot using ``xorriso`` directly.  Otherwise ``grub-mkrescue`` is used.
    """

    ValidateBootSetup(
        Architecture=Architecture,
        BootType=BootType,
        Bootloader=BootSystem,
    )

    UseSystemBootloader = (
        BootSystem == "system"
        and BootloaderComponents
        and BootloaderComponents.get("Stage1")
        and BootloaderComponents.get("Stage2")
    )

    if not UseSystemBootloader:
        print("   GRUB-MKRESCUE")
        RunCommand(
            [
                "grub-mkrescue",
                "-o",
                OutputIso,
                StagingDirectory,
                "--",
                "-volid",
                VolumeLabelName,
            ]
        )
        return

    Stage1Path = str(BootloaderComponents["Stage1"])
    Stage2Path = str(BootloaderComponents["Stage2"])

    PartitionLabelBytes = VolumeLabelName.encode("ascii", errors="replace").ljust(
        32, b" "
    )[:32]
    ElToritoPath = CreateElTorito(
        Stage1Path,
        Stage2Path,
        FileSystemType="iso9660",
        CoreFsBinaries=BootloaderComponents.get("CoreFsBinaries")
        if BootloaderComponents
        else None,
        PartitionLabel=PartitionLabelBytes,
    )
    LoadSectors = (os.path.getsize(ElToritoPath) + 511) // 512

    print(
        f"   XORRISO (El Torito: {os.path.basename(ElToritoPath)}, {LoadSectors} sectors)"
    )

    BootImageInStage = os.path.join(
        StagingDirectory, "boot", os.path.basename(ElToritoPath)
    )
    os.makedirs(os.path.dirname(BootImageInStage), exist_ok=True)
    shutil.copy2(ElToritoPath, BootImageInStage)

    RunCommand(
        [
            "xorriso",
            "-as",
            "mkisofs",
            "-o",
            OutputIso,
            "-b",
            os.path.relpath(BootImageInStage, StagingDirectory),
            "-no-emul-boot",
            "-boot-load-size",
            str(LoadSectors),
            "-boot-info-table",
            "-volid",
            VolumeLabelName,
            StagingDirectory,
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    PartitionLabel, PartitionUuid = ReadIsoPvdFields(OutputIso)
    PatchIsoBootImageCoreFs(OutputIso, PartitionLabel, PartitionUuid)


def BuildGrubConfigContent(
    Config: str = "release",
    KernelName: str = "valeciumx",
    VolumeLabelName: str = VolumeLabel,
) -> str:
    Timeout = "0" if Config == "debug" else "10"

    return textwrap.dedent(f"""\
# Set a variable to prevent recursion loops
if [ -z "$configLoaded" ]; then
    set configLoaded=1

    # Force standard PC keyboard and console output
    terminal_input console
    terminal_output console

    set timeout_style=menu
    set timeout={Timeout}
    set default=0

    menuentry "Valecium OS" {{
        search --no-floppy --label {VolumeLabelName} --set=root
        multiboot /boot/{KernelName} root=LABEL={VolumeLabelName}
        boot
    }}

    menuentry "Reboot" {{
        reboot
    }}
fi
""")


def GenerateGrubConfig(
    GrubDirectory: str,
    Config: str = "release",
    KernelName: str = "valeciumx",
    VolumeLabelName: str = VolumeLabel,
) -> str:
    os.makedirs(GrubDirectory, exist_ok=True)
    ConfigPath = os.path.join(GrubDirectory, "grub.cfg")
    Content = BuildGrubConfigContent(Config, KernelName, VolumeLabelName)
    with open(ConfigPath, "w", encoding="utf-8") as FileHandle:
        FileHandle.write(Content)
    return ConfigPath
