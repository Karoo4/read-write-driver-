CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2

all: uma trainer memscan

uma: main.o uma.o
	$(CXX) $(CXXFLAGS) -o $@ $^

trainer: example_trainer.o uma.o
	$(CXX) $(CXXFLAGS) -o $@ $^

memscan: memscan.o uma.o
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f uma trainer memscan *.o

.PHONY: all clean
