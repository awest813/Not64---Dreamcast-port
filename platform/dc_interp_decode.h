#ifndef DC_INTERP_DECODE_H
#define DC_INTERP_DECODE_H
/* Pure-interpreter-only operand views. Keep PC->addr for exception/debug
 * reporting; delay-slot execution already replaces the current opcode. */
#undef rrt
#undef rrd
#undef rfs
#undef rrs
#undef rsa
#undef irt
#undef ioffset
#undef iimmediate
#undef irs
#undef ibase
#undef jinst_index
#undef lfbase
#undef lfft
#undef lfoffset
#undef cfft
#undef cffs
#undef cffd
#undef rrt32
#undef rrd32
#undef rrs32
#undef irs32
#undef irt32
#define rrt reg[(op >> 16) & 31]
#define rrd reg[(op >> 11) & 31]
#define rfs ((op >> 11) & 31)
#define rrs reg[(op >> 21) & 31]
#define rsa ((op >> 6) & 31)
#define irt rrt
#define ioffset ((short)op)
#define iimmediate ((short)op)
#define irs rrs
#define ibase rrs
#define jinst_index (op & 0x03ffffff)
#define lfbase ((op >> 21) & 31)
#define lfft ((op >> 16) & 31)
#define lfoffset ((short)op)
#define cfft ((op >> 16) & 31)
#define cffs ((op >> 11) & 31)
#define cffd ((op >> 6) & 31)
#define rrt32 (*(int *)&rrt)
#define rrd32 (*(int *)&rrd)
#define rrs32 (*(int *)&rrs)
#define irs32 rrs32
#define irt32 rrt32
#endif
