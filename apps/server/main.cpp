#include <iostream>

#include "kv/version.hpp"

int main() { std::cout << "kv-server " << kv::version() << "\n"; }