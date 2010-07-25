{
	## Mark end of timing section
	if ($0 == "Performance Summary:") {
		timing_section = 0;
	}


	## Check timing constraint
	if (timing_section == 1) {
		if ($0 != "") {
			if (match($NF,"Met")) {
				if (match($(NF-1),"Not")) {
					timing_constraints[$1] = 0;
				} else {
					timing_constraints[$1] = 1;
				}
			} else {
				timing_constraints[$1] = 2;
			}
		}
	}

	## Mark start of timing section
	if ($0 == "Timing Constraint Summary:") {
		timing_section = 1;
	}
}
END{
	nr_constraints = 0;
	nr_passed_constraints = 0;
	nr_ignored_constraints = 0;

	## Find out the number of constraints and the met constraints
	for (x in timing_constraints) {
		if (timing_constraints[x] == 2) {
			nr_ignored_constraints++;
		} else {
			nr_constraints++;
		}
		if (timing_constraints[x] == 1) {
			nr_passed_constraints++;
		}
	}

	## Warn if there were no timing constraints defined at all
	if (nr_constraints == 0) {
		printf("Warning! No timing constraints were found\n") | "cat 1>&2"
	}

	## Check if all constraints were met
	if (nr_constraints == nr_passed_constraints) {
		printf("All constraints were met\n");
	} else {
		printf("Timing constraints failed\n");
	}

	## Warn if one or more constraints were ignored
	if (nr_ignored_constraints > 0) {
		printf("Warning! One or more timing constraints were ignored.\n") | "cat 1>&2";
	}
}
