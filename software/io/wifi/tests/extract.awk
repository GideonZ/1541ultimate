# extract.awk -v f=<name>: prints the definition of the function <name> (for C++ also
# "Class :: method"), from its signature line to its closing brace, by brace count, from a
# C or C++ file, as it is. The signature line starts in column 0 with a letter and holds
# "<name>(" but is not a declaration (does not end with ';').
!p && /^[A-Za-z_]/ && index($0, f "(") && $0 !~ /;[[:space:]]*$/ { p = 1; depth = 0; seen = 0 }
p {
    print
    line = $0
    opens = gsub(/\{/, "", line)
    closes = gsub(/\}/, "", line)
    depth += opens - closes
    if (opens) seen = 1
    if (seen && depth == 0) p = 0
}
