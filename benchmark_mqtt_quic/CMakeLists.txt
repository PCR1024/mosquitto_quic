function(add_mosquitto_client target)
    add_executable(${target} ${ARGN})

    target_include_directories(${target} PRIVATE
        "${mosquitto_SOURCE_DIR}"
        "${mosquitto_SOURCE_DIR}/include"
        "${STDBOOL_H_PATH}"
        "${STDINT_H_PATH}"
        "${PTHREAD_INCLUDE_DIR}"
        "${OPENSSL_INCLUDE_DIR}"
    )

    target_link_directories(${target} PRIVATE
        "${mosquitto_BINARY_DIR}/lib"
    )

    if (CJSON_FOUND)
        target_compile_definitions(${target} PRIVATE WITH_CJSON)

        target_include_directories(${target} PRIVATE "${CJSON_INCLUDE_DIRS}")
        target_link_directories(${target} PRIVATE "${CJSON_DIR}")

        target_link_libraries(${target} PRIVATE ${CJSON_LIBRARIES})
    endif()

    if (WITH_STATIC_LIBRARIES)
        target_link_libraries(${target} PRIVATE libmosquitto_static)
    else()
        target_link_libraries(${target} PRIVATE libmosquitto)
    endif()

    install(TARGETS ${target} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endfunction()

add_subdirectory(latency)