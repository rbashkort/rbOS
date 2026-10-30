#include <algorithm>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    std::vector<int> v{3, 1, 2};
    std::sort(v.begin(), v.end());

    int x = 0;
    std::thread t([&] { x = 42; });
    t.join();

    std::cout << "CXX-OK " << v[0] << v[1] << v[2] << " x=" << x << std::endl;
    return (v[0] == 1 && v[1] == 2 && v[2] == 3 && x == 42) ? 0 : 1;
}
