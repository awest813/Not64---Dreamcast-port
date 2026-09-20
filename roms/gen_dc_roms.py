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
            # --- 32-bit shift / divide block ---------------------------------
            # These caught real LP64 bugs in pure_interp.c: `long` is 32-bit on
            # SH4 and PPC but 64-bit on the host, so SRL/SRLV/DIVU shifted or
            # divided a sign-extended 64-bit value. A negative operand is the
            # whole point of this block - a positive one passes either way.
            i_type(15, 0, 12, 0x95F2),      # lui  r12, 0x95F2
            i_type(13, 12, 12, 0x08B7),     # ori  r12, r12, 0x08B7 -> 0x95F208B7
            i_type(13, 0, 13, 4),           # ori  r13, r0, 4
            special(0, 12, 14, 4, 0x02),    # srl  r14, r12, 4   -> 0x095F208B
            special(13, 12, 15, 0, 0x06),   # srlv r15, r12, r13  -> 0x095F208B
            special(0, 12, 16, 4, 0x03),    # sra  r16, r12, 4   -> 0xF95F208B
            special(13, 12, 17, 0, 0x07),   # srav r17, r12, r13  -> 0xF95F208B
            special(13, 1, 18, 0, 0x04),    # sllv r18, r1, r13   -> 0x00012340
            special(0, 1, 19, 0, 0x23),     # subu r19, r0, r1    -> 0xFFFFEDCC
            special(12, 13, 0, 0, 0x1B),    # divu r12, r13
            special(0, 0, 20, 0, 0x12),     # mflo r20            -> 0x257C822D
            special(0, 0, 21, 0, 0x10),     # mfhi r21            -> 3
            # Result whose bit 31 is set: the FULL 64-bit register must be the
            # sign extension. sign_extended() is a no-op on an LP64 host unless
            # it truncates to 32 bits first, and only a 64-bit check sees that.
            special(0, 12, 22, 8, 0x00),    # sll  r22, r12, 8 -> 0xFFFFFFFFF208B700
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
