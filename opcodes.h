#ifndef OPCODES_H
#define OPCODES_H

#define OPC_LDA_IM 0xA9
#define OPC_LDA_ZP 0xA5
#define OPC_LDA_ZPX 0xB5
#define OPC_LDA_ABS 0xAD
#define OPC_LDA_ABSX 0xBD
#define OPC_LDA_ABSY 0xB9
#define OPC_LDA_INDX 0xA1
#define OPC_LDA_INDY 0xB1

#define OPC_STA_ZP 0x85
#define OPC_STA_ZPX 0x95
#define OPC_STA_ABS 0x8D
#define OPC_STA_ABSX 0x9D
#define OPC_STA_ABSY 0x99
#define OPC_STA_INDX 0x81
#define OPC_STA_INDY 0x91

#define OPC_ADC_IM 0x69 // NICE
#define OPC_ADC_ZP 0x65
#define OPC_ADC_ZPX 0x75
#define OPC_ADC_ABS 0x6D
#define OPC_ADC_ABSX 0x7D
#define OPC_ADC_ABSY 0x79
#define OPC_ADC_INDX 0x61
#define OPC_ADC_INDY 0x71

#endif
