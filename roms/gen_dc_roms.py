#!/usr/bin/env python3
"""Emit tiny z64 images used by the Dreamcast host bring-up."""

from pathlib import Path

HERE = Path(__file__).resolve().parent


def be_words(*words):
    out = bytearray()
    for w in words:
        out.extend(int(w).to_bytes(4, "big"))
    return bytes(out)


def special(rs, rt, rd, sa, fn):
    return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | fn


def i_type(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def header(name, pc=0, cart_id=b"\0\0", country=0, version=0):
    name_b = name.encode("ascii")[:20].ljust(20, b" ")
    h = bytearray(0x40)
    h[0:4] = b"\x80\x37\x12\x40"
    h[0x08:0x0C] = int(pc).to_bytes(4, "big")
    h[0x20:0x34] = name_b
    # Byte- and halfword-addressed tail. The DC loader stores ROM words
    # byte-reversed, so these only decode if rom_dc.c un-swaps the header.
    h[0x3C:0x3E] = cart_id
    h[0x3E] = country
    h[0x3F] = version
    return bytes(h)


def write_rom(path, name, ipl_words, size=4096, **hdr):
    blob = bytearray(header(name, **hdr))
    blob.extend(be_words(*ipl_words))
    if len(blob) > size:
        raise SystemExit("IPL too large for dummy ROM")
    blob.extend(b"\x00" * (size - len(blob)))
    path.write_bytes(blob)
    print("wrote", path, "(%d bytes) name=%r ipl=%d insns" % (size, name, len(ipl_words)))


def main():
    # Dummy: branch to self in the delay-slotted IPL window.
    write_rom(
        HERE / "dc_dummy.z64",
        "DC BRINGUP TEST",
        [i_type(4, 0, 0, -1), 0],
    )

    # CPU smoke test executed from 0xa4000040 (copied out of ROM+0x40).
    #   ori  r1, r0, 0x1234
    #   ori  r2, r0, 0x00FF
    #   addu r3, r1, r2          -> 0x1333
    #   xor  r4, r1, r2          -> 0x12CB
    #   andi r9, r1, 0x00F0      -> 0x0030
    #   sll  r7, r2, 8           -> 0xFF00
    #   lui  r5, 0x8000
    #   sw   r3, 0(r5)
    #   lw   r6, 0(r5)           -> 0x1333
    #   addi r10, r3, 1          -> 0x1334
    #   slti r11, r10, 0x2000    -> 1
    #   bne  r1, r1, +1          not taken; delay nop
    #   nop
    #   beq  r0, r0, -1
    #   nop
    # cart_id 'DO' + country 'E' is a ROM_TABLE entry, so isEEPROM16k() also
    # has to come out 1 for the header un-swap to be considered correct.
    write_rom(
        HERE / "dc_cputest.z64",
        "DC CPUTEST",
        [
            i_type(13, 0, 1, 0x1234),
            i_type(13, 0, 2, 0x00FF),
            special(1, 2, 3, 0, 0x21),
            special(1, 2, 4, 0, 0x26),
            i_type(12, 1, 9, 0x00F0),
            special(0, 2, 7, 8, 0x00),
            i_type(15, 0, 5, 0x8000),
            i_type(43, 5, 3, 0),
            i_type(35, 5, 6, 0),
            i_type(8, 3, 10, 1),
            i_type(10, 10, 11, 0x2000),
            i_type(5, 1, 1, 1),
            0,
            i_type(4, 0, 0, -1),
            0,
        ],
        cart_id=b"DO",
        country=0x45,
        version=0x01,
    )


if __name__ == "__main__":
    main()
