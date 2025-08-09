cmake_minimum_required(VERSION 3.31)
project(tokenizer)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(lib)

add_executable(tokenizer src/main.cpp
		src/files/JsonFile.cpp
		src/files/JsonFile.h
		src/files/MetadataFile.cpp
		src/files/MetadataFile.h
		src/files/DataFile.cpp
		src/files/DataFile.h
		src/utils/Multithread.h
		src/tokenizer/TokenGenerator.cpp
		src/tokenizer/TokenGenerator.h
		src/files/TokenFile.cpp
		src/files/TokenFile.h
		src/tokenizer/LomaxDist.cpp
		src/tokenizer/LomaxDist.h
		src/files/CandidatesFile.cpp
		src/files/CandidatesFile.h
		src/config.h
)
target_link_libraries(tokenizer PUBLIC RapidJSON)