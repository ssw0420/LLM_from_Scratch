#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define HANGUL_BASE  0xAC00
#define HANGUL_END   0xD7A3
#define HANGUL_COUNT (HANGUL_END - HANGUL_BASE + 1)

typedef struct {
    int codepoint;
    int count;
} CharFreq;

/* UTF-8 바이트열에서 유니코드 코드포인트 하나를 읽고, 다음 위치를 반환 */
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

/* UTF-8로 코드포인트를 출력 */
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

static int compare_desc(const void *a, const void *b) {
    return ((CharFreq *)b)->count - ((CharFreq *)a)->count;
}

int main(int argc, char *argv[]) {
    const char *input_path = "../korean_dict/words_only.txt";
    const char *output_path = "output_charfreq.txt";

    if (argc >= 2) input_path = argv[1];
    if (argc >= 3) output_path = argv[2];

#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    FILE *fp = fopen(input_path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open: %s\n", input_path);
        return 1;
    }

    /* 파일 크기 확인 후 전체 읽기 */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *buf = (unsigned char *)malloc(file_size + 1);
    fread(buf, 1, file_size, fp);
    buf[file_size] = 0;
    fclose(fp);

    /* UTF-8 BOM 건너뛰기 */
    int offset = 0;
    if (file_size >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
        offset = 3;

    /* 완성형 한글 빈도 배열 */
    int *freq = (int *)calloc(HANGUL_COUNT, sizeof(int));
    long total_chars = 0;
    int unique_chars = 0;

    /* 한 글자씩 디코딩하며 카운트 */
    int i = offset;
    while (i < file_size) {
        int cp;
        int len = utf8_decode(buf + i, &cp);
        if (cp >= HANGUL_BASE && cp <= HANGUL_END) {
            freq[cp - HANGUL_BASE]++;
            total_chars++;
        }
        i += len;
    }

    free(buf);

    /* 빈도 > 0인 글자만 수집 */
    CharFreq *results = (CharFreq *)malloc(HANGUL_COUNT * sizeof(CharFreq));
    for (int j = 0; j < HANGUL_COUNT; j++) {
        if (freq[j] > 0) {
            results[unique_chars].codepoint = HANGUL_BASE + j;
            results[unique_chars].count = freq[j];
            unique_chars++;
        }
    }

    free(freq);

    /* 빈도 내림차순 정렬 */
    qsort(results, unique_chars, sizeof(CharFreq), compare_desc);

    /* 결과 출력 */
    FILE *out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "Failed to open: %s\n", output_path);
        free(results);
        return 1;
    }

    /* 헤더 */
    fprintf(out, "Total characters: %ld\n", total_chars);
    fprintf(out, "Unique characters: %d\n\n", unique_chars);
    fprintf(out, "Rank\tChar\tCount\tPercent\n");

    for (int j = 0; j < unique_chars; j++) {
        double pct = (double)results[j].count / total_chars * 100.0;
        fprintf(out, "%d\t", j + 1);
        utf8_print(out, results[j].codepoint);
        fprintf(out, "\t%d\t%.4f%%\n", results[j].count, pct);
    }

    fclose(out);

    /* 콘솔에 상위 20개 출력 */
    printf("Total characters: %ld\n", total_chars);
    printf("Unique characters: %d\n\n", unique_chars);
    printf("Top 20:\n");
    int show = unique_chars < 20 ? unique_chars : 20;
    for (int j = 0; j < show; j++) {
        double pct = (double)results[j].count / total_chars * 100.0;
        printf("  %2d. ", j + 1);
        utf8_print(stdout, results[j].codepoint);
        printf("  %6d  (%.2f%%)\n", results[j].count, pct);
    }
    printf("\nFull results saved to: %s\n", output_path);

    free(results);
    return 0;
}