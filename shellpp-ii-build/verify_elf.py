#!/usr/bin/env python3
"""Verify the conservative ELF contract for Shell++ II's first loader test."""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
SECTION_HEADER = struct.Struct("<IIIIIIIIII")
SYMBOL = struct.Struct("<IIIBBH")
RELOCATION = struct.Struct("<II")

ET_REL = 1
EM_ARM = 40
EV_CURRENT = 1
ELFCLASS32 = 1
ELFDATA2LSB = 1
EV_SYSV = 0
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_NOBITS = 8
SHT_REL = 9
SHF_ALLOC = 0x2
SHF_WRITE = 0x1
SHF_EXECINSTR = 0x4
SHN_UNDEF = 0
STT_FUNC = 2
R_ARM_ABS32 = 2
R_ARM_TARGET1 = 38

# This project targets only s441 3.100.028.  The former Xiaomi Band 10 Pro
# constants are deliberately not accepted: no s441 module-runtime address may
# be emitted until it has a matching s441 call-site and ABI validation record.
# register_driver: file+0x53e05c, firmware runtime 0x2c6fe05c.  The module
# mapping aliases this flash entry by -0x20000000 and must call it in Thumb
# state.  Confirmed from the O63 3.100.028 /dev/tun and /dev/usensor callers.
FIRMWARE_FUNCTIONS: set[int] = {
    0x0C6FE05D,  # register_driver
    0x0C31AFD5,  # uv_async_queue_init
    0x0C31B0CD,  # uv_async_queue_submit
    0x0C63B391,  # native_app_register_core
    0x0C639BA9,  # native App ID lookup used for post-registration proof
    0x0C412425,  # app_launcher_add
}
# Confirmed AP RAM globals/objects keep their runtime addresses when accessed
# by an ET_REL module. Only AP flash code uses the 0x0c... module alias.
FIRMWARE_DATA: set[int] = {0x3C2E94C0, 0x3C2040D0, 0x3C203FF4, 0x3C2FE2D0}


class VerificationError(Exception):
    pass


@dataclass(frozen=True)
class Section:
    name_offset: int
    type: int
    flags: int
    address: int
    offset: int
    size: int
    link: int
    info: int
    alignment: int
    entry_size: int
    name: str = ""


@dataclass(frozen=True)
class ElfSymbol:
    name: str
    value: int
    size: int
    info: int
    other: int
    section_index: int


def fail(message: str) -> None:
    raise VerificationError(message)


def require_range(data: bytes, offset: int, size: int, label: str) -> None:
    if offset < 0 or size < 0 or offset + size > len(data):
        fail(label + " is outside the ELF file")


def c_string(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        fail("string offset is outside its string table")
    end = data.find(b"\0", offset)
    if end < 0:
        fail("unterminated string in ELF string table")
    return data[offset:end].decode("ascii", "replace")


def has_flag(value: int, flag: int) -> bool:
    return (value & flag) == flag


def section_footprint(sections: list[Section]) -> int:
    footprint = 0
    for section in sections:
        if not has_flag(section.flags, SHF_ALLOC):
            continue
        alignment = max(section.alignment, 1)
        footprint = (footprint + alignment - 1) // alignment * alignment
        footprint += section.size
    return footprint


def verify_absolute_addresses(
    data: bytes,
    sections: list[Section],
    symbols: list[ElfSymbol],
) -> None:
    """Reject guessed firmware addresses embedded in allocated sections."""
    mapping_ranges: dict[int, list[tuple[int, int]]] = {}
    for section_index, section in enumerate(sections):
        if section.name != ".text":
            continue
        mappings = sorted(
            (symbol.value, symbol.name)
            for symbol in symbols
            if symbol.section_index == section_index
            and (symbol.name in ("$d", "$t")
                 or symbol.name.startswith("$d.")
                 or symbol.name.startswith("$t."))
        )
        ranges: list[tuple[int, int]] = []
        for index, (start, kind) in enumerate(mappings):
            end = mappings[index + 1][0] if index + 1 < len(mappings) else section.size
            if (kind == "$d" or kind.startswith("$d.")) and start < end:
                ranges.append((start, end))
        mapping_ranges[section_index] = ranges

    for section_index, section in enumerate(sections):
        if not has_flag(section.flags, SHF_ALLOC) or section.type == SHT_NOBITS:
            continue
        if section.name == ".text":
            ranges = mapping_ranges.get(section_index, [])
        elif section.name == ".data":
            ranges = [(0, section.size)]
        else:
            # Absolute firmware call/data constants emitted by this toolchain
            # live in Thumb literal pools or writable pointer tables.  Scanning
            # arbitrary .rodata words would reinterpret UTF-8/string bytes as
            # addresses (for example ASCII "l++ ").
            ranges = []
        for range_start, range_end in ranges:
            aligned_start = (range_start + 3) & ~3
            for relative in range(aligned_start, range_end - 3, 4):
                value = struct.unpack_from("<I", data, section.offset + relative)[0]
                location = section.name + "+0x" + format(relative, "x")
                if 0x2C000000 <= value < 0x2D000000:
                    fail("analysis-only firmware address at " + location
                         + ": 0x" + format(value, "08x"))
                if 0x0C000000 <= value < 0x0D000000:
                    if (value & 1) == 0:
                        fail("firmware function address is not Thumb at " + location
                             + ": 0x" + format(value, "08x"))
                    if value not in FIRMWARE_FUNCTIONS:
                        fail("unverified s441 module-runtime function at "
                             + location + ": 0x" + format(value, "08x"))
                if 0x1C000000 <= value < 0x1D000000:
                    fail("invalid s441 RAM alias at " + location
                         + ": 0x" + format(value, "08x"))
                if 0x3C000000 <= value < 0x3D000000 and value not in FIRMWARE_DATA:
                    fail("unverified s441 runtime data at " + location
                         + ": 0x" + format(value, "08x"))
                if 0x20000000 <= value < 0x21000000 and value not in FIRMWARE_DATA:
                    fail("unverified s441 module-runtime data at "
                         + location + ": 0x" + format(value, "08x"))


def parse_elf(path: Path) -> tuple[bytes, dict[str, int], list[Section], list[ElfSymbol], set[int]]:
    data = path.read_bytes()
    if len(data) < ELF_HEADER.size:
        fail("file is smaller than an ELF32 header")

    header = ELF_HEADER.unpack_from(data)
    ident = header[0]
    e_type, e_machine, e_version, e_entry = header[1:5]
    e_phoff, e_shoff, e_flags = header[5:8]
    e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = header[8:]

    if ident[:4] != b"\x7fELF":
        fail("missing ELF magic")
    if ident[4] != ELFCLASS32 or ident[5] != ELFDATA2LSB or ident[6] != EV_CURRENT:
        fail("expected ELF32 little-endian version 1")
    if ident[7] != EV_SYSV:
        fail("expected System V ELF ABI")
    if e_type != ET_REL or e_machine != EM_ARM or e_version != EV_CURRENT:
        fail("expected ARM ELF32 ET_REL")
    if ((e_flags >> 24) & 0xFF) != 5:
        fail("expected ARM EABI5 e_flags")
    if (e_flags & 0x00FFFFFF) not in (0, 0x200):
        fail("unsupported ARM float ABI flags")
    if e_ehsize != ELF_HEADER.size or e_phnum != 0 or e_phoff != 0:
        fail("ET_REL must not contain program headers")
    if e_shentsize != SECTION_HEADER.size or e_shnum == 0:
        fail("invalid section-header table")
    if e_shstrndx >= e_shnum:
        fail("section-name table index is invalid")
    require_range(data, e_shoff, e_shnum * e_shentsize, "section-header table")

    sections: list[Section] = []
    for index in range(e_shnum):
        values = SECTION_HEADER.unpack_from(data, e_shoff + index * e_shentsize)
        section = Section(*values)
        if section.type != SHT_NOBITS:
            require_range(data, section.offset, section.size, "section " + str(index))
        sections.append(section)

    names_section = sections[e_shstrndx]
    if names_section.type != SHT_STRTAB:
        fail("section-name table is not a string table")
    names = data[names_section.offset:names_section.offset + names_section.size]
    named_sections = []
    for section in sections:
        named_sections.append(Section(
            section.name_offset,
            section.type,
            section.flags,
            section.address,
            section.offset,
            section.size,
            section.link,
            section.info,
            section.alignment,
            section.entry_size,
            c_string(names, section.name_offset) if section.name_offset else "",
        ))
    sections = named_sections

    symbol_sections = [section for section in sections if section.type == SHT_SYMTAB]
    if len(symbol_sections) != 1:
        fail("expected exactly one .symtab section")
    symbol_section = symbol_sections[0]
    if symbol_section.name != ".symtab" or symbol_section.entry_size != SYMBOL.size:
        fail("symbol table format is invalid")
    if symbol_section.link >= len(sections):
        fail("symbol table string-table link is invalid")
    strings_section = sections[symbol_section.link]
    if strings_section.type != SHT_STRTAB:
        fail("symbol table does not point to a string table")
    strings = data[strings_section.offset:strings_section.offset + strings_section.size]
    if symbol_section.size % symbol_section.entry_size:
        fail("symbol table has partial entries")

    symbols: list[ElfSymbol] = []
    for offset in range(symbol_section.offset, symbol_section.offset + symbol_section.size, symbol_section.entry_size):
        values = SYMBOL.unpack_from(data, offset)
        name = c_string(strings, values[0]) if values[0] else ""
        symbols.append(ElfSymbol(name, *values[1:]))

    undefined = [symbol.name or "<anonymous>" for symbol in symbols[1:] if symbol.section_index == SHN_UNDEF]
    if undefined:
        fail("undefined imports are not allowed: " + ", ".join(undefined))

    entry_symbols = [symbol for symbol in symbols if symbol.name == "module_initialize"]
    if len(entry_symbols) != 1:
        fail("module_initialize must be exported exactly once")
    entry_symbol = entry_symbols[0]
    if entry_symbol.section_index == SHN_UNDEF or (entry_symbol.info & 0x0F) != STT_FUNC:
        fail("module_initialize is not a defined function")
    text_section = next(
        (section for section in sections if section.name == ".text"),
        None,
    )
    if text_section is None:
        fail("missing required NuttX module section .text")
    if entry_symbol.section_index >= len(sections):
        fail("module_initialize has an invalid section index")
    if sections[entry_symbol.section_index].name != ".text":
        fail("module_initialize must be defined in .text")

    # The s441 modlib ET_REL path computes text_base + e_entry and invokes it
    # with BLX.  e_entry therefore must name the exported Thumb initializer,
    # rather than staying at the generic ET_REL default of zero.
    if e_entry == 0:
        fail("s441 ET_REL module entry must not be zero")
    if (e_entry & 1) == 0:
        fail("s441 ET_REL module entry must be a Thumb address")
    if e_entry != entry_symbol.value:
        fail("ELF entry must equal module_initialize")
    entry_offset = e_entry & ~1
    if entry_offset < text_section.address or entry_offset >= text_section.address + text_section.size:
        fail("ELF entry is outside .text")

    required_sections = {
        ".text": (SHF_ALLOC | SHF_EXECINSTR, False),
        ".rodata": (SHF_ALLOC, False),
        ".data": (SHF_ALLOC | SHF_WRITE, False),
        ".bss": (SHF_ALLOC | SHF_WRITE, True),
    }
    named_sections_by_name = {section.name: section for section in sections}
    for name, (required_flags, must_be_nobits) in required_sections.items():
        section = named_sections_by_name.get(name)
        if section is None:
            fail("missing required NuttX module section " + name)
        if not has_flag(section.flags, required_flags):
            fail("section " + name + " has incompatible flags")
        if must_be_nobits != (section.type == SHT_NOBITS):
            fail("section " + name + " has an incompatible type")

    allowed_alloc_sections = set(required_sections) | {".ARM.exidx", ".init_array"}
    unexpected_alloc_sections = sorted(
        section.name or "<anonymous>"
        for section in sections
        if has_flag(section.flags, SHF_ALLOC)
        and section.name not in allowed_alloc_sections
    )
    if unexpected_alloc_sections:
        fail("unexpected allocated sections: " + ", ".join(unexpected_alloc_sections))

    prohibited = {".preinit_array"}
    found_prohibited = sorted(section.name for section in sections if section.name in prohibited)
    if found_prohibited:
        fail("constructor/destructor sections are prohibited: " + ", ".join(found_prohibited))

    loaded_size = section_footprint(sections)
    verify_absolute_addresses(data, sections, symbols)
    relocations: set[int] = set()
    for section in sections:
        if section.type == SHT_RELA:
            fail("SHT_RELA relocations are not accepted by this target profile")
        if section.type != SHT_REL:
            continue
        if section.entry_size != RELOCATION.size or section.size % section.entry_size:
            fail("invalid REL relocation section " + section.name)
        if section.link >= len(sections) or sections[section.link].type != SHT_SYMTAB:
            fail("relocation section has no valid symbol table: " + section.name)
        if section.info == 0 or section.info >= len(sections):
            fail("relocation section has no valid target: " + section.name)
        target_section = sections[section.info]
        if not has_flag(target_section.flags, SHF_ALLOC):
            fail("relocation target is not allocated: " + section.name)
        for offset in range(section.offset, section.offset + section.size, section.entry_size):
            relocation_offset, info = RELOCATION.unpack_from(data, offset)
            if relocation_offset + 4 > target_section.size:
                fail("relocation is outside target section: " + section.name)
            symbol_index = info >> 8
            if symbol_index >= len(symbols):
                fail("relocation references an out-of-range symbol")
            if symbol_index and symbols[symbol_index].section_index == SHN_UNDEF:
                fail("relocation references an undefined symbol")
            relocation_type = info & 0xFF
            allowed_types = {R_ARM_ABS32}
            if section.name == ".rel.ARM.exidx":
                # Clang emits NONE personality records and PREL31 function
                # ranges for the unwind index, matching device modules.
                allowed_types.update({0, 42})  # R_ARM_NONE, R_ARM_PREL31
            if section.name == ".rel.init_array":
                allowed_types.add(R_ARM_TARGET1)
            if relocation_type not in allowed_types:
                fail("unsupported relocation type " + str(relocation_type)
                     + " in " + section.name)
            relocations.add(relocation_type)

    metadata = {
        "entry": e_entry,
        "entry_symbol": entry_symbol.value,
        "loaded_size": loaded_size,
        "file_size": len(data),
        "bss_size": named_sections_by_name[".bss"].size,
    }
    return data, metadata, sections, symbols, relocations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("--max-loaded-size", type=int, required=True)
    parser.add_argument("--max-bss-size", type=int, required=True)
    args = parser.parse_args()

    try:
        _, metadata, sections, symbols, relocations = parse_elf(args.module)
        if metadata["loaded_size"] >= args.max_loaded_size:
            fail(
                "SHF_ALLOC size " + str(metadata["loaded_size"])
                + " must be strictly less than " + str(args.max_loaded_size)
            )
        if metadata["bss_size"] > args.max_bss_size:
            fail(
                ".bss size " + str(metadata["bss_size"])
                + " exceeds limit " + str(args.max_bss_size)
            )
    except (OSError, VerificationError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1

    relocation_list = ", ".join(str(value) for value in sorted(relocations)) or "none"
    print("OK: " + str(args.module))
    print("  file size: " + str(metadata["file_size"]) + " bytes")
    print("  SHF_ALLOC total: " + str(metadata["loaded_size"]) + " bytes")
    print("  .bss: " + str(metadata["bss_size"]) + " bytes")
    print("  entry: 0x" + format(metadata["entry"], "08x"))
    print("  module_initialize: 0x" + format(metadata["entry_symbol"], "08x"))
    print("  defined symbols: " + str(len(symbols) - 1))
    print("  ARM relocation types: " + relocation_list)
    print("  sections: " + ", ".join(section.name or "<null>" for section in sections))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
