find_package(JNI)
include_directories(SYSTEM ${JNI_INCLUDE_DIRS})

include_directories(include)
include_directories(../src/include)


add_subdirectory(src)

add_library(stellar_booster SHARED ${ALL_OBJECT_FILES})
target_link_libraries(stellar_booster PRIVATE kuzu)
