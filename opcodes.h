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

#define OPC_CLC_IM      0x18
#define OPC_CLD_IM      0xD8
#define OPC_CLI_IM      0x58
#define OPC_CLV_IM      0xB8

#define OPC_CMP_IM      0xC9
#define OPC_CMP_ZP      0xC5
#define OPC_CMP_ZPX     0xD5
#define OPC_CMP_ABS     0xCD
#define OPC_CMP_ABSX    0xDD
#define OPC_CMP_ABSY    0xD9
#define OPC_CMP_INDX    0xC1
#define OPC_CMP_INDY    0xD1

#endif
