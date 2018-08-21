add_executable(main main.cpp hostcheck.cpp openssl_hostname_validation.cpp scheduled_client.cpp ticker.cpp)
target_include_directories(main PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(main event event_openssl crypto ssl pthread)
cotire(main)
