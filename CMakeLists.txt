cmake_minimum_required(VERSION 3.31)
project(tokenizer)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(lib)

add_executable(tokenizer src/main.cpp
		src/data_set/file/JsonFile.cpp
		src/data_set/file/JsonFile.h
		src/data_set/Entry.h
		src/data_set/file/MetadataFile.cpp
		src/data_set/file/MetadataFile.h
		src/data_set/file/DataFile.cpp
		src/data_set/file/DataFile.h)
target_link_libraries(tokenizer PUBLIC RapidJSON)