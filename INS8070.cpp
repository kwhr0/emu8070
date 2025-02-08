// INS8070
// Copyright 2025 © Yasuo Kuwahara
// MIT License

#include "INS8070.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>

#define MIE 	1
#define MOV 	0x40
#define MCY 	0x80

#define ua(v)	(eac = (eac & 0xff00) | ((v) & 0xff))

static constexpr uint8_t cycletbl[] = {
//	 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
	 3, 5, 3, 3, 3, 3, 3, 3, 8, 4, 5, 4, 4,43, 3, 4, // 0x00
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16, // 0x10
	16, 3,16,16, 9, 9, 9, 9, 3, 3, 3, 3,37, 9, 7, 7, // 0x20
	 4, 4, 4, 4, 3, 3, 3, 3, 6, 5, 9, 5, 3, 3, 3, 3, // 0x30
	 4, 3, 3, 3, 5, 5, 5, 5, 4, 3, 3, 3, 7, 7, 7, 7, // 0x40
	 4, 3, 3, 3, 8, 3, 8, 8, 4, 3, 3, 3,10, 3,10,10, // 0x50
	 4, 3, 3, 3, 5, 3, 5, 5, 3, 3, 3, 3, 5, 3, 5, 5, // 0x60
	 4, 3, 3, 3, 5, 3, 5, 5, 4, 3, 3, 3, 5, 3, 5, 5, // 0x70
	10,10,10,10, 8,10,12,12,10,10,10,10, 3,10,12,12, // 0x80
	 8, 8, 8, 8, 3, 8,10,10, 8, 8, 8, 8, 3, 8,10,10, // 0x90
	10,10,10,10, 8,10,12,12, 3, 3, 3, 3, 3, 3, 3, 3, // 0xa0
	10,10,10,10, 8,10,12,12,10,10,10,10, 8,10,12,12, // 0xb0
	 7, 7, 7, 7, 5, 7, 9, 9, 7, 7, 7, 7, 3, 7, 9, 9, // 0xc0
	 7, 7, 7, 7, 5, 7, 9, 9, 7, 7, 7, 7, 5, 7, 9, 9, // 0xd0
	 7, 7, 7, 7, 5, 7, 9, 9, 3, 3, 3, 3, 3, 3, 3, 3, // 0xe0
	 7, 7, 7, 7, 5, 7, 9, 9, 7, 7, 7, 7, 5, 7, 9, 9, // 0xf0
};

INS8070::INS8070() {
#if INS8070_TRACE
	memset(tracebuf, 0, sizeof(tracebuf));
	tracep = tracebuf;
#endif
}

void INS8070::Reset() {
	eac = 0;
	s = 0x30;
	p[0] = p[1] = p[2] = p[3] = 0;
	irq = halted = false;
}

INS8070::u16 INS8070::ea(u8 op) {
	u16 r = 0;
	s8 o;
	switch (op & 7) {
		case 4: break; // imm
		case 5: r = 0xff00 | imm1(); break; // direct
		case 6: case 7: // auto index
			o = imm1();
			if (o < 0) r = p[op & 3] += o;
			else { r = p[op & 3]; p[op & 3] += o; }
			break;
		default: // index
			o = imm1();
			r = p[op & 3] + o;
			break;
	}
	return r;
}

void INS8070::add1(u8 v) {
	u8 ac = eac, r = ac + v;
	if (((r & ~ac & ~v) | (~r & ac & v)) & 0x80) s |= MOV;
	else s &= ~MOV;
	if (((ac & v) | (v & ~r) | (~r & ac)) & 0x80) s |= MCY;
	else s &= ~MCY;
	ua(r);
}

void INS8070::add2(u16 v) {
	u16 r = eac + v;
	if (((r & ~eac & ~v) | (~r & eac & v)) & 0x8000) s |= MOV;
	else s &= ~MOV;
	if (((eac & v) | (v & ~r) | (~r & eac)) & 0x8000) s |= MCY;
	else s &= ~MCY;
	eac = r;
}

void INS8070::sub1(u8 v) {
	u8 ac = eac, r = ac - v;
	if (((ac & ~v & ~r) | (~ac & v & r)) & 0x80) s |= MOV;
	else s &= ~MOV;
	if (((~ac & v) | (v & r) | (r & ~ac)) & 0x80) s &= ~MCY;
	else s |= MCY;
	ua(r);
}

void INS8070::sub2(u16 v) {
	u16 r = eac - v;
	if (((eac & ~v & ~r) | (~eac & v & r)) & 0x8000) s |= MOV;
	else s &= ~MOV;
	if (((~eac & v) | (v & r) | (r & ~eac)) & 0x8000) s &= ~MCY;
	else s |= MCY;
	eac = r;
}

int INS8070::ssm(u8 index) {
	u16 &r = p[index];
	u8 a = eac;
	for (int n = 0; n < 256; n++)
		if (ld1(r++) == a) {
			p[0] += 2;
			return n << 2;
		}
	r--;
	return -2;
}

static uint8_t input() {
	uint8_t c = getchar();
	if (c == 10) c = 13;
	if (islower(c)) c = toupper(c);
	return c;
}

static void output(int c) {
	putchar(c);
	fflush(stdout);
}

int INS8070::Execute(int n) {
	n >>= 2;
	int cycle = 0;
	do {
		if (irq && s & MIE) {
			irq = halted = false;
			s &= ~MIE;
			st2(p[1] -= 2, p[0]);
			p[0] = 3;
			cycle += 9;
		}
		if (halted) return 0;
#if INS8070_TRACE
		tracep->pc = p[0] + 1;
		tracep->index = tracep->opn = 0;
#endif
		u32 t32;
		u16 t16;
		u8 t8, op = imm1();
		switch (op) {
			case 0x00: break; // nop
			case 0x01: eac = eac << 8 | eac >> 8; break; // xch A,E
			case 0x02: ua(input()); break; // getc (emulator)
			case 0x03: output(eac & 0x7f); break; // putc (emulator)
			case 0x04:
#if INS8070_TRACE
				StopTrace();
#else
				emu_exit(); // exit (emulator)
#endif
			case 0x05: halted = true; break; // halt (emulator)
			case 0x06: ua(s); break; // ld A,S
			case 0x07: s = eac; break; // ld S,A
			case 0x08: st2(p[1] -= 2, eac); break; // push EA
			case 0x09: t = eac; break; // ld T,EA
			case 0x0a: st1(--p[1], eac); break; // push A
			case 0x0b: eac = t; break; // ld EA,T
			case 0x0c: eac >>= 1; break; // sr EA
			case 0x0d: t16 = eac % t; eac /= t; t = t16; break; // div (flags?)
			case 0x0e: ua(eac << 1); break; // sl
			case 0x0f: eac <<= 1; break; // sl EA
			case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
			case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
				st2(p[1] -= 2, p[0]); p[0] = ld2(0x20 + ((op & 0xf) << 1)); break; // call
			case 0x20: case 0x22: case 0x23:
				t16 = imm2(); st2(p[1] -= 2, p[op & 3]); p[op & 3] = t16; break; // jsr/pli P,s
			case 0x24: case 0x25: case 0x26: case 0x27: p[op & 3] = imm2(); break; // jmp/ld P,imm
			case 0x2c: t32 = (s16)(t & 0x7fff) * (s16)eac; eac = t32 >> 16; t = t32; break; // mpy (flags?)
			case 0x2d: t8 = eac;
				if (isdigit(t8)) { ua(t8 - '0'); p[0]++; }
				else p[0] += (s8)imm1();
				break; // bnd
			case 0x2e: case 0x2f: cycle += ssm(op & 3); break; // ssm
			case 0x30: case 0x31: case 0x32: case 0x33: eac = p[op & 3]; break; // ld EA,P
			case 0x38: ua(ld1(p[1])); p[1]++; break; // pop A
			case 0x39: s &= imm1(); break; // and S,imm
			case 0x3a: eac = ld2(p[1]); p[1] += 2; break; // pop EA
			case 0x3b: s |= imm1(); break; // or S,imm
			case 0x3c: ua(eac >> 1 & 0x7f); break; // sr A
			case 0x3d: ua(s & MCY | eac >> 1 & 0x7f); break; // srl A
			case 0x3e: ua(eac << 7 | eac >> 1 & 0x7f); break; // rr A
			case 0x3f: t8 = eac; ua(s & MCY | t8 >> 1); s = t8 & 1 ? s | MCY : s & ~MCY; break; // rrl A
			case 0x40: ua(eac >> 8); break; // ld A,E
			case 0x44: case 0x45: case 0x46: case 0x47: p[op & 3] = eac; break; // ld P,EA
			case 0x48: eac = eac << 8 | (eac & 0xff); break; // ld E,A
			case 0x4c: case 0x4d: case 0x4e: case 0x4f: std::swap(eac, p[op & 3]);  break; // xch EA,P
			case 0x50: ua(eac & eac >> 8); break; // and A,E
			case 0x54: case 0x56: case 0x57: st2(p[1] -= 2, p[op & 3]); break; // push P
			case 0x58: ua(eac | eac >> 8); break; // or A,E
			case 0x5c: case 0x5e: case 0x5f: p[op & 3] = ld2(p[1]); p[1] += 2; break; // ret/pop P
			case 0x60: ua(eac ^ eac >> 8); break; // xor A,E
			case 0x64: case 0x66: case 0x67: br(op, !(eac & 0x80)); break; // bp
			case 0x6c: case 0x6e: case 0x6f: br(op, !(eac & 0xff)); break; // bz
			case 0x70: add1(eac >> 8); break; // add A,E
			case 0x74: case 0x76: case 0x77: br(op); break; // bra
			case 0x78: sub1(eac >> 8); break; // sub A,E
			case 0x7c: case 0x7e: case 0x7f: br(op, eac & 0xff); break; // bnz
			case 0x80: case 0x81: case 0x82: case 0x83: case 0x85: case 0x86: case 0x87:
				eac = ld2(ea(op)); break; // ld EA,s
			case 0x84: eac = imm2(); break; // ld ea,imm
			case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x8d: case 0x8e: case 0x8f:
				st2(ea(op), eac); break; // st EA,d
			case 0x90: case 0x91: case 0x92: case 0x93: case 0x95: case 0x96: case 0x97:
				t16 = ea(op); st1(t16, ua(ld1(t16) + 1)); break; // ild A,d
			case 0x98: case 0x99: case 0x9a: case 0x9b: case 0x9d: case 0x9e: case 0x9f:
				t16 = ea(op); st1(t16, ua(ld1(t16) - 1)); break; // dld A,d
			case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa5: case 0xa6: case 0xa7:
				t = ld2(ea(op)); break; // ld T,s
			case 0xa4: t = imm2(); break; // ld T,imm
			case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb5: case 0xb6: case 0xb7:
				add2(ld2(ea(op))); break; // add EA,s
			case 0xb4: add2(imm2()); break; // add EA,imm
			case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbd: case 0xbe: case 0xbf:
				sub2(ld2(ea(op))); break; // sub EA,s
			case 0xbc: sub2(imm2()); break; // sub EA,imm
			case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc5: case 0xc6: case 0xc7:
				ua(ld1(ea(op))); break; // ld A,s
			case 0xc4: ua(imm1()); break; // ld A,imm
			case 0xc8: case 0xc9: case 0xca: case 0xcb: case 0xcd: case 0xce: case 0xcf:
				st1(ea(op), eac); break; // st A,d
			case 0xd0: case 0xd1: case 0xd2: case 0xd3: case 0xd5: case 0xd6: case 0xd7:
				ua(eac & ld1(ea(op))); break; // and A,s
			case 0xd4: ua(eac & imm1()); break; // and A,imm
			case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdd: case 0xde: case 0xdf:
				ua(eac | ld1(ea(op))); break; // or A,s
			case 0xdc: ua(eac | imm1()); break; // or A,imm
			case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe5: case 0xe6: case 0xe7:
				ua(eac ^ ld1(ea(op))); break; // xor A,s
			case 0xe4: ua(eac ^ imm1()); break; // xor A,imm
			case 0xf0: case 0xf1: case 0xf2: case 0xf3: case 0xf5: case 0xf6: case 0xf7:
				add1(ld1(ea(op))); break; // add A,s
			case 0xf4: add1(imm1()); break; // add A,imm
			case 0xf8: case 0xf9: case 0xfa: case 0xfb: case 0xfd: case 0xfe: case 0xff:
				sub1(ld1(ea(op))); break; // sub A,s
			case 0xfc: sub1(imm1()); break; // sub A,imm
			default:
#if INS8070_TRACE
				fprintf(stderr, "Illegal op: PC=%04x OP=%02x\n", p[0], op);
				StopTrace();
#endif
				break;
		}
#if INS8070_TRACE
		memcpy(tracep->p, p, sizeof(tracep->p));
		tracep->eac = eac;
		tracep->t = t;
		tracep->s = s;
#if INS8070_TRACE > 1
		if (++tracep >= tracebuf + TRACEMAX - 1) StopTrace();
#else
		if (++tracep >= tracebuf + TRACEMAX) tracep = tracebuf;
#endif
#endif
		cycle += cycletbl[op];
	} while (!halted && cycle < n);
	return halted ? 0 : (cycle - n) << 2;
}

#if INS8070_TRACE
#include <string>
void INS8070::StopTrace() {
	TraceBuffer *endp = tracep;
	int i = 0, j;
	FILE *fo;
	if (!(fo = fopen((std::string(getenv("HOME")) + "/Desktop/trace.txt").c_str(), "w"))) exit(1);
	do {
		if (++tracep >= tracebuf + TRACEMAX) tracep = tracebuf;
		fprintf(fo, "%4d %04X ", i++, tracep->pc);
		for (j = 0; j < 3; j++) fprintf(fo, j < tracep->opn ? "%02X " : "   ", tracep->op[j]);
		fprintf(fo, "%04X %04X %04X %04X ", tracep->p[0], tracep->p[1], tracep->p[2], tracep->p[3]);
		fprintf(fo, "%04X %04X ", tracep->eac, tracep->t);
		fprintf(fo, "%c%c ", tracep->s & 0x80 ? 'C' : '-', tracep->s & 0x40 ? 'V' : '-');
		for (Acs *p = tracep->acs; p < tracep->acs + tracep->index; p++) {
			switch (p->type) {
				case acsLoad1:
					fprintf(fo, "L %04X %02X ", p->adr, p->data);
					break;
				case acsLoad2:
					fprintf(fo, "L %04X %04X ", p->adr, p->data);
					break;
				case acsStore1:
					fprintf(fo, "S %04X %02X ", p->adr, p->data);
					break;
				case acsStore2:
					fprintf(fo, "S %04X %04X ", p->adr, p->data);
					break;
			}
		}
		fprintf(fo, "\n");
	} while (tracep != endp);
	fclose(fo);
	fprintf(stderr, "trace dumped.\n");
	exit(1);
}
#endif	// INS8070_TRACE
