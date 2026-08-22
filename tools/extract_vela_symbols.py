#!/usr/bin/env python3
"""Conservative static address discovery for a Vela AP image.

This is an analysis aid, not a symbol-table recovery tool. Vela AP images are
usually stripped, so results are labelled as string references or candidates
and must be validated in a debugger/disassembler before use as call targets.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

# Runtime mapping verified from the s441 3.100.028 crash PC and modlib
# disassembly. File offset N maps to 0x2c1c0000 + N at runtime.
DEFAULT_RUNTIME_BASE = 0x2C1C0000
EXPECTED_AP_SHA256 = "c1a738d70ff5284569439bbcfb1212a94f357cd81c6f715d7a7a34ef0155912a"
KEYWORDS = (
    "supervisor", "Supervisor", "lua_", "Lua", "modlib",
    "register_driver", "loaded_notification", "module_notification",
    "launcher", "lvgl", "APPTHREAD",
)

def u32(data: bytes, offset: int) -> int | None:
    if offset < 0 or offset + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, offset)[0]

def is_thumb(value: int, base: int, size: int) -> bool:
    address = value & ~1
    return bool(value & 1) and base <= address < base + size and address % 2 == 0

def unwrap_avb(data: bytes) -> tuple[bytes, int]:
    # AVB footer is the final 64 bytes; original_image_size is at +12.
    if len(data) >= 64 and data[-64:-60] == b"AVBf":
        image_size = struct.unpack_from("<Q", data, len(data) - 64 + 12)[0]
        if 0 < image_size <= len(data) - 64:
            return data[:image_size], 0
    return data, 0

def validate_ap(path: Path) -> str:
    path = path.expanduser()
    if not path.is_file():
        raise ValueError(f"找不到选择的文件: {path}")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != EXPECTED_AP_SHA256:
        raise ValueError(
            "拒绝查询：vela_ap.bin SHA-256 不匹配。\n"
            f"期望: {EXPECTED_AP_SHA256}\n实际: {digest}")
    return digest

def scan(path: Path, runtime_base: int, min_string: int) -> dict:
    raw_data = path.read_bytes()
    data, payload_offset = unwrap_avb(raw_data)
    digest = hashlib.sha256(data).hexdigest()
    strings = []
    pattern = re.compile(rb"[ -~]{%d,}" % min_string)
    for match in pattern.finditer(data):
        raw = match.group().decode("ascii", "replace")
        if any(keyword in raw for keyword in KEYWORDS):
            refs = []
            needle = struct.pack("<I", runtime_base + match.start())
            cursor = 0
            while True:
                hit = data.find(needle, cursor)
                if hit < 0:
                    break
                refs.append({"file_offset": hit, "runtime_address": runtime_base + hit,
                             "kind": "literal_pointer"})
                cursor = hit + 1
            strings.append({"text": raw[:240], "file_offset": match.start(),
                            "runtime_address": runtime_base + match.start(), "references": refs})
    candidates = []
    seen = set()
    for offset in range(0, len(data) - 4, 4):
        value = u32(data, offset)
        if value is None or not is_thumb(value, runtime_base, len(data)):
            continue
        target = value & ~1
        if target in seen:
            continue
        seen.add(target)
        candidates.append({"address": value, "file_offset": offset,
                           "kind": "thumb_pointer_candidate"})
    return {"tool": "extract_vela_symbols", "format": 2,
            "input": str(path), "sha256": digest, "size": len(data),
            "input_size": len(raw_data), "payload_offset": payload_offset,
            "runtime_base": runtime_base,
            "address_semantics": "runtime_address = runtime_base + file_offset",
            "warning": "Stripped-image candidates require disassembly and runtime validation before use as call targets.",
            "keyword_strings": strings,
            "thumb_pointer_candidates": candidates}

def write_outputs(result: dict, json_path: Path, header_path: Path | None) -> None:
    json_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if header_path is None:
        return
    lines = ["/* Generated candidates; validate before calling. */",
             "#pragma once", ""]
    for item in result["thumb_pointer_candidates"]:
        lines.append(f"/* file offset 0x{item['file_offset']:x} */")
        lines.append(f"#define VELA_CANDIDATE_{item['address']:08X} 0x{item['address']:08X}u")
    header_path.write_text("\n".join(lines) + "\n", encoding="ascii")

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Vela AP 符号地址分析器")
        self.geometry("900x620")
        default_image = Path.cwd() / "vela_ap.bin"
        self.path = tk.StringVar(value=str(default_image) if default_image.is_file() else "")
        self.base = tk.StringVar(value=hex(DEFAULT_RUNTIME_BASE))
        self.min_string = tk.IntVar(value=6)
        self.output = tk.StringVar()
        self._build()

    def _build(self):
        form = ttk.Frame(self, padding=10); form.pack(fill="x")
        ttk.Label(form, text="选择 AP 固件").grid(row=0, column=0, sticky="w")
        ttk.Entry(form, textvariable=self.path).grid(row=0, column=1, sticky="ew", padx=6)
        ttk.Button(form, text="选择", command=self.choose).grid(row=0, column=2)
        ttk.Label(form, text="运行时基址").grid(row=1, column=0, sticky="w", pady=6)
        ttk.Entry(form, textvariable=self.base, width=18).grid(row=1, column=1, sticky="w", padx=6)
        ttk.Label(form, text="最短字符串").grid(row=1, column=1, padx=(180, 6), sticky="w")
        ttk.Spinbox(form, from_=4, to=64, textvariable=self.min_string, width=6).grid(row=1, column=1, padx=(270, 6), sticky="w")
        self.scan_button = ttk.Button(form, text="扫描并导出", command=self.run_scan)
        self.scan_button.grid(row=1, column=2)
        form.columnconfigure(1, weight=1)
        self.text = tk.Text(self, wrap="none", font=("Menlo", 11)); self.text.pack(fill="both", expand=True, padx=10, pady=(0, 10))
        try:
            validate_ap(Path(self.path.get()))
            self.text.insert("end", "已验证目标 vela_ap.bin，可以查询。\n")
        except ValueError as exc:
            self.scan_button.state(["disabled"])
            self.text.insert("end", str(exc) + "\n")

    def choose(self):
        chosen = filedialog.askopenfilename(
            initialdir=str(Path.cwd()),
            filetypes=[("AP firmware", "*.bin"), ("All files", "*")])
        if chosen:
            self.path.set(chosen)
            try:
                digest = validate_ap(Path(chosen))
                self.scan_button.state(["!disabled"])
                self.text.delete("1.0", "end")
                self.text.insert("end", f"SHA-256 校验通过：{digest}\n可以查询。\n")
            except ValueError as exc:
                self.scan_button.state(["disabled"])
                self.text.delete("1.0", "end")
                self.text.insert("end", str(exc) + "\n")

    def run_scan(self):
        try:
            path = Path(self.path.get()).expanduser()
            validate_ap(path)
            runtime_base = int(self.base.get(), 0)
            result = scan(path, runtime_base, self.min_string.get())
            json_path = path.with_name(path.name + ".symbols.json")
            header_path = path.with_name(path.name + ".symbols.h")
            write_outputs(result, json_path, header_path)
            summary = {"input_sha256": result["sha256"], "size": result["size"],
                       "keyword_strings": len(result["keyword_strings"]),
                       "thumb_candidates": len(result["thumb_pointer_candidates"]),
                       "json": str(json_path), "header": str(header_path)}
            self.text.delete("1.0", "end"); self.text.insert("end", json.dumps(summary, ensure_ascii=False, indent=2))
        except Exception as exc:
            messagebox.showerror("扫描失败", str(exc))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", nargs="?", type=Path)
    parser.add_argument("--runtime-base", type=lambda value: int(value, 0),
                        default=DEFAULT_RUNTIME_BASE)
    parser.add_argument("--min-string", type=int, default=6)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--header", type=Path)
    args = parser.parse_args()
    if args.image:
        try:
            validate_ap(args.image)
        except ValueError as exc:
            parser.error(str(exc))
        result = scan(args.image, args.runtime_base, args.min_string)
        write_outputs(result, args.json or args.image.with_name(args.image.name + ".symbols.json"), args.header)
        print(json.dumps({"sha256": result["sha256"], "keyword_strings": len(result["keyword_strings"]),
                          "thumb_candidates": len(result["thumb_pointer_candidates"])}, indent=2))
    else:
        App().mainloop()

if __name__ == "__main__":
    main()
