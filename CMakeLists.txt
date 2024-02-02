cmake_minimum_required(VERSION 3.19)


project(mcfat
    LANGUAGES
    C CXX
    VERSION
    0.9
)

add_library(mcfat STATIC 
            src/main.c
            src/mcsio2.c
            src/ps2mc_fio.c)
target_include_directories(mcfat 
                                PRIVATE 
                                    src/ 
                                PUBLIC 
                                    include/)

