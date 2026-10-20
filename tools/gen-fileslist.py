#!/usr/bin/env python3
"""
ISO XML to FilesList Generator

Parses iso.xml and generates fileslist.h and fileslist.c with:
- String table of all CD file paths
- Enum indices based on filename (slashes → underscores)

Usage:
    python gen-fileslist.py [iso.xml]
    python gen-fileslist.py --test

Output:
    fileslist.h - Enum definitions + extern declaration
    fileslist.c - String table array
"""

import sys
import os
import xml.etree.ElementTree as ET
from pathlib import Path


def normalize_path(path: str) -> str:
    """
    Normalize CD path to \\ format.
    
    Examples:
        "RES\\SAVICO.TIM" -> "\\RES\\SAVICO.TIM"
        "RES\\AUDIO\\PIANO.VAG" -> "\\RES\\AUDIO\\PIANO.VAG"
    """
    if not path.startswith("\\"):
        path = "\\" + path
    return path


def get_enum_name(path: str) -> str:
    """
    Generate enum name from file path.
    
    Converts slashes to underscores and prefixes with FIDX__.
    
    Examples:
        "RES\\SAVICO.TIM" -> "FIDX__RES_SAVICO_TIM"
        "RES\\AUDIO\\PIANO.VAG" -> "FIDX__RES_AUDIO_PIANO_VAG"
    """
    # Replace backslashes and forward slashes with underscores
    name = path.replace("\\", "_").replace("/", "_")
    # Remove any leading dots from path components
    name = name.replace(".", "_")
    # Prefix with FIDX__
    return f"FIDX__{name.upper()}"


def parse_iso_xml(xml_path: str) -> list:
    """
    Parse iso.xml and extract all file entries with full paths.
    
    Returns sorted list of (normalized_path, cd_path) tuples.
    """
    tree = ET.parse(xml_path)
    root = tree.getroot()
    
    files = []
    
    # Try different approaches to find directory_tree
    # It might be wrapped in track or be the direct child
    directory_tree = None
    
    # Check if root is iso_project and find track first
    if root.tag == "iso_project":
        track = root.find("track")
        if track is not None:
            directory_tree = track.find("directory_tree")
    
    # Also try direct child
    if directory_tree is None:
        directory_tree = root.find("directory_tree")
    
    if directory_tree is None:
        print(f"Error: No <directory_tree> found in {xml_path}", file=sys.stderr)
        sys.exit(1)
    
    # Process all file elements and build full paths
    # Track directory context using a stack-based approach
    
    def build_full_path_for_file(file_elem):
        """Build full path for a file element by examining parent dirs."""
        name = file_elem.get("name")
        if not name:
            return None
        
        full_path = name
        current = file_elem
        
        # Walk up through parent elements
        # We need to manually traverse since ElementTree has no getparent()
        # Strategy: iterate through all elements and find those that are ancestors
        
        # Simpler approach: use a pre-order traversal that builds path as it goes
        return full_path
    
    # Use a stack-based pre-order traversal to build paths
    def traverse_with_context(elem, dir_stack):
        """Traverse element tree, building full paths for files."""
        results = []
        
        if elem.tag == "file":
            name = elem.get("name")
            if name:
                # Build full path from stack (most recent dirs first)
                if dir_stack:
                    full_path = dir_stack[0] + "\\" + name
                else:
                    full_path = name
                
                cd_path = normalize_path(full_path)
                enum_name = get_enum_name(full_path)
                results.append((cd_path, enum_name))
        
        elif elem.tag == "dir":
            dir_name = elem.get("name")
            if dir_name:
                # Push this dir onto stack and process children
                dir_stack.append(dir_name)
                for child in elem:
                    results.extend(traverse_with_context(child, dir_stack))
                dir_stack.pop()
            else:
                # dir without name - still process children
                for child in elem:
                    results.extend(traverse_with_context(child, dir_stack))
        
        else:
            # Other elements (comments, text) - recurse
            for child in elem:
                results.extend(traverse_with_context(child, dir_stack))
        
        return results
    
    # Traverse from directory_tree with empty stack
    all_files = traverse_with_context(directory_tree, [])
    files = all_files
    
    # Sort by normalized path for deterministic ordering
    files.sort(key=lambda x: x[0])
    
    return files


def generate_fileslist_h(files: list, output_path: str):
    """
    Generate fileslist.h with enum definitions and extern declaration.
    """
    lines = [
        "//",
        "// THIS FILE IS AUTO-GENERATED, DO NOT EDIT BY HAND.",
        "//",
        f"// Generated from: {Path(output_path).parent / 'iso.xml'}",
        "//",
        "",
        "",
    ]
    
    # Generate enum definitions
    c = 0
    for i, (cd_path, enum_name) in enumerate(files):
        lines.append(f"#define {enum_name}   ({i})")
        c += 1
    lines.append(f"#define FIDX__COUNT ({c})");
    
    lines.extend([
        "",
        "",
        "// String table for file paths (indexed by enum above)",
        "",
        "extern const char* file_strings_table[];",
        "",
        "",
    ])
    
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    
    print(f"Generated: {output_path}")


def generate_fileslist_c(files: list, output_path: str):
    """
    Generate fileslist.c with string table array.
    """
    lines = [
        "//",
        "// THIS FILE IS AUTO-GENERATED, DO NOT EDIT BY HAND.",
        "//",
        f"// Generated from: {Path(output_path).parent / 'iso.xml'}",
        "//",
        "// String table containing all CD file paths.",
        "// Indexed by FIDX__... macros defined in fileslist.h",
        "",
        "",
        "const char* file_strings_table[] = ",
        "{",
    ]
    
    for i, (cd_path, enum_name) in enumerate(files):
        # Escape backslashes for C string
        escaped_path = cd_path.replace("\\", "\\\\")
        comma = "," if i < len(files) - 1 else ""
        lines.append(f'    "{escaped_path}"{comma}')
    
    lines.extend([
        "};",
        "",
        "",
    ])
    
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    
    print(f"Generated: {output_path}")


def run_tests():
    """
    Run basic tests on the generator.
    """
    print("Running gen-fileslist.py tests...")
    print("=" * 50)
    
    # Test 1: Check if iso.xml exists
    iso_path = Path(__file__).parent.parent / "iso.xml"
    if not iso_path.exists():
        print(f"Test 1 FAILED: iso.xml not found at {iso_path}")
        return False
    print(f"Test 1 PASSED: Found iso.xml at {iso_path}")
    
    # Test 2: Parse XML
    try:
        files = parse_iso_xml(str(iso_path))
        print(f"Test 2 PASSED: Parsed {len(files)} files from iso.xml")
    except Exception as e:
        print(f"Test 2 FAILED: {e}")
        return False
    
    # Test 3: Verify paths are normalized
    for cd_path, _ in files:
        if not cd_path.startswith(".\\"):
            print(f"Test 3 FAILED: Path not normalized: {cd_path}")
            return False
    print("Test 3 PASSED: All paths normalized with .\\ prefix")
    
    # Test 4: Verify enum names
    for _, enum_name in files:
        if not enum_name.startswith("FIDX__"):
            print(f"Test 4 FAILED: Invalid enum name: {enum_name}")
            return False
        if "_" not in enum_name:
            print(f"Test 4 FAILED: Enum name should have underscores: {enum_name}")
            return False
    print("Test 4 PASSED: All enum names valid (FIDX__ prefix, underscores)")
    
    # Test 5: Verify no duplicates
    paths = [p[0] for p in files]
    if len(paths) != len(set(paths)):
        print("Test 5 FAILED: Duplicate paths found")
        return False
    print("Test 5 PASSED: No duplicate paths")
    
    # Test 6: Generate files
    try:
        h_path = iso_path.parent / "fileslist.h"
        c_path = iso_path.parent / "fileslist.c"
        generate_fileslist_h(files, str(h_path))
        generate_fileslist_c(files, str(c_path))
        print("Test 6 PASSED: Generated fileslist.h and fileslist.c")
    except Exception as e:
        print(f"Test 6 FAILED: {e}")
        return False
    
    # Test 7: Verify generated files
    h_path = iso_path.parent / "fileslist.h"
    c_path = iso_path.parent / "fileslist.c"
    
    if not h_path.exists():
        print("Test 7 FAILED: fileslist.h not found")
        return False
    if not c_path.exists():
        print("Test 7 FAILED: fileslist.c not found")
        return False
    
    # Check fileslist.h content
    with open(h_path) as f:
        h_content = f.read()
    if "THIS FILE IS AUTO-GENERATED" not in h_content:
        print("Test 7 FAILED: fileslist.h missing auto-generation comment")
        return False
    if "extern const char* file_strings_table[]" not in h_content:
        print("Test 7 FAILED: fileslist.h missing extern declaration")
        return False
    
    # Check fileslist.c content
    with open(c_path) as f:
        c_content = f.read()
    if "THIS FILE IS AUTO-GENERATED" not in c_content:
        print("Test 7 FAILED: fileslist.c missing auto-generation comment")
        return False
    if "file_strings_table[]" not in c_content:
        print("Test 7 FAILED: fileslist.c missing array declaration")
        return False
    
    print("Test 7 PASSED: Generated files valid")
    
    print("=" * 50)
    print("All tests passed!")
    return True


def main():
    """
    Main entry point.
    """
    # Check for --test flag
    if "--test" in sys.argv:
        return run_tests()
    
    # Get input file path
    if len(sys.argv) > 1:
        xml_path = sys.argv[1]
    else:
        xml_path = str(Path(__file__).parent.parent / "iso.xml")
    
    # Check if iso.xml exists
    if not Path(xml_path).exists():
        print(f"Error: iso.xml not found at {xml_path}", file=sys.stderr)
        sys.exit(1)
    
    # Parse XML
    try:
        files = parse_iso_xml(xml_path)
    except ET.ParseError as e:
        print(f"Error: Failed to parse XML: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    
    if not files:
        print("Warning: No files found in iso.xml", file=sys.stderr)
    
    # Generate output files
    output_dir = Path(xml_path).parent
    generate_fileslist_h(files, str(output_dir / "source" / "fileslist.h"))
    generate_fileslist_c(files, str(output_dir / "source" / "fileslist.c"))
    
    print(f"Total files: {len(files)}")
    print("Done!")


if __name__ == "__main__":
    main()
