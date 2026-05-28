.PHONY: all test clean configure

BUILD_DIR := build

$(BUILD_DIR)/CMakeCache.txt:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug .

configure: $(BUILD_DIR)/CMakeCache.txt

all: configure
	cmake --build $(BUILD_DIR) --target raylib_server -j4

test: configure
	cmake --build $(BUILD_DIR) --target all -j4
	cd $(BUILD_DIR) && ctest --output-on-failure

clean:
	rm -rf $(BUILD_DIR)
