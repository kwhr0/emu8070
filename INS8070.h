// INS8070
// Copyright 2025 © Yasuo Kuwahara
// MIT License

#include <cstdint>
#include "main.h" // emulator

#define INS8070_TRACE			0

#if INS8070_TRACE
#define INS8070_TRACE_LOG(adr, data, type) \
	if (tracep->index < ACSMAX) tracep->acs[tracep->index++] = { adr, (u16)data, type }
#else
#define INS8070_TRACE_LOG(adr, data, type)
#endif

class INS8070 {
	using s8 = int8_t;
	using u8 = uint8_t;
	using s16 = int16_t;
	using u16 = uint16_t;
	using u32 = uint32_t;
public:
	INS8070();
	void Reset();
	void SetMemoryPtr(u8 *p) { m = p; }
	int Execute(int n);
	void IRQ() { irq = true; }
	bool Halted() const { return halted; }
private:
	u8 imm1() {
		u8 o = m[++p[0]];
#if INS8070_TRACE
		if (tracep->opn < 3) tracep->op[tracep->opn++] = o;
#endif
		return o;
	}
	u16 imm2() {
		u16 o = (u16 &)m[++p[0]];
		++p[0];
#if INS8070_TRACE
		if (tracep->opn < 2) {
			(u16 &)tracep->op[tracep->opn] = o;
			tracep->opn += 2;
		}
#endif
		return o;
	}
	u8 ld1(u16 adr) {
		u8 data = 0;
		switch (adr) {
			case 0xfe00: data = file_getc(); break;
			case 0xfe01: data = keyboard_getc(); break;
			case 0xfe02: data = sound_fill(); break;
			default: data = m[adr]; break;
		}
		INS8070_TRACE_LOG(adr, data, acsLoad1);
		return data;
	}
	u16 ld2(u16 adr) {
		u16 data = (uint16_t &)m[adr];
		INS8070_TRACE_LOG(adr, data, acsLoad2);
		return data;
	}
	void st1(u16 adr, u8 data) {
		static int lba, snd;
		switch (adr) {
			case 0xfe00: lba = data; break;
			case 0xfe01: lba |= data << 8; break;
			case 0xfe02: lba |= data << 16; file_seek(lba); break;
			case 0xfe04: snd = data; break;
			case 0xfe05: snd |= data << 8; sound_put(snd); break;
			default: m[adr] = data; break;
		}
		INS8070_TRACE_LOG(adr, data, acsStore1);
	}
	void st2(u16 adr, u16 data) {
		(uint16_t &)m[adr] = data;
		INS8070_TRACE_LOG(adr, data, acsStore2);
	}
	//
	u8 *m;
	u16 p[4];
	u16 eac, t;
	u8 s;
	bool irq, halted;
	//
	void br(u8 op, u8 cond = true) {
		if (cond) { s8 o = imm1(); p[0] = p[op & 3] + o; }
		else p[0]++;
	}
	u16 ea(u8 op);
	void add1(u8 v);
	void add2(u16 v);
	void sub1(u8 v);
	void sub2(u16 v);
	int ssm(u8 index);
#if INS8070_TRACE
	static constexpr int TRACEMAX = 10000;
	static constexpr int ACSMAX = 2;
	enum {
		acsLoad1 = 1, acsLoad2, acsStore1, acsStore2
	};
	struct Acs {
		u16 adr, data;
		u8 type;
	};
	struct TraceBuffer {
		u16 pc;
		u8 op[3];
		u16 p[4];
		u16 eac, t;
		u8 s, index, opn;
		Acs acs[ACSMAX];
	};
	TraceBuffer tracebuf[TRACEMAX];
	TraceBuffer *tracep;
public:
	void StopTrace();
#endif
};
