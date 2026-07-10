project(cryfs-bench)

add_executable(${PROJECT_NAME}
  crypto_bench.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE blockstore cpp-utils)

target_enable_style_warnings(${PROJECT_NAME})
target_activate_cpp17(${PROJECT_NAME})

add_executable(cryfs-kdf-bench
  kdf_bench.cpp
)
target_include_directories(cryfs-kdf-bench PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cryfs-kdf-bench PRIVATE cpp-utils)
target_enable_style_warnings(cryfs-kdf-bench)
target_activate_cpp17(cryfs-kdf-bench)
