add_library(kuzu_storage_buffer_manager
        OBJECT
        vm_region.cpp
        buffer_manager.cpp
        memory_manager.cpp
        spiller.cpp)

set(ALL_OBJECT_FILES
        ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:kuzu_storage_buffer_manager>
        PARENT_SCOPE)
