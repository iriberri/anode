


set(RUNTIME_SOURCE_FILES memory.cpp ${ANODE_INCLUDE_DIR}/runtime/builtins.h builtins.cpp)

add_library(anode-runtime ${RUNTIME_SOURCE_FILES})


