#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define MAX_CHARS 3000
#define GEN_LENGTH 200

typedef struct {
    char utf8[5];   // UTF-8 한글은 최대 3바이트 + null
    int count;
} CharEntry;

int main(void)
{
    SetConsoleOutputCP(65001);

    // 1. output_charfreq.txt 파싱
    FILE *fin = fopen("../step01_charfreq/output_charfreq.txt", "r");
    if (!fin) {
        fprintf(stderr, "output_charfreq.txt를 열 수 없습니다.\n");
        return 1;
    }

    CharEntry entries[MAX_CHARS];
    int num_entries = 0;
    long long total_count = 0;

    char line[256];
    int line_num = 0;
    while (fgets(line, sizeof(line), fin)) {
        line_num++;
        if (line_num <= 4) continue;  // 헤더 4줄 스킵

        // 형식: Rank\tChar\tCount\tPercent
        int rank, count;
        char ch[16];
        if (sscanf(line, "%d\t%s\t%d", &rank, ch, &count) == 3) {
            strcpy(entries[num_entries].utf8, ch);
            entries[num_entries].count = count;
            total_count += count;
            num_entries++;
            if (num_entries >= MAX_CHARS) break;
        }
    }
    fclose(fin);

    printf("로드 완료: %d개 글자, 총 빈도 %lld\n\n", num_entries, total_count);

    // 2. 누적 확률 테이블 구축
    double *cumulative = malloc(sizeof(double) * num_entries);
    long long running = 0;
    for (int i = 0; i < num_entries; i++) {
        running += entries[i].count;
        cumulative[i] = (double)running / total_count;
    }

    // 3. 랜덤 샘플링으로 텍스트 생성
    srand((unsigned int)time(NULL));

    FILE *fout = fopen("output_unigram.txt", "w");
    if (!fout) {
        fprintf(stderr, "output_unigram.txt를 열 수 없습니다.\n");
        free(cumulative);
        return 1;
    }

    printf("생성된 텍스트 (%d글자):\n", GEN_LENGTH);

    for (int i = 0; i < GEN_LENGTH; i++) {
        double r = (double)rand() / RAND_MAX;

        // 누적 확률에서 해당 구간 찾기 (선형 탐색)
        int idx = 0;
        for (int j = 0; j < num_entries; j++) {
            if (r <= cumulative[j]) {
                idx = j;
                break;
            }
        }

        printf("%s", entries[idx].utf8);
        fprintf(fout, "%s", entries[idx].utf8);

        if ((i + 1) % 10 == 0) {
            printf("\n");
            fprintf(fout, "\n");
        }
    }

    printf("\n\n결과가 output_unigram.txt에 저장되었습니다.\n");
    fprintf(fout, "\n");
    fclose(fout);
    free(cumulative);

    return 0;
}
