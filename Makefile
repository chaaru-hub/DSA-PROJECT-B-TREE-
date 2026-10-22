CXX ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra

btree_animation: btree_animation.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

demo: btree_animation
	./btree_animation --demo

clean:
	rm -f btree_animation btree_animation.exe

.PHONY: demo clean
