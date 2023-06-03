with open("d", "r") as f:
    check = 0
    for line in f:
        if check == 1:
            if "addik" not in line:
                print (checkline, line.strip())
            check = 0
        if ">:" in line:
            check = 1
            checkline = line.strip()


