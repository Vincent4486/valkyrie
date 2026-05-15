# SPDX-License-Identifier: BSD-3-Clause

import copy
import os
import struct

from SCons.Environment import Environment

AllowedBootSetups = (
    ("aarch64", "efi", "system"),
    ("aarch64", "efi", "grub"),
    ("x86_64", "bios", "system"),
    ("x86_64", "bios", "grub"),
    ("x86_64", "efi", "system"),
    ("x86_64", "efi", "grub"),
    ("i686", "bios", "system"),
    ("i686", "bios", "grub"),
)


BootloaderProfiles = {
    "bios": {
        "SupportedArchitectures": ["i686", "x86_64"],
        "CompilerFlags": ["-DBOOTLOADER_BIOS=1"],
        "AssemblerFlags": ["-DBOOTLOADER_BIOS=1"],
    },
    "efi": {
        "SupportedArchitectures": ["x86_64", "aarch64"],
        "CompilerFlags": ["-DBOOTLOADER_EFI=1"],
        "AssemblerFlags": ["-DBOOTLOADER_EFI=1"],
    },
}

CoreFsPatchSignature = b"VLSF"
CoreFsPatchOffset = 4
CoreFsPartitionLabelOffset = 8
ElToritoLoadAddress = 0x7C00
CoreFsLoadAddress = 0x57E00


def GetSupportedBootTypes() -> list:
    return list(BootloaderProfiles.keys())


def ValidateBootSetup(Architecture: str, BootType: str, Bootloader: str) -> None:
    Config = (Architecture.lower(), BootType.lower(), Bootloader.lower())
    if Config not in AllowedBootSetups:
        raise ValueError(
            f"Unsupported boot setup: "
            f"Architecture={Architecture}, "
            f"BootType={BootType}, "
            f"Bootloader={Bootloader}. "
        )


def GetBootloaderBuildConfig(BootType: str, Architecture: str) -> dict:
    if BootType not in BootloaderProfiles:
        raise ValueError(
            f"Unsupported boot type: {BootType}. Supported: {list(BootloaderProfiles.keys())}"
        )

    config = copy.deepcopy(BootloaderProfiles[BootType])
    supported_architectures = config.get("SupportedArchitectures", [])
    if supported_architectures and Architecture not in supported_architectures:
        raise ValueError(
            f"Unsupported architecture {Architecture} for boot type {BootType}. "
            f"Supported: {supported_architectures}"
        )

    config["BootType"] = BootType
    config["Architecture"] = Architecture
    config["OutputName"] = f"bootloader-{BootType}-{Architecture}"

    return config


def ConfigureBootloaderEnvironment(
    env: Environment,
    source_path: str,
    architecture_path: str,
    architecture_config: dict,
    bootloader_config: dict,
):
    env.Append(
        ASFLAGS=architecture_config.get("AssemblyFlags", []),
        CCFLAGS=architecture_config.get("CompilerFlags", []),
        LINKFLAGS=architecture_config.get("LinkerFlags", []),
    )

    env.Append(
        CCFLAGS=[
            "-ffreestanding",
            "-fno-stack-protector",
            "-fno-builtin",
            "-Wall",
            "-Wextra",
        ],
        CPATH=[source_path, os.path.join(source_path, "common"), "#include"],
        CPPPATH=[source_path, os.path.join(source_path, "common"), "#include"],
        ASFLAGS=[
            "-I",
            source_path,
            "-I",
            os.path.join(source_path, "common"),
            "-I",
            architecture_path,
            "-g",
            "-Wa,--noexecstack",
        ],
    )

    env.Append(CCFLAGS=bootloader_config.get("CompilerFlags", []))
    env.Append(ASFLAGS=bootloader_config.get("AssemblerFlags", []))


def PatchBinaryValue(
    BinaryPath: str,
    Signature: bytes,
    Value: int,
    ValueFormat: str,
    ValueOffset: int = 4,
) -> None:
    if not isinstance(Signature, (bytes, bytearray)):
        raise TypeError("Signature must be bytes")

    with open(BinaryPath, "rb") as FileHandle:
        Data = bytearray(FileHandle.read())

    SignatureOffset = Data.find(Signature)
    if SignatureOffset == -1:
        raise ValueError(f"Signature {Signature!r} not found in {BinaryPath}")
    if Data.find(Signature, SignatureOffset + 1) != -1:
        raise ValueError(
            f"Signature {Signature!r} appears multiple times in {BinaryPath}"
        )

    ValueSize = struct.calcsize(ValueFormat)
    PatchOffset = SignatureOffset + ValueOffset
    if PatchOffset + ValueSize > len(Data):
        raise ValueError(f"Patch offset {PatchOffset} exceeds size of {BinaryPath}")

    struct.pack_into(ValueFormat, Data, PatchOffset, Value)

    with open(BinaryPath, "wb") as FileHandle:
        FileHandle.write(Data)


def PatchCoreFsStartAddress(
    CorePath: str,
    StartAddress: int,
) -> None:
    PatchBinaryValue(
        BinaryPath=CorePath,
        Signature=CoreFsPatchSignature,
        Value=StartAddress,
        ValueFormat="<I",
        ValueOffset=CoreFsPatchOffset,
    )


def PatchCoreFsPartitionLabel(
    CorePath: str,
    Label: bytes,
) -> None:
    if len(Label) != 32:
        raise ValueError(f"Partition label must be exactly 32 bytes, got {len(Label)}")
    PatchBinaryValue(
        BinaryPath=CorePath,
        Signature=CoreFsPatchSignature,
        Value=Label,
        ValueFormat="32s",
        ValueOffset=CoreFsPartitionLabelOffset,
    )


def ResolveCoreFsBinaryPath(
    FileSystemType: str,
    CoreFsBinaries: list,
    Stage2Path: str,
) -> str:
    TargetName = f"corefs_{FileSystemType}.bin"

    for Node in CoreFsBinaries or []:
        NodePath = Node.get_abspath() if hasattr(Node, "get_abspath") else str(Node)
        if os.path.basename(NodePath) == TargetName:
            return NodePath

    CandidatePath = os.path.join(os.path.dirname(Stage2Path), TargetName)
    if os.path.exists(CandidatePath):
        return CandidatePath

    raise FileNotFoundError(
        f"Filesystem module not found: {TargetName} (stage2 dir: {os.path.dirname(Stage2Path)})"
    )


def CreateElTorito(
    Stage1Path: str,
    Stage2Path: str,
    FileSystemType: str,
    CoreFsBinaries: list = None,
    PartitionLabel: bytes = b"\x00" * 32,
) -> str:
    if not os.path.exists(Stage1Path):
        raise FileNotFoundError(f"Stage1 bootloader file does not exist: {Stage1Path}")
    if not os.path.exists(Stage2Path):
        raise FileNotFoundError(f"Stage2 bootloader file does not exist: {Stage2Path}")

    with open(Stage1Path, "rb") as Stage1File:
        Stage1Data = Stage1File.read()

    PatchCoreFsStartAddress(Stage2Path, CoreFsLoadAddress)
    PatchCoreFsPartitionLabel(Stage2Path, PartitionLabel)

    with open(Stage2Path, "rb") as Stage2File:
        Stage2Data = Stage2File.read()

    CoreFsPath = ResolveCoreFsBinaryPath(
        FileSystemType=FileSystemType,
        CoreFsBinaries=CoreFsBinaries,
        Stage2Path=Stage2Path,
    )
    with open(CoreFsPath, "rb") as CoreFsFile:
        CoreFsData = CoreFsFile.read()

    # Pad stage1 to a full 512-byte sector so the Boot Info Table (bytes 8-31)
    # has a well-defined home and stage2 begins on a sector boundary.
    if len(Stage1Data) < 512:
        Stage1Data = Stage1Data + b"\x00" * (512 - len(Stage1Data))

    if CoreFsLoadAddress <= ElToritoLoadAddress:
        raise ValueError(
            f"Invalid corefs load address: {CoreFsLoadAddress:#x} <= {ElToritoLoadAddress:#x}"
        )

    corefs_offset = CoreFsLoadAddress - ElToritoLoadAddress
    corefs_start = len(Stage1Data) + len(Stage2Data)
    if corefs_start > corefs_offset:
        raise ValueError(
            f"core.bin exceeds corefs load boundary: {corefs_start:#x} > {corefs_offset:#x}"
        )

    if corefs_start < corefs_offset:
        Stage2Data = Stage2Data + b"\x00" * (corefs_offset - corefs_start)

    Combined = Stage1Data + Stage2Data + CoreFsData

    OutputPath = os.path.join(
        os.path.dirname(Stage1Path),
        os.path.basename(Stage1Path).replace(".bin", "-eltorito.bin"),
    )
    with open(OutputPath, "wb") as OutputFile:
        OutputFile.write(Combined)

    return os.path.abspath(OutputPath)
