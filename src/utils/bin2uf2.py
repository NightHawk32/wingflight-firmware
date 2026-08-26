#!/usr/bin/python

# Converts a flat firmware .bin into a UF2 image for the RP2 USB bootloader
# (drag-and-drop BOOTSEL flashing). Self-contained re-implementation of the
# UF2 format described in pico-sdk's boot/uf2.h - no picotool/elf2uf2
# dependency required.

import argparse
import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000

UF2_BLOCK_SIZE   = 512
UF2_DATA_SIZE    = 256  # RP2 bootrom only honours the first 256 bytes of payload per block

def convert(infile, outfile, base_addr, family_id):
    with open(infile, 'rb') as f:
        data = f.read()

    padding = (-len(data)) % UF2_DATA_SIZE
    data += b'\x00' * padding

    num_blocks = len(data) // UF2_DATA_SIZE

    with open(outfile, 'wb') as out:
        for block_no in range(num_blocks):
            chunk = data[block_no * UF2_DATA_SIZE:(block_no + 1) * UF2_DATA_SIZE]
            block = struct.pack(
                '<IIIIIIII',
                UF2_MAGIC_START0,
                UF2_MAGIC_START1,
                UF2_FLAG_FAMILY_ID_PRESENT,
                base_addr + block_no * UF2_DATA_SIZE,
                UF2_DATA_SIZE,
                block_no,
                num_blocks,
                family_id,
            )
            block += chunk
            block += b'\x00' * (UF2_BLOCK_SIZE - len(block) - 4)
            block += struct.pack('<I', UF2_MAGIC_END)
            out.write(block)

    print('Wrote %d UF2 block(s) (%d bytes payload) to "%s"' % (num_blocks, len(data), outfile))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Convert a flat .bin firmware image to UF2.')
    parser.add_argument('-i', '--input', required=True, help='input .bin file')
    parser.add_argument('-o', '--output', required=True, help='output .uf2 file')
    parser.add_argument('--base-addr', type=lambda x: int(x, 0), required=True,
                         help='flash base address the .bin was linked at, e.g. 0x10000000')
    parser.add_argument('--family', type=lambda x: int(x, 0), required=True,
                         help='UF2 family ID, e.g. 0xe48bff59 for RP2350 ARM-Secure')
    args = parser.parse_args()

    convert(args.input, args.output, args.base_addr, args.family)
    sys.exit(0)
