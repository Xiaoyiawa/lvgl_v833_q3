#include "str_utils.h"

bool str_begin(const char * str1, const char * str2)
{
    if(str1 == NULL || str2 == NULL) return false;

    uint16_t len1 = strlen(str1);
    uint16_t len2 = strlen(str2);
    if((len1 < len2) || (len1 == 0 || len2 == 0)) return false;

    uint16_t i = 0;
    char * p   = str2;
    while(*p != '\0') {
        if(*p != str1[i]) return false;

        p++;
        i++;
    }

    return true;
}

bool str_end(const char * str1, const char * str2)
{
    if(str1 == NULL || str2 == NULL) return false;

    uint16_t len1 = strlen(str1);
    uint16_t len2 = strlen(str2);
    if((len1 < len2) || (len1 == 0 || len2 == 0)) return false;

    while(len2 >= 1) {
        if(str2[len2 - 1] != str1[len1 - 1]) return false;

        len2--;
        len1--;
    }

    return true;
}