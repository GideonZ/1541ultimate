/*
 * report.h
 *
 *  Created on: Nov 18, 2016
 *      Author: gideon
 */

#ifndef REPORT_H_
#define REPORT_H_

#include <stdint.h>
#include <stddef.h>
#include "indexed_list.h"
#include "rtc_only.h"

typedef int (*TestFunction_t)(JTAG_Access_t *target, int timeout, char **log);

struct TestDefinition_t
{
	const char *nameOfTest;
	TestFunction_t function;
	int  timeout; // in seconds
	bool breakOnFail;
	bool skipWhenErrors;
	bool logInSummary;
};

typedef enum {
	notRun = 0,
	skipped,
	passed,
	failed
} EState;

const char *stateStrings[] = { "Not run", "Skipped", "OK", "FAILED!" };

class TestResult
{
	EState state;
	char *log;
	int returnValue;
	TestResult(TestResult &copy) { } // copy constructor not allowed
public:
	const TestDefinition_t *test;

	TestResult(TestDefinition_t *def) {
		test = def;
		state = notRun;
		log = NULL;
	}
	~TestResult() {
		if (log) {
			delete log;
		}
	}

	void setResult(char *log, EState state, int retval) {
		this->log = log;
		this->state = state;
		this->returnValue = retval;
	}

	void format() {
		if (state == failed) {
			printf("| %-40s | %-8s | %4d |\n", test->nameOfTest, stateStrings[(int)state], returnValue);
		} else {
			printf("| %-40s | %-8s |      |\n", test->nameOfTest, stateStrings[(int)state]);
		}
/*
		if ((test->logInSummary) && (log)) {
			printf("| Response from DUT:\n%s\n", this->log);
		}
*/
	}
	EState getState() {
		return state;
	}

	char *getLog() {
		return log;
	}
	int getReturnValue() {
		return returnValue;
	}
};

/**
 * This class provides a way to run tests and generate a summary report, logs, etc.
 */
class TestSuite
{
	const char *suiteName;
	char dateTime[40];
	IndexedList <TestDefinition_t *>tests;
	IndexedList <TestResult *> results;
public:
	TestSuite(const char *name, TestDefinition_t tests[]) : tests(32, 0), results(32, 0) {
		for (int i=0; tests[i].function; i++) {
			this->tests.append(&tests[i]);
			this->results.append(new TestResult(&tests[i]));
		}
		suiteName = name;
	}

	void Reset(void) {
		dateTime[0] = 0;
		for (int i=0; i < results.get_elements(); i++) {
			char *log = results[i]->getLog();
			if(log) {
				delete log;
			}
			results[i]->setResult(NULL, notRun, 0);
		}
	}

	void Run(volatile uint32_t *jtag) {
		char *log;
		JTAG_Access_t target;
		target.host = jtag;
		int errors = 0;
		rtc.get_datetime_compact(this->dateTime, 40);
		for (int i=0; i < tests.get_elements(); i++) {
			if ((errors) && (tests[i]->skipWhenErrors)) {
				results[i]->setResult(NULL, skipped, 0);
				continue;
			}
			log = NULL;
			printf("\n*** RUNNING TEST: '%s' ***\n", tests[i]->nameOfTest);
			int result = tests[i]->function(&target, tests[i]->timeout, &log);
			if (!result) {
				results[i]->setResult(log, passed, result);
			} else {
				errors ++;
				results[i]->setResult(log, failed, result);
				if (tests[i]->breakOnFail) {
					break;
				}
			}
		}
	}

	bool Passed(void) {
		bool pass = true;
		for (int i=0; i < results.get_elements(); i++) {
			if (results[i]->getState() != passed) {
				pass = false;
				break;
			}
		}
		return pass;
	}

	void Report(void) {
		printf("\n--------------------------------------------------------------\n");
		printf("| TestSuite %-30s | %-16s|", suiteName, dateTime);
		printf("\n+------------------------------------------------------------+\n");
		for (int i=0; i < results.get_elements(); i++) {
			results[i]->format();
		}
		printf("+------------------------------------------------------------+\n");
		printf("| Final verdict:  %-42s |\n", Passed()?"-PASSED-" : "** FAILED **");
		printf("--------------------------------------------------------------\n");
	}

	const char *getDateTime(void) {
		return dateTime;
	}
};


#endif /* REPORT_H_ */
