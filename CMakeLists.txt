cmake_minimum_required(VERSION 3.16)
project(snippets VERSION 7.2)

find_package(Albert REQUIRED)

albert_plugin(
    INCLUDE
        INTERFACE include
        PRIVATE include/albert/plugin
    QT
        Concurrent Widgets
)
