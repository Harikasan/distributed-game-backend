cmake_minimum_required(VERSION 3.16)
project(match_service CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQXX REQUIRED libpqxx)

set(PROTO_DIR ${CMAKE_SOURCE_DIR}/../proto)
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(MATCH_PROTO ${PROTO_DIR}/match.proto)
set(PLAYER_PROTO ${PROTO_DIR}/player.proto)

add_custom_command(
  OUTPUT
    ${GENERATED_DIR}/match.pb.cc ${GENERATED_DIR}/match.pb.h
    ${GENERATED_DIR}/match.grpc.pb.cc ${GENERATED_DIR}/match.grpc.pb.h
    ${GENERATED_DIR}/player.pb.cc ${GENERATED_DIR}/player.pb.h
    ${GENERATED_DIR}/player.grpc.pb.cc ${GENERATED_DIR}/player.grpc.pb.h
  COMMAND protoc
    --proto_path=${PROTO_DIR}
    --cpp_out=${GENERATED_DIR}
    --grpc_out=${GENERATED_DIR}
    --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
    ${MATCH_PROTO} ${PLAYER_PROTO}
  DEPENDS ${MATCH_PROTO} ${PLAYER_PROTO}
)

add_executable(match_service
  src/main.cpp
  src/match_service_impl.cpp
  src/db_pool.cpp
  src/player_client.cpp
  ${GENERATED_DIR}/match.pb.cc
  ${GENERATED_DIR}/match.grpc.pb.cc
  ${GENERATED_DIR}/player.pb.cc
  ${GENERATED_DIR}/player.grpc.pb.cc
)

target_include_directories(match_service PRIVATE
  ${GENERATED_DIR}
  ${LIBPQXX_INCLUDE_DIRS}
  src
)

target_link_libraries(match_service
  gRPC::grpc++
  gRPC::grpc++_reflection
  protobuf::libprotobuf
  ${LIBPQXX_LIBRARIES}
  pq
  pthread
)
