all: json

json:
	g++ -std=c++14 -g -I../../api -I../../components -I../../io/stream -I../../system json.cpp \
	../../components/mystring.cc ../../system/small_printf.cc ../../api/json.cc \
	-lgtest -lgtest_main -lpthread -o jsonTest && ./jsonTest
