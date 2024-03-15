add_library(
  booster_source
  OBJECT
  booster_native.cpp
  )

set(ALL_OBJECT_FILES
    ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:booster_source>
    PARENT_SCOPE)
