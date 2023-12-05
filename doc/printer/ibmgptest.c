/* IBM GP emulation test page */
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
#define NLQ_ON              "\x1b" "x1"
#define NLQ_OFF             "\x1b" "x0"
#define BOLD_ON             "\x1b" "E"
#define BOLD_OFF            "\x1b" "F"
#define PICA                "\x12"
#define ELITE               "\x1b" "M"
#define CONDENSE            "\x0f"
#define TABLE2              "\x1b" "6"
#define OVERLINE_ON
#define OVERLINE_OFF

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
    printf(NLQ_ON "\t" BOLD_ON EN_ON UNDERLINE_ON "IBM GP EMULATION PRINT TEST PAGE" BOLD_OFF EN_OFF UNDERLINE_OFF NLQ_OFF "\n\r\n\r\n\r\n\r");

    /* ======== Char tests */
    /* -------- Simple */
    printf( "DRAFT Simple  "
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Over" OVERLINE_OFF
           "\n\r");

    printf( "NLQ Simple    "
           NLQ_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Over" OVERLINE_OFF
           NLQ_OFF "\n\r\n\r");

    /* ------- Double */
    printf( "DRAFT Double  "
           DOUBLE_STRIKE_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Over" OVERLINE_OFF
           DOUBLE_STRIKE_OFF "\n\r");

    printf( "NLQ Double    "
           NLQ_ON
           DOUBLE_STRIKE_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Over" OVERLINE_OFF
           DOUBLE_STRIKE_OFF
           NLQ_OFF"\n\r\n\r");

    /* -------  Large */
    printf( "DRAFT Large   "
           EN_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Over" OVERLINE_OFF
           EN_OFF "\n\r");

    printf( "NLQ Large     "
           NLQ_ON
           EN_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Over" OVERLINE_OFF
           EN_OFF
           NLQ_OFF"\n\r\n\r");

    /* -------  Large Double */
    printf( "DRAFT Lg Db   "
           EN_ON DOUBLE_STRIKE_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Over" OVERLINE_OFF
           DOUBLE_STRIKE_OFF
           EN_OFF "\n\r");

    printf( "NLQ Lg Db     "
           NLQ_ON DOUBLE_STRIKE_ON
           EN_ON
           UNDERLINE_ON "Under,Agqp" UNDERLINE_OFF " "
           BOLD_ON "Bold " BOLD_OFF
           SUPERSCRIPT "Super "
           SUBSCRIPT "Sub " SUSCRIPT_OFF
           OVERLINE_ON "Over" OVERLINE_OFF
           EN_OFF DOUBLE_STRIKE_OFF
           NLQ_OFF"\n\r\n\r\n\r");

    /* =======  Test X spacing */
    printf("PICA"
           PICA POS_C20 PICA "Draft Regular"
           PICA POS_C40 PICA NLQ_ON "Near Letter Quality" NLQ_OFF
           PICA "\n\r");

    printf("ELITE"
           PICA POS_C20 ELITE "Draft Regular"
           PICA POS_C40 ELITE NLQ_ON "Near Letter Quality" NLQ_OFF
           PICA "\n\r");

    printf("CONDENSED"
           PICA POS_C20 CONDENSE "Draft Regular"
           PICA POS_C40 CONDENSE NLQ_ON "Near Letter Quality" NLQ_OFF
           PICA "\n\r");

    /* =======  Test graphic bitmap */
    printf("\n\r\n\r" NLQ_ON TABLE2);

    printf("   0 1 2 3 4 5 6 7 8 9 A B C D E F\n\r\n\r");
    
    printf("\x1b\x44\x03\x05\x07\x09\x0b\x0d\x0f\x11\x13\x15\x17\x19\x1B\x1D\x1F\x21\x23\x01");
    for (i=0; i<16; i++)
    {
        printf( "%c" , '0'+ ((i<10)?i:i+7));
        for(j=i; j<256; j+=16)
        {
            if (((j >= '\x07') && (j <= '\x0F')) ||
                ((j >= '\x11') && (j <= '\x14')) ||
                (j == '\x18') ||
                (j == '\x1b'))
                printf("\t");
            else
                printf("\t%c", j);
        }

        printf("\n\r");
    }
    return 0;
}
