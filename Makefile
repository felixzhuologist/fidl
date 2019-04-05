main : main.o source_manager.o source_file.o
	g++ main.o source_manager.o source_file.o -o main

main.o : main.cpp source_manager.h
	g++ --std=c++17 -c main.cpp

source_manager.o : source_manager.cpp source_manager.h source_file.h string_view.h
	g++ --std=c++17 -c source_manager.cpp

source_file.o : source_file.cpp source_file.h string_view.h
	g++ --std=c++17 -c source_file.cpp
