#include <stdio.h>
#include <stdlib.h>
#include "testare_evaluare_util.h"
#include "testare_evaluare_util_minus.h"
int main(int argc, const char** argv) {
    if (argc > 1) {
        printf("Am primit argumentele: ");
        for (int i = 1; i < argc; i++) {
            printf("%s, ", argv[i]);
        }
        printf("\n");
    }
    printf("Util add: %d\n", util_add(20, 30));
    printf("Util minus: %d\n", util_minus(20, 30));
    return 0;
}
