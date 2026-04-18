#ifndef TESTS_HOST_TEST_HOST_TEST_H
#define TESTS_HOST_TEST_HOST_TEST_H

#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace host_test {

struct TestEntry {
	const char *suite;
	const char *name;
	void (*fn)();
};

class TestFailure : public std::runtime_error
{
  public:
	explicit TestFailure(const std::string& message) : std::runtime_error(message) { }
};

inline std::vector<TestEntry>& registry()
{
	static std::vector<TestEntry> tests;
	return tests;
}

inline int& total_failures()
{
	static int failures = 0;
	return failures;
}

inline int*& current_test_failures()
{
	static int *failures = 0;
	return failures;
}

inline bool register_test(const char *suite, const char *name, void (*fn)())
{
	registry().push_back({ suite, name, fn });
	return true;
}

inline void record_failure(const std::string& message, bool fatal)
{
	if (current_test_failures()) {
		(*current_test_failures())++;
	}
	total_failures()++;
	if (fatal) {
		throw TestFailure(message);
	}
}

template<typename Left, typename Right>
inline void expect_eq(const Left& left, const Right& right, const char *left_expr, const char *right_expr,
	const char *file, int line, bool fatal)
{
	if (!(left == right)) {
		std::ostringstream stream;
		stream << file << ":" << line << ": expected " << left_expr << " == " << right_expr
		       << ", actual " << left << " vs " << right;
		record_failure(stream.str(), fatal);
	}
}

template<typename Left, typename Right>
inline void expect_ne(const Left& left, const Right& right, const char *left_expr, const char *right_expr,
	const char *file, int line, bool fatal)
{
	if (!(left != right)) {
		std::ostringstream stream;
		stream << file << ":" << line << ": expected " << left_expr << " != " << right_expr;
		record_failure(stream.str(), fatal);
	}
}

inline void expect_true(bool condition, const char *expr, const char *file, int line, bool fatal)
{
	if (!condition) {
		std::ostringstream stream;
		stream << file << ":" << line << ": expected true: " << expr;
		record_failure(stream.str(), fatal);
	}
}

inline void expect_false(bool condition, const char *expr, const char *file, int line, bool fatal)
{
	if (condition) {
		std::ostringstream stream;
		stream << file << ":" << line << ": expected false: " << expr;
		record_failure(stream.str(), fatal);
	}
}

inline int init(int *, char **)
{
	return 0;
}

int run_all_tests();

} // namespace host_test

#define TEST(suite_name, test_name) \
	static void suite_name##_##test_name##_impl(); \
	static bool suite_name##_##test_name##_registered = ::host_test::register_test(#suite_name, #test_name, suite_name##_##test_name##_impl); \
	static void suite_name##_##test_name##_impl()

#define EXPECT_EQ(left, right) ::host_test::expect_eq((left), (right), #left, #right, __FILE__, __LINE__, false)
#define ASSERT_EQ(left, right) ::host_test::expect_eq((left), (right), #left, #right, __FILE__, __LINE__, true)
#define EXPECT_NE(left, right) ::host_test::expect_ne((left), (right), #left, #right, __FILE__, __LINE__, false)
#define ASSERT_TRUE(expr) ::host_test::expect_true((expr), #expr, __FILE__, __LINE__, true)
#define EXPECT_TRUE(expr) ::host_test::expect_true((expr), #expr, __FILE__, __LINE__, false)
#define EXPECT_FALSE(expr) ::host_test::expect_false((expr), #expr, __FILE__, __LINE__, false)

#endif