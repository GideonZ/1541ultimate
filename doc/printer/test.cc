/* Standalone printer main */
/* Rene Garcia */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "mps_printer.h"

int
main (int argc, char *argv[])
{
    MpsPrinter * myprinter = MpsPrinter::getMpsPrinter();
    int i,x;
    char c;

    /* External getopt variables */
    extern char *optarg;
    extern int optind, optopt;

    myprinter->setTopMargin(5*36);
    myprinter->setPrintableHeight(60*36);

    while ((c = getopt(argc, argv, "d:f:c:t:l:sepgj")) != -1)
    {
        switch(c)
        {
            case 'd':   /* Dot size */
                myprinter->setDotSize(atoi(optarg));
                break;

            case 'f':   /* Filename base */
                myprinter->setFilename(optarg);
                break;

            case 'c':   /* Charset */
                myprinter->setCBMCharset(atoi(optarg));
                myprinter->setEpsonCharset(atoi(optarg));
                myprinter->setIBMCharset(atoi(optarg));
                break;

            case 'j':   /* Color printer */
                myprinter->setColorMode(true);
                break;

            case 's':   /* Secondary CBM charset */
                myprinter->setCharsetVariant(1);
                break;

            case 'e':   /* Epson emulation */
                myprinter->setInterpreter(MPS_PRINTER_INTERPRETER_EPSONFX80);
                break;

            case 'p':   /* IBM proprinter emulation */
                myprinter->setInterpreter(MPS_PRINTER_INTERPRETER_IBMPP);
                break;

            case 'g':   /* IBM graphics printer emulation */
                myprinter->setInterpreter(MPS_PRINTER_INTERPRETER_IBMGP);
                break;

            case 't':   /* Top page margin */
                myprinter->setTopMargin(atoi(optarg)*36);
                break;

            case 'l':   /* Printable page height */
                myprinter->setPrintableHeight(atoi(optarg)*36);
                break;

            default:
                fprintf(stderr,
                        "printer v1.0 " __DATE__ "\n\n"
                        "USAGE: %s [-d 0-3] [-f file] [-c n] [-t n] [-l n] [-sepg] file1 [file2 ...]\n"
                        "    -d : dot size 0, 1, 2 or 3\n"
                        "    -f : base filename (default is mps)\n"
                        "    -c : charset num (default is 0)\n"
                        "    -s : commodore secondary charset (upper/lower)\n"
                        "    -j : color printer (default is greyscale)\n"
                        "    -e : Epson FX80/JX80 emulation\n"
                        "    -p : IBM Proprinter emulation\n"
                        "    -g : IBM Graphics printer emulation\n"
                        "    -t : top margin (0-69, default 5)\n"
                        "    -l : page height (1-70, default 60)\n"
                        "default emulation if no option given is Commodore MPS\n",
                        argv[0]);
                return 100;
        }
    }

    if (optind < argc)
    {
        for ( ; optind < argc; optind++)
        {
            int hdl = open(argv[optind], O_RDONLY);
            if (hdl)
            {
                printf("Reading input file %s\n", argv[optind]);
                unsigned char buffer[4096];
                int bytes;

                do
                {
                    bytes = read(hdl,buffer,4096);
                    myprinter->Interpreter(buffer,bytes);
                }
                while (bytes == 4096);
                close (hdl);

                myprinter->FormFeed();
            }
            else
            {
                printf("Can't open input file %s\n", argv[1]);
            }
        }
    }
    else
    {
        unsigned char buffer[4096];
        int bytes;

        printf("Processing standard input\n");

        do
        {
            bytes = fread(buffer, 1, 4096, stdin);
            myprinter->Interpreter(buffer,bytes);
        }
        while (bytes == 4096);

        myprinter->FormFeed();
    }

    return 0;
}

/****************************  END OF FILE  ****************************/
