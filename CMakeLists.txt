cmake_minimum_required(VERSION 3.10)
project(6502_emu)

set(CMAKE_C_STANDARD 11)

set(MAPPER_SOURCES
    cartridge.c
    mapper.c
    mapper0.c
)

add_executable(6502_emu
    main.c
    cpu.c
    bus.c
    memory.c
    ${MAPPER_SOURCES}
)

add_executable(6502_tests
    tests.c
    cpu.c
    bus.c
    memory.c
    ${MAPPER_SOURCES}
)
