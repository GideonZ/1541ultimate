# We want the build to always terminate with a useful error if any of the
# primary nets in the bit stream are assigned to i_riscv/* signals. This is
# what the below shell commands accomplish.

all:
	diamondc make.tcl > build.log.new
	@if [ -s build.log.new ]; then cp -a build.log.new build.log; else [ ! -e build.log ] && touch build.log || true; fi
	@cat build.log | grep 'Semantic error in "PROHIBIT PRIMARY NET' || true
	@cat build.log | grep -A 25 "WARNING - par: The following .* data signals have to use primary clock resources" | grep -v '^[^ W]' | grep -v '^$$' || true
	@BAD="$$(cat build.log | grep -A 25 'WARNING - par: The following .* data signals have to use primary clock resources' | grep -v '^[^ W]' | grep '  i_riscv\/.*driver.*' | wc -l)"; echo "BAD='$$BAD'"; if [ "$$BAD" != "0" ]; then echo "ERROR: Found $$BAD i_riscv/* signals assigned to a PRIMARY net which for some reason prevents booting, aborting. Please fix."; false; fi
