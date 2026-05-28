cmake_minimum_required(VERSION 3.10)
project(6502_emu)

set(CMAKE_C_STANDARD 11)

set(MAPPER_SOURCES
    cartridge.c
    mapper.c
    mapper0.c
    mapper1.c
    mapper2.c
)

set(PPU_SOURCES ppu.c)

set(APU_SOURCES apu.c)

find_package(SDL2 REQUIRED)

add_executable(6502_emu
    main.c cpu.c bus.c memory.c controller.c
    ${MAPPER_SOURCES} ${PPU_SOURCES} ${APU_SOURCES}
)
target_include_directories(6502_emu PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(6502_emu PRIVATE ${SDL2_LIBRARIES})

add_executable(6502_tests
    tests.c cpu.c bus.c memory.c controller.c
    ${MAPPER_SOURCES} ${PPU_SOURCES} ${APU_SOURCES}
)
