add_library(kuzu_table_call
        OBJECT
        checkpoint.cpp
        current_setting.cpp
        db_version.cpp
        khop.cpp
        khop_rc.cpp
        show_attached_databases.cpp
        show_connection.cpp
        show_sequences.cpp
        show_tables.cpp
        sssp.cpp
        storage_info.cpp
        table_info.cpp
        basic.cpp
        )

set(ALL_OBJECT_FILES
        ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:kuzu_table_call>
        PARENT_SCOPE)
