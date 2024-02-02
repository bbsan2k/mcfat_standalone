cmake_minimum_required(VERSION 3.19)


project(mcfat
    LANGUAGES
    C CXX
    VERSION
    0.9
)

add_subdirectory(lib)
add_subdirectory(testapp)