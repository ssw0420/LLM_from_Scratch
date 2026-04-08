#include <stdio.h>

int main(void) {
    FILE *fp = fopen("output.txt", "w");
    if (fp == NULL) {
        printf("Failed to open file.\n");
        return 1;
    }
    fprintf(fp, "Hello, World!\n");
    fclose(fp);
    return 0;
}
