cmake_minimum_required(VERSION 3.16)
project(player_service CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED hiredis)
pkg_check_modules(LIBPQXX REQUIRED libpqxx)

set(PROTO_DIR ${CMAKE_SOURCE_DIR}/../proto)
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(PROTO_FILE ${PROTO_DIR}/player.proto)

add_custom_command(
  OUTPUT
    ${GENERATED_DIR}/player.pb.cc
    ${GENERATED_DIR}/player.pb.h
    ${GENERATED_DIR}/player.grpc.pb.cc
    ${GENERATED_DIR}/player.grpc.pb.h
  COMMAND protoc
    --proto_path=${PROTO_DIR}
    --cpp_out=${GENERATED_DIR}
    --grpc_out=${GENERATED_DIR}
    --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
    ${PROTO_FILE}
  DEPENDS ${PROTO_FILE}
)

add_executable(player_service
  src/main.cpp
  src/player_service_impl.cpp
  src/db_pool.cpp
  src/cache_client.cpp
  ${GENERATED_DIR}/player.pb.cc
  ${GENERATED_DIR}/player.grpc.pb.cc
)

target_include_directories(player_service PRIVATE
  ${GENERATED_DIR}
  ${HIREDIS_INCLUDE_DIRS}
  ${LIBPQXX_INCLUDE_DIRS}
  src
)

target_link_libraries(player_service
  gRPC::grpc++
  gRPC::grpc++_reflection
  protobuf::libprotobuf
  ${HIREDIS_LIBRARIES}
  ${LIBPQXX_LIBRARIES}
  pq
  pthread
)
