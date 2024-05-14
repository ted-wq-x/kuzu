add_library(kuzu_function_algorithm
        OBJECT
        demo_algorithm.cpp
        page_rank.cpp
        shortest_path.cpp
        variable_length_path.cpp
        wekaly_connected_component.cpp)

set(ALL_OBJECT_FILES
        ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:kuzu_function_algorithm>
        PARENT_SCOPE)
