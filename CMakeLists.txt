cmake_minimum_required(VERSION 3.10.0)
project(bstunnel VERSION 0.1.0 LANGUAGES C)

add_executable(client client.c)
add_executable(server server.c)