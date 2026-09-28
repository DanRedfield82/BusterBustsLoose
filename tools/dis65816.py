#!/usr/bin/env python3
"""Minimal 65816 disassembler for the LoROM Buster Busts Loose ROM (dev tool).
usage: dis65816.py <rom> <start-snes-addr-hex> [count=40] [m=1] [x=1]
REP/SEP update the m/x flag assumptions as disassembly proceeds."""
import sys

OPS = {}
def op(code, name, mode):
    OPS[code] = (name, mode)

table = """
00 BRK imm8|01 ORA dpxi|02 COP imm8|03 ORA sr|04 TSB dp|05 ORA dp|06 ASL dp|07 ORA dpil|08 PHP imp|09 ORA immm|0A ASL acc|0B PHD imp|0C TSB abs|0D ORA abs|0E ASL abs|0F ORA long
10 BPL rel|11 ORA dpiy|12 ORA dpi|13 ORA sriy|14 TRB dp|15 ORA dpx|16 ASL dpx|17 ORA dpily|18 CLC imp|19 ORA absy|1A INC acc|1B TCS imp|1C TRB abs|1D ORA absx|1E ASL absx|1F ORA longx
20 JSR abs|21 AND dpxi|22 JSL long|23 AND sr|24 BIT dp|25 AND dp|26 ROL dp|27 AND dpil|28 PLP imp|29 AND immm|2A ROL acc|2B PLD imp|2C BIT abs|2D AND abs|2E ROL abs|2F AND long
30 BMI rel|31 AND dpiy|32 AND dpi|33 AND sriy|34 BIT dpx|35 AND dpx|36 ROL dpx|37 AND dpily|38 SEC imp|39 AND absy|3A DEC acc|3B TSC imp|3C BIT absx|3D AND absx|3E ROL absx|3F AND longx
40 RTI imp|41 EOR dpxi|42 WDM imm8|43 EOR sr|44 MVP mv|45 EOR dp|46 LSR dp|47 EOR dpil|48 PHA imp|49 EOR immm|4A LSR acc|4B PHK imp|4C JMP abs|4D EOR abs|4E LSR abs|4F EOR long
50 BVC rel|51 EOR dpiy|52 EOR dpi|53 EOR sriy|54 MVN mv|55 EOR dpx|56 LSR dpx|57 EOR dpily|58 CLI imp|59 EOR absy|5A PHY imp|5B TCD imp|5C JML long|5D EOR absx|5E LSR absx|5F EOR longx
60 RTS imp|61 ADC dpxi|62 PER rell|63 ADC sr|64 STZ dp|65 ADC dp|66 ROR dp|67 ADC dpil|68 PLA imp|69 ADC immm|6A ROR acc|6B RTL imp|6C JMP absi|6D ADC abs|6E ROR abs|6F ADC long
70 BVS rel|71 ADC dpiy|72 ADC dpi|73 ADC sriy|74 STZ dpx|75 ADC dpx|76 ROR dpx|77 ADC dpily|78 SEI imp|79 ADC absy|7A PLY imp|7B TDC imp|7C JMP absxi|7D ADC absx|7E ROR absx|7F ADC longx
80 BRA rel|81 STA dpxi|82 BRL rell|83 STA sr|84 STY dp|85 STA dp|86 STX dp|87 STA dpil|88 DEY imp|89 BIT immm|8A TXA imp|8B PHB imp|8C STY abs|8D STA abs|8E STX abs|8F STA long
90 BCC rel|91 STA dpiy|92 STA dpi|93 STA sriy|94 STY dpx|95 STA dpx|96 STX dpy|97 STA dpily|98 TYA imp|99 STA absy|9A TXS imp|9B TXY imp|9C STZ abs|9D STA absx|9E STZ absx|9F STA longx
A0 LDY immx|A1 LDA dpxi|A2 LDX immx|A3 LDA sr|A4 LDY dp|A5 LDA dp|A6 LDX dp|A7 LDA dpil|A8 TAY imp|A9 LDA immm|AA TAX imp|AB PLB imp|AC LDY abs|AD LDA abs|AE LDX abs|AF LDA long
B0 BCS rel|B1 LDA dpiy|B2 LDA dpi|B3 LDA sriy|B4 LDY dpx|B5 LDA dpx|B6 LDX dpy|B7 LDA dpily|B8 CLV imp|B9 LDA absy|BA TSX imp|BB TYX imp|BC LDY absx|BD LDA absx|BE LDX absy|BF LDA longx
C0 CPY immx|C1 CMP dpxi|C2 REP imm8|C3 CMP sr|C4 CPY dp|C5 CMP dp|C6 DEC dp|C7 CMP dpil|C8 INY imp|C9 CMP immm|CA DEX imp|CB WAI imp|CC CPY abs|CD CMP abs|CE DEC abs|CF CMP long
D0 BNE rel|D1 CMP dpiy|D2 CMP dpi|D3 CMP sriy|D4 PEI dpi|D5 CMP dpx|D6 DEC dpx|D7 CMP dpily|D8 CLD imp|D9 CMP absy|DA PHX imp|DB STP imp|DC JML absil|DD CMP absx|DE DEC absx|DF CMP longx
E0 CPX immx|E1 SBC dpxi|E2 SEP imm8|E3 SBC sr|E4 CPX dp|E5 SBC dp|E6 INC dp|E7 SBC dpil|E8 INX imp|E9 SBC immm|EA NOP imp|EB XBA imp|EC CPX abs|ED SBC abs|EE INC abs|EF SBC long
F0 BEQ rel|F1 SBC dpiy|F2 SBC dpi|F3 SBC sriy|F4 PEA abs|F5 SBC dpx|F6 INC dpx|F7 SBC dpily|F8 SED imp|F9 SBC absy|FA PLX imp|FB XCE imp|FC JSR absxi|FD SBC absx|FE INC absx|FF SBC longx
"""
for line in table.strip().split("\n"):
    for ent in line.split("|"):
        c, n, m = ent.split()
        op(int(c, 16), n, m)

def off(a):
    return (((a >> 16) & 0x7f) << 15) | (a & 0x7fff)

def dis(rom, start, count, m=1, x=1, out=print):
    pc = start
    for _ in range(count):
        o = off(pc)
        b = rom[o]
        name, mode = OPS[b]
        size = 1
        arg = ""
        def rd(n):
            return int.from_bytes(rom[o+1:o+1+n], "little")
        if mode == "imp": pass
        elif mode == "acc": arg = "A"
        elif mode == "imm8": size = 2; arg = "#$%02X" % rd(1)
        elif mode == "immm":
            size = 2 if m else 3; arg = ("#$%02X" % rd(1)) if m else ("#$%04X" % rd(2))
        elif mode == "immx":
            size = 2 if x else 3; arg = ("#$%02X" % rd(1)) if x else ("#$%04X" % rd(2))
        elif mode in ("dp", "dpx", "dpy", "dpi", "dpxi", "dpiy", "dpil", "dpily", "sr", "sriy"):
            size = 2; v = rd(1)
            arg = {"dp": "$%02X", "dpx": "$%02X,X", "dpy": "$%02X,Y", "dpi": "($%02X)", "dpxi": "($%02X,X)",
                   "dpiy": "($%02X),Y", "dpil": "[$%02X]", "dpily": "[$%02X],Y", "sr": "$%02X,S", "sriy": "($%02X,S),Y"}[mode] % v
        elif mode in ("abs", "absx", "absy", "absi", "absxi", "absil"):
            size = 3; v = rd(2)
            arg = {"abs": "$%04X", "absx": "$%04X,X", "absy": "$%04X,Y", "absi": "($%04X)", "absxi": "($%04X,X)", "absil": "[$%04X]"}[mode] % v
        elif mode in ("long", "longx"):
            size = 4; v = rd(3); arg = ("$%06X" if mode == "long" else "$%06X,X") % v
        elif mode == "rel":
            size = 2; v = rd(1); v = v - 256 if v >= 128 else v
            arg = "$%04X" % ((pc + 2 + v) & 0xffff)
        elif mode == "rell":
            size = 3; v = rd(2); v = v - 65536 if v >= 32768 else v
            arg = "$%04X" % ((pc + 3 + v) & 0xffff)
        elif mode == "mv":
            size = 3; arg = "$%02X,$%02X" % (rom[o+2], rom[o+1])
        raw = " ".join("%02X" % rom[o+i] for i in range(size))
        out("%02X:%04X  %-12s %s %s" % (pc >> 16, pc & 0xffff, raw, name, arg))
        if name == "REP":
            v = rom[o+1]
            if v & 0x20: m = 0
            if v & 0x10: x = 0
        if name == "SEP":
            v = rom[o+1]
            if v & 0x20: m = 1
            if v & 0x10: x = 1
        pc = (pc & 0xff0000) | ((pc + size) & 0xffff)
    return pc

if __name__ == "__main__":
    rom = open(sys.argv[1], "rb").read()
    start = int(sys.argv[2], 16)
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    m = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    x = int(sys.argv[5]) if len(sys.argv) > 5 else 1
    dis(rom, start, count, m, x)
