add_library(kuzu_json_table_functions
        OBJECT
        json_scan.cpp)

set(JSON_EXTENSION_OBJECT_FILES
        ${JSON_EXTENSION_OBJECT_FILES} $<TARGET_OBJECTS:kuzu_json_table_functions>
        PARENT_SCOPE)
