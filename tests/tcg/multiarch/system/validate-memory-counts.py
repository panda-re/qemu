#!/usr/bin/env python3
#
# validate-memory-counts.py: check we instrumented memory properly
#
# This program takes two inputs:
#   - the mem plugin output
#   - the memory binary output
#
# Copyright (C) 2024 Linaro Ltd
#
# SPDX-License-Identifier: GPL-2.0-or-later

import sys

def extract_counts(path):
    """
    Load the output from path and extract the lines containing:

      Test data start: 0x40214000
      Test data end: 0x40218001
      Test data read: 2522280
      Test data write: 262111

    From the stream of data. Extract the values for use in the
    validation function.
    """
    start_address = None
    end_address = None
    read_count = 0
    write_count = 0
    with open(path, 'r') as f:
        for line in f:
            if line.startswith("Test data start:"):
                start_address = int(line.split(':')[1].strip(), 16)
            elif line.startswith("Test data end:"):
                end_address = int(line.split(':')[1].strip(), 16)
            elif line.startswith("Test data read:"):
                read_count = int(line.split(':')[1].strip())
            elif line.startswith("Test data write:"):
                write_count = int(line.split(':')[1].strip())
    return start_address, end_address, read_count, write_count


def parse_plugin_output(path, start, end):
    """
    Load the plugin output from path in the form of:

      Region Base, Reads, Writes, Seen all
      0x0000000040004000, 31093, 0, false
      0x0000000040214000, 2522280, 278579, true
      0x0000000040000000, 137398, 0, false
      0x0000000040210000, 54727397, 33721956, false

    And extract the ranges that match test data start and end and
    return the results.
    """
    total_reads = 0
    total_writes = 0
    seen_all = False

    with open(path, 'r') as f:
        next(f)  # Skip the header
        for line in f:

            if line.startswith("Region Base"):
                continue

            parts = line.strip().split(', ')
            if len(parts) != 4:
                continue

            region_base = int(parts[0], 16)
            reads = int(parts[1])
            writes = int(parts[2])

            if start <= region_base < end: # Checking if within range
                total_reads += reads
                total_writes += writes
                seen_all = parts[3] == "true"

    return total_reads, total_writes, seen_all

def main():
    if len(sys.argv) != 3:
        print("Usage: <script_name>.py <memory_binary_output_path> <mem_plugin_output_path>")
        sys.exit(1)

    memory_binary_output_path = sys.argv[1]
    mem_plugin_output_path = sys.argv[2]

    # Extract counts from memory binary
    start, end, expected_reads, expected_writes = extract_counts(memory_binary_output_path)

    if start is None or end is None:
        print("Failed to extract start or end address from memory binary output.")
        sys.exit(1)

    # Parse plugin output
    actual_reads, actual_writes, seen_all = parse_plugin_output(mem_plugin_output_path, start, end)

    if not seen_all:
        print("Fail: didn't instrument all accesses to test_data.")
        sys.exit(1)

    # Compare and report
    if actual_reads == expected_reads and actual_writes == expected_writes:
        sys.exit(0)
    else:
        print("Fail: The memory reads and writes count does not match.")
        print(f"Expected Reads: {expected_reads}, Actual Reads: {actual_reads}")
        print(f"Expected Writes: {expected_writes}, Actual Writes: {actual_writes}")
        sys.exit(1)

if __name__ == "__main__":
    main()
