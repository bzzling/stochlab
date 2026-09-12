#include <exception>
#include <iostream>
#include <string>
namespace stochlab {
void test_processes();
void validate_processes();
void test_exchange();
} // namespace stochlab
int main(int argc, char **argv) {
  try {
    if (argc > 1 && std::string(argv[1]) == "statistics")
      stochlab::validate_processes();
    else {
      stochlab::test_processes();
      stochlab::test_exchange();
    }
    std::cout << "PASS / all requested checks\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL / " << e.what() << '\n';
    return 1;
  }
}
