#include <iostream>
#include "host_test/host_test.h"

namespace host_test {

int run_all_tests()
{
	int failed_tests = 0;
	for (std::vector<TestEntry>::const_iterator it = registry().begin(); it != registry().end(); ++it) {
		int failures = 0;
		current_test_failures() = &failures;
		try {
			it->fn();
		} catch (const TestFailure& failure) {
			std::cerr << it->suite << "." << it->name << ": " << failure.what() << std::endl;
		} catch (const std::exception& failure) {
			std::cerr << it->suite << "." << it->name << ": unexpected exception: " << failure.what() << std::endl;
			failures++;
			total_failures()++;
		} catch (...) {
			std::cerr << it->suite << "." << it->name << ": unknown exception" << std::endl;
			failures++;
			total_failures()++;
		}
		current_test_failures() = 0;
		if (failures != 0) {
			failed_tests++;
		}
	}
	if (failed_tests != 0) {
		std::cerr << failed_tests << " test(s) failed" << std::endl;
		return 1;
	}
	std::cout << registry().size() << " test(s) passed" << std::endl;
	return 0;
}

} // namespace host_test

int main(int argc, char **argv)
{
	::host_test::init(&argc, argv);
	return ::host_test::run_all_tests();
}