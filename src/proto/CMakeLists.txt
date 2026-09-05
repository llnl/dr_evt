# Create the dr_evt_proto library
if (DR_EVT_HAS_PROTOBUF)
    enable_language(C)

    set_full_path(PROTO_INPUTS
        dr_evt_params.proto
    )

    foreach (proto IN LISTS PROTO_INPUTS)
        get_filename_component(name "${proto}" NAME_WE)
        list(APPEND PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.cc")
        list(APPEND PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.h")
        #list(APPEND PROTO_PY "${CMAKE_CURRENT_BINARY_DIR}/proto/${name}_pb2.py")
    endforeach ()

    add_custom_command(
        COMMAND protobuf::protoc
        --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
        --python_out=${CMAKE_CURRENT_BINARY_DIR}
        --proto_path=${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/cmake/protobuf
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
        ${PROTO_INPUTS}
        OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${PROTO_PY}
        DEPENDS ${PROTO_INPUTS} protobuf::protoc
        COMMENT "Running protoc on DR_EVT protobuf sources."
        COMMAND_EXPAND_LISTS
        VERBATIM)

    add_custom_target(dr_evt_proto_genSrc
        DEPENDS ${PROTO_SRCS} ${PROTO_HDRS})

    add_library(dr_evt_proto ${PROTO_SRCS} ${PROTO_HDRS})
    target_link_libraries(dr_evt_proto PUBLIC protobuf::libprotobuf)
    target_include_directories(dr_evt_proto SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
    target_include_directories(dr_evt_proto PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

    add_dependencies(dr_evt_proto dr_evt_proto_genSrc)

    # Install the library
    install(TARGETS dr_evt_proto
        EXPORT DR_EVT_Targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/proto
    )

    # Install the newly built headers
    install(FILES ${PROTO_HDRS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/proto)

    # Install the Python module into the site-packages directory
    if (PROTO_PY)
        install(FILES ${PROTO_PY} DESTINATION
                ${PYTHON_INSTALL_PREFIX}/${CMAKE_INSTALL_PYTHONDIR})
    endif (PROTO_PY)


    # Add the header files for this directory
    set_full_path(THIS_DIR_HEADERS
        utils.hpp
        dr_evt_params.hpp
    )

    # Add the source files for this directory
    set_full_path(THIS_DIR_SOURCES
        utils.cpp
        dr_evt_params.cpp
    )

    # Add the subdirectories
    #add_subdirectory(subdir)
endif (DR_EVT_HAS_PROTOBUF)


# gRPC service (client/server): separate from the dr_evt_proto library
# above, since dr_evt_params.proto (a plain config message) and
# dr_evt_service.proto (an actual gRPC service definition) have
# different codegen needs - the service needs grpc_cpp_plugin's
# .grpc.pb.cc/.h in addition to protoc's own .pb.cc/.h.
if (DR_EVT_HAS_GRPC)
    set_full_path(GRPC_PROTO_INPUTS
        dr_evt_service.proto
    )

    set(SERVICE_PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/dr_evt_service.pb.cc")
    set(SERVICE_PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/dr_evt_service.pb.h")
    set(SERVICE_GRPC_SRCS "${CMAKE_CURRENT_BINARY_DIR}/dr_evt_service.grpc.pb.cc")
    set(SERVICE_GRPC_HDRS "${CMAKE_CURRENT_BINARY_DIR}/dr_evt_service.grpc.pb.h")

    add_custom_command(
        OUTPUT ${SERVICE_PROTO_SRCS} ${SERVICE_PROTO_HDRS}
               ${SERVICE_GRPC_SRCS} ${SERVICE_GRPC_HDRS}
        COMMAND protobuf::protoc
        ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
             --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
             --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
             --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
             ${GRPC_PROTO_INPUTS}
        DEPENDS ${GRPC_PROTO_INPUTS} protobuf::protoc gRPC::grpc_cpp_plugin
        COMMENT "Running protoc + grpc_cpp_plugin on dr_evt_service.proto."
        VERBATIM)

    add_custom_target(dr_evt_service_proto_genSrc
        DEPENDS ${SERVICE_PROTO_SRCS} ${SERVICE_PROTO_HDRS}
                ${SERVICE_GRPC_SRCS} ${SERVICE_GRPC_HDRS})

    add_library(dr_evt_service_proto
        ${SERVICE_PROTO_SRCS} ${SERVICE_PROTO_HDRS}
        ${SERVICE_GRPC_SRCS} ${SERVICE_GRPC_HDRS})
    target_link_libraries(dr_evt_service_proto PUBLIC
        protobuf::libprotobuf gRPC::grpc++)
    target_include_directories(dr_evt_service_proto SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

    add_dependencies(dr_evt_service_proto dr_evt_service_proto_genSrc)

    # Install the library - needed by dr_evt_server, dr_evt_client, and test executables at runtime
    install(TARGETS dr_evt_service_proto
        EXPORT DR_EVTTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif (DR_EVT_HAS_GRPC)


# Propagate the files up the tree (only if protobuf is enabled)
if (DR_EVT_HAS_PROTOBUF)
    set(DR_EVT_HEADERS "${DR_EVT_HEADERS}" "${THIS_DIR_HEADERS}" PARENT_SCOPE)
    set(DR_EVT_SOURCES "${DR_EVT_SOURCES}" "${THIS_DIR_SOURCES}" PARENT_SCOPE)
endif()
