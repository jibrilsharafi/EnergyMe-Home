#!/usr/bin/env python3
"""
Find unused #define macros in header files.

This script scans header files for #define statements and checks if they're
used anywhere in the codebase. Note: this is a heuristic approach and may
have false positives (e.g., macros used in conditional compilation).

Usage: python find_unused_defines.py [--include-guards] [--verbose]
"""

import os
import re
import argparse
from pathlib import Path
from collections import defaultdict
from tqdm import tqdm

# Directories to scan
HEADER_DIRS = ["include"]
SOURCE_DIRS = ["src", "include"]

# File extensions
HEADER_EXTENSIONS = {".h", ".hpp"}
SOURCE_EXTENSIONS = {".c", ".cpp", ".h", ".hpp", ".ino"}

# Patterns to exclude (common false positives)
EXCLUDE_PATTERNS = [
    r"^_.*_H$",           # Include guards like _FILE_H
    r"^_.*_HPP$",         # Include guards like _FILE_HPP
    r".*_H_$",            # Include guards like FILE_H_
    r".*_HPP_$",          # Include guards like FILE_HPP_
    r"^__.*__$",          # Double underscore (reserved)
]


def find_files(directories: list[str], extensions: set[str], base_path: Path) -> list[Path]:
    """Find all files with given extensions in directories."""
    files = []
    for directory in directories:
        dir_path = base_path / directory
        if not dir_path.exists():
            continue
        for file_path in dir_path.rglob("*"):
            if file_path.suffix in extensions and file_path.is_file():
                files.append(file_path)
    return files


def extract_defines(file_path: Path) -> dict[str, int]:
    """Extract all #define macro names from a file with line numbers."""
    defines = {}
    define_pattern = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)")
    
    try:
        with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
            for line_num, line in enumerate(f, 1):
                match = define_pattern.match(line)
                if match:
                    macro_name = match.group(1)
                    defines[macro_name] = line_num
    except Exception as e:
        print(f"Warning: Could not read {file_path}: {e}")
    
    return defines


def is_excluded(macro_name: str, include_guards: bool) -> bool:
    """Check if macro should be excluded from analysis."""
    if not include_guards:
        for pattern in EXCLUDE_PATTERNS:
            if re.match(pattern, macro_name):
                return True
    return False


def find_usages(macro_name: str, source_files: list[Path], definition_file: Path) -> list[tuple[Path, int]]:
    """Find all usages of a macro in source files."""
    usages = []
    # Match whole word only (not part of another identifier)
    pattern = re.compile(rf"\b{re.escape(macro_name)}\b")
    
    for file_path in source_files:
        try:
            with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
                for line_num, line in enumerate(f, 1):
                    # Skip the definition line itself
                    if file_path == definition_file:
                        if line.strip().startswith("#") and "define" in line and macro_name in line:
                            continue
                    
                    if pattern.search(line):
                        usages.append((file_path, line_num))
        except Exception:
            pass
    
    return usages


def main():
    parser = argparse.ArgumentParser(description="Find unused #define macros")
    parser.add_argument("--include-guards", action="store_true", 
                        help="Include potential include guards in analysis")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Show all macros, not just unused ones")
    parser.add_argument("--base-path", type=str, default=".",
                        help="Base path of the project")
    args = parser.parse_args()
    
    base_path = Path(args.base_path).resolve()
    print(f"Scanning project at: {base_path}\n")
    
    # Find all files
    header_files = find_files(HEADER_DIRS, HEADER_EXTENSIONS, base_path)
    source_files = find_files(SOURCE_DIRS, SOURCE_EXTENSIONS, base_path)
    
    print(f"Found {len(header_files)} header files")
    print(f"Found {len(source_files)} source files to search\n")
    
    # Extract all defines from headers
    all_defines: dict[str, tuple[Path, int]] = {}  # macro_name -> (file, line)
    for header in tqdm(header_files, desc="Extracting defines", unit="file"):
        defines = extract_defines(header)
        for macro_name, line_num in defines.items():
            if not is_excluded(macro_name, args.include_guards):
                all_defines[macro_name] = (header, line_num)
    
    print(f"Found {len(all_defines)} #define macros (excluding guards)\n")
    
    # Check usage of each define
    unused_defines: dict[Path, list[tuple[str, int]]] = defaultdict(list)
    used_count = 0
    
    for macro_name, (def_file, def_line) in tqdm(sorted(all_defines.items()), desc="Checking usages", unit="macro"):
        usages = find_usages(macro_name, source_files, def_file)
        
        if not usages:
            unused_defines[def_file].append((macro_name, def_line))
            if args.verbose:
                rel_path = def_file.relative_to(base_path)
                print(f"UNUSED: {macro_name} ({rel_path}:{def_line})")
        else:
            used_count += 1
            if args.verbose:
                rel_path = def_file.relative_to(base_path)
                print(f"USED:   {macro_name} ({rel_path}:{def_line}) - {len(usages)} usage(s)")
    
    # Summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"Total macros analyzed: {len(all_defines)}")
    print(f"Used macros: {used_count}")
    print(f"Potentially unused: {len(all_defines) - used_count}")
    
    if unused_defines:
        print("\n" + "=" * 60)
        print("POTENTIALLY UNUSED DEFINES (by file)")
        print("=" * 60)
        
        for file_path in sorted(unused_defines.keys()):
            rel_path = file_path.relative_to(base_path)
            macros = unused_defines[file_path]
            print(f"\n{rel_path} ({len(macros)} unused):")
            for macro_name, line_num in sorted(macros, key=lambda x: x[1]):
                print(f"  Line {line_num:4d}: {macro_name}")
    
    print("\n" + "=" * 60)
    print("NOTE: Some 'unused' macros may be:")
    print("  - Used in conditional compilation (#ifdef)")
    print("  - Used by external libraries")
    print("  - Platform-specific defines")
    print("  - Reserved for future use")
    print("=" * 60)


if __name__ == "__main__":
    main()
