#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <stdexcept>
#include <cassert>
#include <queue>
#include <set>
#include <algorithm>

template <typename... Args>
class MutableTuple {
private:
    std::tuple<Args...> data;

public:
    MutableTuple(Args... args) : data(std::make_tuple(args...)) {}

    template <size_t Index>
    auto& get() { return std::get<Index>(data); }

    template <size_t Index, typename T>
    void set(T&& value) { std::get<Index>(data) = std::forward<T>(value); }
};

int main(){
    MutableTuple<int,int,int>a(0,1,3);
    std::cout<<a.get<1>();

    std::vector<MutableTuple<int,int,int>>b;
    b.push_back(a);
    std::cout<<b[0].get<1>();
}