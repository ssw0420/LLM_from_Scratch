#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define HANGUL_BASE  0xAC00
#define HANGUL_END   0xD7A3
#define HANGUL_COUNT (HANGUL_END - HANGUL_BASE + 1)
#define GEN_LENGTH   200

typedef struct {
    int codepoint;
    int count;
} CharFreq;

/* UTF-8 바이트열에서 유니코드 코드포인트 하나를 읽고, 바이트 길이를 반환 */
static int utf8_decode(const unsigned char *buf, int *codepoint) {
    if (buf[0] < 0x80) {
        *codepoint = buf[0];
        return 1;
    } else if ((buf[0] & 0xE0) == 0xC0) {
        *codepoint = ((buf[0] & 0x1F) << 6) | (buf[1] & 0x3F);
        return 2;
    } else if ((buf[0] & 0xF0) == 0xE0) {
        *codepoint = ((buf[0] & 0x0F) << 12) | ((buf[1] & 0x3F) << 6) | (buf[2] & 0x3F);
        return 3;
    } else if ((buf[0] & 0xF8) == 0xF0) {
        *codepoint = ((buf[0] & 0x07) << 18) | ((buf[1] & 0x3F) << 12) |
                     ((buf[2] & 0x3F) << 6) | (buf[3] & 0x3F);
        return 4;
    }
    *codepoint = -1;
    return 1;
}

/* 코드포인트를 UTF-8로 출력 */
static void utf8_print(FILE *fp, int cp) {
    if (cp < 0x80) {
        fputc(cp, fp);
    } else if (cp < 0x800) {
        fputc(0xC0 | (cp >> 6), fp);
        fputc(0x80 | (cp & 0x3F), fp);
    } else if (cp < 0x10000) {
        fputc(0xE0 | (cp >> 12), fp);
        fputc(0x80 | ((cp >> 6) & 0x3F), fp);
        fputc(0x80 | (cp & 0x3F), fp);
    }
}

int main(void)
{
    FILE *fp, *fout;
    long file_size;
    unsigned char *buf;
    int offset, *freq, num_entries, i, j, k, cp, len, idx;
    long long total_chars, running;
    CharFreq *entries;
    double *cumulative, r;

    SetConsoleOutputCP(65001);
    srand((unsigned int)time(NULL));

    /* 1. words_only.txt 읽기 */
    fp = fopen("../korean_dict/words_only.txt", "rb");
    if (!fp) {
        fprintf(stderr, "../korean_dict/words_only.txt를 열 수 없습니다.\n");
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    buf = (unsigned char *)malloc(file_size + 1);
    fread(buf, 1, file_size, fp);
    buf[file_size] = 0;
    fclose(fp);

    /* UTF-8 BOM 건너뛰기 */
    offset = 0;
    if (file_size >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
        offset = 3;

    /* 2. 한글 완성형 글자 빈도 카운트 */
    freq = (int *)calloc(HANGUL_COUNT, sizeof(int));
    total_chars = 0;

    i = offset;
    while (i < file_size) {
        len = utf8_decode(buf + i, &cp);
        if (cp >= HANGUL_BASE && cp <= HANGUL_END) {
            freq[cp - HANGUL_BASE]++;
            total_chars++;
        }
        i += len;
    }
    free(buf);

    /* 3. 빈도 > 0인 글자만 수집 */
    entries = (CharFreq *)malloc(HANGUL_COUNT * sizeof(CharFreq));
    num_entries = 0;

    for (j = 0; j < HANGUL_COUNT; j++) {
        if (freq[j] > 0) {
            entries[num_entries].codepoint = HANGUL_BASE + j;
            entries[num_entries].count = freq[j];
            num_entries++;
        }
    }
    free(freq);

    printf("로드 완료: %d개 글자, 총 빈도 %lld\n\n", num_entries, total_chars);

    /* 4. 누적 확률 테이블 구축 */
    cumulative = (double *)malloc(sizeof(double) * num_entries);
    running = 0;
    for (j = 0; j < num_entries; j++) {
        running += entries[j].count;
        cumulative[j] = (double)running / total_chars;
    }

    /* 5. 랜덤 샘플링으로 텍스트 생성 */
    fout = fopen("output_unigram2.txt", "w");
    if (!fout) {
        fprintf(stderr, "output_unigram2.txt를 열 수 없습니다.\n");
        free(cumulative);
        free(entries);
        return 1;
    }

    printf("생성된 텍스트 (%d글자):\n", GEN_LENGTH);

    for (k = 0; k < GEN_LENGTH; k++) {
        r = (double)rand() / RAND_MAX;

        idx = 0;
        for (j = 0; j < num_entries; j++) {
            if (r <= cumulative[j]) {
                idx = j;
                break;
            }
        }

        utf8_print(stdout, entries[idx].codepoint);
        utf8_print(fout, entries[idx].codepoint);

        if ((k + 1) % 10 == 0) {
            printf("\n");
            fprintf(fout, "\n");
        }
    }

    printf("\n결과가 output_unigram2.txt에 저장되었습니다.\n");
    fclose(fout);
    free(cumulative);
    free(entries);

    return 0;
}
