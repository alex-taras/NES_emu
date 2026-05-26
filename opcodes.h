#ifndef OPCODES_H
#define OPCODES_H

#define OPC_ADC_IM      0x69 // NICE
#define OPC_ADC_ZP      0x65
#define OPC_ADC_ZPX     0x75
#define OPC_ADC_ABS     0x6D
#define OPC_ADC_ABSX    0x7D
#define OPC_ADC_ABSY    0x79
#define OPC_ADC_INDX    0x61
#define OPC_ADC_INDY    0x71

#define OPC_AND_IM      0x29
#define OPC_AND_ZP      0x25
#define OPC_AND_ZPX     0x35
#define OPC_AND_ABS     0x2D
#define OPC_AND_ABSX    0x3D
#define OPC_AND_ABSY    0x39
#define OPC_AND_INDX    0x21
#define OPC_AND_INDY    0x31

#define OPC_ASL_ACC     0x0A
#define OPC_ASL_ZP      0x1A
#define OPC_ASL_ZPX     0x16
#define OPC_ASL_ABS     0x0E
#define OPC_ASL_ABSX    0x1E

#define OPC_LDA_IM      0xA9
#define OPC_LDA_ZP      0xA5
#define OPC_LDA_ZPX     0xB5
#define OPC_LDA_ABS     0xAD
#define OPC_LDA_ABSX    0xBD
#define OPC_LDA_ABSY    0xB9
#define OPC_LDA_INDX    0xA1
#define OPC_LDA_INDY    0xB1

#define OPC_STA_ZP      0x85
#define OPC_STA_ZPX     0x95
#define OPC_STA_ABS     0x8D
#define OPC_STA_ABSX    0x9D
#define OPC_STA_ABSY    0x99
#define OPC_STA_INDX    0x81
#define OPC_STA_INDY    0x91

#define OPC_BCC_REL     0x90
#define OPC_BCS_REL     0xB0
#define OPC_BEQ_REL     0xF0
#define OPC_BMI_REL     0x30
#define OPC_BNE_REL     0xD0
#define OPC_BPL_REL     0x10
#define OPC_BVC_REL     0x50
#define OPC_BVS_REL     0x70

#define OPC_BIT_ZP      0x24
#define OPC_BIT_ABS     0x2C

#define OPC_BRK_IMP     0x00

#define OPC_CLC_IMP     0x18
#define OPC_CLD_IMP     0xD8
#define OPC_CLI_IMP     0x58
#define OPC_CLV_IMP     0xB8

#define OPC_CMP_IM      0xC9
#define OPC_CMP_ZP      0xC5
#define OPC_CMP_ZPX     0xD5
#define OPC_CMP_ABS     0xCD
#define OPC_CMP_ABSX    0xDD
#define OPC_CMP_ABSY    0xD9
#define OPC_CMP_INDX    0xC1
#define OPC_CMP_INDY    0xD1

#define OPC_CPX_IM      0xE0
#define OPC_CPX_ZP      0xE4
#define OPC_CPX_ABS     0xEC

#define OPC_CPY_IM      0xC0
#define OPC_CPY_ZP      0xC4
#define OPC_CPY_ABS     0xCC

#define OPC_DEC_ZP      0xC6
#define OPC_DEC_ZPX     0xD6
#define OPC_DEC_ABS     0xCE
#define OPC_DEC_ABSX    0xDE

#define OPC_DEX_IMP     0xCA
#define OPC_DEY_IMP     0x88

/* SEC, SED, SEI */
#define OPC_SEC_IMP     0x38
#define OPC_SED_IMP     0xF8
#define OPC_SEI_IMP     0x78

/* NOP */
#define OPC_NOP_IMP     0xEA

/* Register transfers */
#define OPC_TAX_IMP     0xAA
#define OPC_TAY_IMP     0xA8
#define OPC_TXA_IMP     0x8A
#define OPC_TYA_IMP     0x98
#define OPC_TSX_IMP     0xBA
#define OPC_TXS_IMP     0x9A

/* INC */
#define OPC_INC_ZP      0xE6
#define OPC_INC_ZPX     0xF6
#define OPC_INC_ABS     0xEE
#define OPC_INC_ABSX    0xFE

/* INX, INY */
#define OPC_INX_IMP     0xE8
#define OPC_INY_IMP     0xC8

/* EOR */
#define OPC_EOR_IM      0x49
#define OPC_EOR_ZP      0x45
#define OPC_EOR_ZPX     0x55
#define OPC_EOR_ABS     0x4D
#define OPC_EOR_ABSX    0x5D
#define OPC_EOR_ABSY    0x59
#define OPC_EOR_INDX    0x41
#define OPC_EOR_INDY    0x51

/* ORA */
#define OPC_ORA_IM      0x09
#define OPC_ORA_ZP      0x05
#define OPC_ORA_ZPX     0x15
#define OPC_ORA_ABS     0x0D
#define OPC_ORA_ABSX    0x1D
#define OPC_ORA_ABSY    0x19
#define OPC_ORA_INDX    0x01
#define OPC_ORA_INDY    0x11

/* SBC */
#define OPC_SBC_IM      0xE9
#define OPC_SBC_ZP      0xE5
#define OPC_SBC_ZPX     0xF5
#define OPC_SBC_ABS     0xED
#define OPC_SBC_ABSX    0xFD
#define OPC_SBC_ABSY    0xF9
#define OPC_SBC_INDX    0xE1
#define OPC_SBC_INDY    0xF1

/* LDX */
#define OPC_LDX_IM      0xA2
#define OPC_LDX_ZP      0xA6
#define OPC_LDX_ZPY     0xB6
#define OPC_LDX_ABS     0xAE
#define OPC_LDX_ABY     0xBE

/* LDY */
#define OPC_LDY_IM      0xA0
#define OPC_LDY_ZP      0xA4
#define OPC_LDY_ZPX     0xB4
#define OPC_LDY_ABS     0xAC
#define OPC_LDY_ABX     0xBC

/* STX */
#define OPC_STX_ZP      0x86
#define OPC_STX_ZPY     0x96
#define OPC_STX_ABS     0x8E

/* STY */
#define OPC_STY_ZP      0x84
#define OPC_STY_ZPX     0x94
#define OPC_STY_ABS     0x8C

/* LSR */
#define OPC_LSR_ACC     0x4A
#define OPC_LSR_ZP      0x46
#define OPC_LSR_ZPX     0x56
#define OPC_LSR_ABS     0x4E
#define OPC_LSR_ABSX    0x5E

/* ROL */
#define OPC_ROL_ACC     0x2A
#define OPC_ROL_ZP      0x26
#define OPC_ROL_ZPX     0x36
#define OPC_ROL_ABS     0x2E
#define OPC_ROL_ABSX    0x3E

/* ROR */
#define OPC_ROR_ACC     0x6A
#define OPC_ROR_ZP      0x66
#define OPC_ROR_ZPX     0x76
#define OPC_ROR_ABS     0x6E
#define OPC_ROR_ABSX    0x7E

/* Stack ops */
#define OPC_PHA_IMP     0x48
#define OPC_PHP_IMP     0x08
#define OPC_PLA_IMP     0x68
#define OPC_PLP_IMP     0x28

/* Jumps and subroutines */
#define OPC_JMP_ABS     0x4C
#define OPC_JMP_IND     0x6C
#define OPC_JSR_ABS     0x20
#define OPC_RTS_IMP     0x60
#define OPC_RTI_IMP     0x40

#endif
