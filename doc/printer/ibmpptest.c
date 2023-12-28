/* IBM PP emulation test page */
/* Rene Garcia 2016 */

#include <strings.h>
#include <stdio.h>

#define EN_ON               "\x0e"
#define EN_OFF              "\x14"
#define UNDERLINE_ON        "\x1b" "-1"
#define UNDERLINE_OFF       "\x1b" "-0"
#define DOUBLE_STRIKE_ON    "\x1b" "G"
#define DOUBLE_STRIKE_OFF   "\x1b" "H"
#define SUPERSCRIPT         "\x1b" "S0"
#define SUBSCRIPT           "\x1b" "S1"
#define SUSCRIPT_OFF        "\x1b" "T"
#define NLQ_ON
#define NLQ_OFF
#define BOLD_ON             "\x1b" "E"
#define BOLD_OFF            "\x1b" "F"
#define ITALIC_ON
#define ITALIC_OFF
#define PICA                "\x12"
#define ELITE               "\x1b" ":"
#define CONDENSE            "\x0f"
#define TABLE2              "\x1b" "6"
#define OVERLINE_ON         "\x1b" "_1"
#define OVERLINE_OFF        "\x1b" "_0"

#define POS_C20             "\r" "                    "
#define POS_C40             "\r" "                                        "
#define POS_C60             "\r" "                                                            "

#define ONE_ESCAPE          "\x1b" "^"
int main (argc, argv)
int argc;
char *argv[];
{
    int i,j;

    /* ======== Title */
    printf(NLQ_ON "\t" BOLD_ON EN_ON UNDERLINE_ON "IBM PP EMULATION PRINT TEST PAGE" BOLD_OFF EN_OFF UNDERLINE_OFF NLQ_OFF "\n\r\n\r\n\r\n\r");

    /* ======== Char tests */
    /* -------- Simple */
    printf( "DRAFT Simple  "
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Overline" OVERLINE_OFF
           "\n\r");

    printf( "ITALIC Simple "
           ITALIC_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Overline" OVERLINE_OFF
           ITALIC_OFF
           "\n\r");

    printf( "NLQ Simple    "
           NLQ_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Overline" OVERLINE_OFF
           NLQ_OFF "\n\r\n\r");

    /* ------- Double */
    printf( "DRAFT Double  "
           DOUBLE_STRIKE_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Overline" OVERLINE_OFF
           DOUBLE_STRIKE_OFF "\n\r");

    printf( "ITALIC Double "
           ITALIC_ON
           DOUBLE_STRIKE_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Overline" OVERLINE_OFF
           DOUBLE_STRIKE_OFF
           ITALIC_OFF "\n\r");

    printf( "NLQ Double    "
           NLQ_ON
           DOUBLE_STRIKE_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Overline" OVERLINE_OFF
           DOUBLE_STRIKE_OFF
           NLQ_OFF"\n\r\n\r");

    /* -------  Large */
    printf( "DRAFT Large   "
           EN_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Overline" OVERLINE_OFF
           EN_OFF "\n\r");

    printf( "ITALIC Large  "
           EN_ON
           ITALIC_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Overline" OVERLINE_OFF
           EN_OFF
           ITALIC_OFF "\n\r");

    printf( "NLQ Large     "
           NLQ_ON
           EN_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Overline" OVERLINE_OFF
           EN_OFF
           NLQ_OFF"\n\r\n\r");

    /* -------  Large Double */
    printf( "DRAFT Lg Db   "
           EN_ON DOUBLE_STRIKE_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Overline" OVERLINE_OFF
           DOUBLE_STRIKE_OFF
           EN_OFF "\n\r");

    printf( "ITALIC Lg Db  "
           EN_ON DOUBLE_STRIKE_ON
           ITALIC_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Overline" OVERLINE_OFF
           EN_OFF  DOUBLE_STRIKE_OFF
           ITALIC_OFF "\n\r");

    printf( "NLQ Lg Db     "
           NLQ_ON DOUBLE_STRIKE_ON
           EN_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Overline" OVERLINE_OFF
           EN_OFF DOUBLE_STRIKE_OFF
           NLQ_OFF"\n\r\n\r\n\r");

    /* =======  Test X spacing */
    printf("PICA"
           PICA POS_C20 PICA "Draft Regular"
           PICA POS_C40 PICA ITALIC_ON "Draft Italic" ITALIC_OFF
           PICA POS_C60 PICA NLQ_ON "Near Letter Quality" NLQ_OFF
           PICA "\n\r");

    printf("ELITE"
           PICA POS_C20 ELITE "Draft Regular"
           PICA POS_C40 ELITE ITALIC_ON "Draft Italic" ITALIC_OFF
           PICA POS_C60 ELITE NLQ_ON "Near Letter Quality" NLQ_OFF
           PICA "\n\r");

    printf("CONDENSED"
           PICA POS_C20 CONDENSE "Draft Regular"
           PICA POS_C40 CONDENSE ITALIC_ON "Draft Italic" ITALIC_OFF
           PICA POS_C60 CONDENSE NLQ_ON "Near Letter Quality" NLQ_OFF
           PICA "\n\r");

    /* =======  Test graphic bitmap */
    printf("\n\r\n\r" TABLE2);

    printf("   0 1 2 3 4 5 6 7 8 9 A B C D E F\n\r\n\r");

    printf("\x1b\x44\x03\x05\x07\x09\x0b\x0d\x0f\x11\x13\x15\x17\x19\x1B\x1D\x1F\x21\x23\x01");
    for (i=0; i<16; i++)
    {
        printf( NLQ_OFF"%c" NLQ_ON, '0'+ ((i<10)?i:i+7));
        for(j=i; j<256; j+=16)
        {
            printf("\t" ONE_ESCAPE "%c", j);
        }

        printf("\n\r");
    }

    return 0;
}
