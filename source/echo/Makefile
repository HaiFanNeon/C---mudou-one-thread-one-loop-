.PHONY: ALL clean

CXX = g++    
CXXFLAGS = -std=c++11 -lpthread -g    

main: main.cc
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f main
