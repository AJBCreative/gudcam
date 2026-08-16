CXX = g++
CXXFLAGS = -O3 -march=native -ffast-math -fopenmp -pthread -fPIC -std=c++17 -Iinclude -Wall
LDFLAGS = -fopenmp -pthread

SRCS = src/v4l2_engine.cpp src/v4l2_controls.cpp src/gpu_pipeline.cpp src/c_api.cpp
OBJS = $(SRCS:.cpp=.o)

LIB = libgudcam.so
APP = gudcam_app

all: $(LIB) $(APP) tests

$(LIB): $(OBJS)
	$(CXX) -shared -o $@ $(OBJS) $(LDFLAGS)

$(APP): src/main.o $(OBJS)
	$(CXX) -o $@ src/main.o $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

tests: tests/test_lockfree tests/test_v4l2 tests/benchmark_latency

tests/test_lockfree: tests/test_lockfree.cpp src/gpu_pipeline.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

tests/test_v4l2: tests/test_v4l2.cpp $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

tests/benchmark_latency: tests/benchmark_latency.cpp $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJS) src/main.o tests/*.o $(LIB) $(APP) tests/test_lockfree tests/test_v4l2 tests/benchmark_latency

.PHONY: all clean tests
