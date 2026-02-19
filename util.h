#include "types.h"

Byte read_bit(Byte value, Byte position) {
    Byte result = (value >> position) & 0x01;
    return result;
}