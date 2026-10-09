#include <cstddef>
#include <string>

int main() { std::string s = "ok"; return static_cast<int>(s.size()) - 2; }
