cmake_minimum_required(VERSION 3.28)
project(EDAA)

set(CMAKE_CXX_STANDARD 17)

add_executable(EDAA main.cpp
        source/Multigraph.cpp
        source/Edge.cpp
        source/Vertex.cpp
        source/Coordinate.cpp
        source/FileParser.cpp
        header/MutablePriorityQueue.h
        source/FibonacciHeap.cpp
        source/MutablePriorityQueue.cpp
        source/BruteForceQueue.cpp
        source/App.cpp
        source/Quadtree.cpp
        source/VRP.cpp
)
