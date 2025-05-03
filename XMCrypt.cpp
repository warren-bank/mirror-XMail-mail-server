

#include <stdio.h>
#include <stdlib.h>
#include <string.h>





char           *StrCrypt(char const * pszString, char *pszCrypt)
{

    strcpy(pszCrypt, "");

    for (int ii = 0; pszString[ii] != '\0'; ii++)
    {
        unsigned int    uChar = (unsigned int) pszString[ii];
        char            szByte[32] = "";

        sprintf(szByte, "%02x", (uChar ^ 101) & 0xff);

        strcat(pszCrypt, szByte);
    }

    return (pszCrypt);

}




int             main(int argc, char *argv[])
{

    if (argc < 2)
    {
        printf("usage : %s  password\n", argv[0]);
        return (1);
    }


    char            szCrypt[1024] = "";

    StrCrypt(argv[1], szCrypt);


    printf("%s\n", szCrypt);


    return (0);

}
