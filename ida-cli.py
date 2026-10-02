#!/usr/bin/env python3
"""
IDA Pro CLI Tool
Headless IDA Pro CLI powered by idalib & IDAPython.
"""
import sys
import os
import argparse

try:
    import idapro
except ImportError:
    print("[ERROR] idapro Python package is not installed or not activated.")
    print("Please ensure IDA Pro 9.0+ idalib is activated via py-activate-idalib.py.")
    sys.exit(1)

import ida_idaapi
import ida_funcs
import ida_name
import ida_bytes
import ida_lines
import ida_segment
import ida_auto

def cmd_info(args):
    db_path = os.path.abspath(args.file)
    print(f"[*] Opening: {db_path}")
    idapro.open_database(db_path, False)
    
    seg_qty = ida_segment.get_segm_qty()
    print(f"[+] Total Segments: {seg_qty}")
    for i in range(seg_qty):
        seg = ida_segment.getnseg(i)
        name = ida_segment.get_segm_name(seg)
        print(f"    - [{hex(seg.start_ea)} - {hex(seg.end_ea)}] {name} (Class: {ida_segment.get_segm_class(seg)}, Perms: {seg.perm})")
    
    idapro.close_database()

def cmd_find(args):
    db_path = os.path.abspath(args.file)
    query = args.query.lower()
    print(f"[*] Searching for '{query}' in {db_path}...")
    idapro.open_database(db_path, False)
    
    matches = []
    qty = ida_name.get_nlist_size()
    for i in range(qty):
        ea = ida_name.get_nlist_ea(i)
        name = ida_name.get_nlist_name(i)
        if name and query in name.lower():
            matches.append((ea, name))
            if len(matches) >= args.limit:
                break
                
    print(f"[+] Found {len(matches)} matching symbol(s):")
    for ea, name in matches:
        f = ida_funcs.get_func(ea)
        extra = f" (Function size: {f.size()} bytes)" if f else ""
        print(f"    {hex(ea)}: {name}{extra}")
        
    idapro.close_database()

def cmd_disasm(args):
    db_path = os.path.abspath(args.file)
    ea = int(args.address, 16) if args.address.startswith("0x") or args.address.startswith("0X") else int(args.address, 10)
    lines_count = args.lines
    print(f"[*] Disassembling {lines_count} instructions starting at {hex(ea)} in {db_path}...")
    idapro.open_database(db_path, False)
    
    cur_ea = ea
    for _ in range(lines_count):
        raw_line = ida_lines.generate_disasm_line(cur_ea, 0)
        clean = ida_lines.tag_remove(raw_line) if raw_line else ""
        name = ida_name.get_name(cur_ea)
        prefix = f"<{name}>:\n" if name and cur_ea == ea else ""
        print(f"{prefix}    {hex(cur_ea)}: {clean}")
        sz = ida_bytes.get_item_size(cur_ea)
        cur_ea += sz if sz > 0 else 4
        
    idapro.close_database()

def cmd_script(args):
    db_path = os.path.abspath(args.file)
    script_path = os.path.abspath(args.script)
    if not os.path.isfile(script_path):
        print(f"[ERROR] Script file not found: {script_path}")
        sys.exit(1)
        
    print(f"[*] Opening {db_path} and executing {script_path}...")
    idapro.open_database(db_path, args.auto_wait)
    if args.auto_wait:
        ida_auto.auto_wait()
    ida_idaapi.IDAPython_ExecScript(script_path, globals())
    idapro.close_database()

def main():
    parser = argparse.ArgumentParser(description="IDA Pro CLI Tool (Headless idalib powered)")
    subparsers = parser.add_subparsers(dest="command", required=True)
    
    # Subcommand: info
    p_info = subparsers.add_parser("info", help="Get binary & segment details")
    p_info.add_argument("file", help="Binary or IDB file (.so, .dll, .exe, .i64)")
    p_info.set_defaults(func=cmd_info)
    
    # Subcommand: find
    p_find = subparsers.add_parser("find", help="Search functions or symbols by name")
    p_find.add_argument("file", help="Binary or IDB file")
    p_find.add_argument("query", help="Symbol or function name pattern to search")
    p_find.add_argument("--limit", type=int, default=50, help="Max results to show (default: 50)")
    p_find.set_defaults(func=cmd_find)
    
    # Subcommand: disasm
    p_disasm = subparsers.add_parser("disasm", help="Disassemble instructions at an address")
    p_disasm.add_argument("file", help="Binary or IDB file")
    p_disasm.add_argument("address", help="Memory address/offset (e.g. 0x3C1248)")
    p_disasm.add_argument("--lines", "-n", type=int, default=10, help="Number of instructions to disassemble")
    p_disasm.set_defaults(func=cmd_disasm)
    
    # Subcommand: script
    p_script = subparsers.add_parser("script", help="Run an IDAPython script headlessly")
    p_script.add_argument("file", help="Binary or IDB file")
    p_script.add_argument("script", help="Python script file to run")
    p_script.add_argument("--auto-wait", action="store_true", help="Wait for full auto-analysis before script")
    p_script.set_defaults(func=cmd_script)
    
    args = parser.parse_args()
    args.func(args)

if __name__ == "__main__":
    main()
