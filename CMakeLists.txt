cmake_minimum_required(VERSION 3.10)
project(6502_emu)

set(CMAKE_C_STANDARD 11)

set(MAPPER_SOURCES
    cartridge.c
    mapper.c
    mapper0.c
)

set(PPU_SOURCES ppu.c)

find_package(SDL2 REQUIRED)

add_executable(6502_emu
    main.c cpu.c bus.c memory.c
    ${MAPPER_SOURCES} ${PPU_SOURCES}
)
target_include_directories(6502_emu PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(6502_emu PRIVATE ${SDL2_LIBRARIES})

add_executable(6502_tests
    tests.c cpu.c bus.c memory.c
    ${MAPPER_SOURCES} ${PPU_SOURCES}
)
# 6502_tests does NOT link SDL2
