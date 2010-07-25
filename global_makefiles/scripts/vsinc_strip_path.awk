#
# This scripts reads a sinc file and strips all path information.

BEGIN{
	FS = ",";
	OFS = ",";
}
{
	nrdeps = split($4, deps, " ");
	basename_deps = "";
	for (i = 1; i <= nrdeps; i++) {
		basename_deps = sprintf("%s%s ", basename_deps, basename(deps[i]));
	}

	if ($3 != "") {
		print $1, $2, basename($3), basename_deps;
	}
}

function basename(filename, pos)
{
	pos = split(filename, parts, "/");
	return parts[pos];
}
