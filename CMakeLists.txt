cmake_minimum_required(VERSION 3.31)
project(tokenizer)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(lib)

add_executable(tokenizer src/main.cpp
		src/files/DataFile.cpp
		src/files/DataFile.h
		src/files/JsonFile.cpp
		src/files/JsonFile.h
		src/files/MetadataFile.cpp
		src/files/MetadataFile.h
		src/files/SolutionFile.cpp
		src/files/SolutionFile.h
		src/tokenizer/GetTokens.cpp
		src/tokenizer/GetTokens.h
		src/tokenizer/LomaxDist.cpp
		src/tokenizer/LomaxDist.h
		src/tokenizer/Token.cpp
		src/tokenizer/Token.h
		src/tokenizer/TokenGenerator.cpp
		src/tokenizer/TokenGenerator.h
		src/utils/Multithread.h
		src/utils/Multithread.cpp
		src/config.h
		src/tokenizer/Trie.h
		src/tokenizer/Trie.cpp
)
target_link_libraries(tokenizer PUBLIC RapidJSON)
target_link_libraries(tokenizer PUBLIC utf8cpp)