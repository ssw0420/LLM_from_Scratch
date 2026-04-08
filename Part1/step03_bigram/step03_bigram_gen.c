#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define HANGUL_BASE  0xAC00
#define HANGUL_END   0xD7A3
#define HANGUL_COUNT (HANGUL_END - HANGUL_BASE + 1)
#define GEN_LENGTH   200

/* ── 자료구조 ── */

typedef struct {
    int codepoint;
    int count;
} Pair;

/* 하나의 "이전 글자"에 대한 바이그램 행 */
typedef struct {
    int from_cp;
    Pair *nexts;        /* 다음 글자 목록 */
    int num_nexts;
    int cap_nexts;
    int total;          /* count 합계 */
    double *cumulative; /* 누적 확률 (생성 단계에서 구축) */
} BigramRow;

/* 해시맵: from_cp -> BigramRow */
#define ROW_HASH_SIZE 8192
typedef struct {
    BigramRow *rows;
    int num_rows;
    int cap_rows;
    int bucket[ROW_HASH_SIZE]; /* bucket[hash] -> rows 인덱스, -1이면 비어있음 */
    int *chain;                /* 체이닝용 */
} BigramTable;

/* 단어 시작 글자 빈도 (첫 글자 샘플링용) */
typedef struct {
    int codepoint;
    int count;
} StartFreq;

/* ── UTF-8 유틸 ── */

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

static int is_hangul(int cp) {
    return cp >= HANGUL_BASE && cp <= HANGUL_END;
}

/* ── 해시맵 연산 ── */

static unsigned int hash_cp(int cp) {
    return (unsigned int)cp % ROW_HASH_SIZE;
}

static void bigram_init(BigramTable *bt) {
    bt->num_rows = 0;
    bt->cap_rows = 4096;
    bt->rows = (BigramRow *)malloc(sizeof(BigramRow) * bt->cap_rows);
    bt->chain = (int *)malloc(sizeof(int) * bt->cap_rows);
    for (int i = 0; i < ROW_HASH_SIZE; i++)
        bt->bucket[i] = -1;
}

/* from_cp에 해당하는 row를 찾거나 새로 만든다 */
static BigramRow *bigram_get_row(BigramTable *bt, int from_cp) {
    unsigned int h = hash_cp(from_cp);
    int idx = bt->bucket[h];
    while (idx != -1) {
        if (bt->rows[idx].from_cp == from_cp)
            return &bt->rows[idx];
        idx = bt->chain[idx];
    }
    /* 새 row 생성 */
    if (bt->num_rows == bt->cap_rows) {
        bt->cap_rows *= 2;
        bt->rows = (BigramRow *)realloc(bt->rows, sizeof(BigramRow) * bt->cap_rows);
        bt->chain = (int *)realloc(bt->chain, sizeof(int) * bt->cap_rows);
    }
    idx = bt->num_rows++;
    bt->rows[idx].from_cp = from_cp;
    bt->rows[idx].nexts = NULL;
    bt->rows[idx].num_nexts = 0;
    bt->rows[idx].cap_nexts = 0;
    bt->rows[idx].total = 0;
    bt->rows[idx].cumulative = NULL;
    bt->chain[idx] = bt->bucket[h];
    bt->bucket[h] = idx;
    return &bt->rows[idx];
}

/* row 안에서 to_cp의 count를 1 증가 */
static void bigram_add(BigramTable *bt, int from_cp, int to_cp) {
    BigramRow *row = bigram_get_row(bt, from_cp);

    /* 이미 있는 to_cp인지 선형 탐색 */
    for (int i = 0; i < row->num_nexts; i++) {
        if (row->nexts[i].codepoint == to_cp) {
            row->nexts[i].count++;
            row->total++;
            return;
        }
    }
    /* 새 항목 추가 */
    if (row->num_nexts == row->cap_nexts) {
        row->cap_nexts = row->cap_nexts == 0 ? 8 : row->cap_nexts * 2;
        row->nexts = (Pair *)realloc(row->nexts, sizeof(Pair) * row->cap_nexts);
    }
    row->nexts[row->num_nexts].codepoint = to_cp;
    row->nexts[row->num_nexts].count = 1;
    row->num_nexts++;
    row->total++;
}

/* 각 row에 대해 누적 확률 테이블 구축 */
static void bigram_build_cumulative(BigramTable *bt) {
    for (int i = 0; i < bt->num_rows; i++) {
        BigramRow *row = &bt->rows[i];
        if (row->num_nexts == 0) continue;
        row->cumulative = (double *)malloc(sizeof(double) * row->num_nexts);
        long long running = 0;
        for (int j = 0; j < row->num_nexts; j++) {
            running += row->nexts[j].count;
            row->cumulative[j] = (double)running / row->total;
        }
    }
}

static void bigram_free(BigramTable *bt) {
    for (int i = 0; i < bt->num_rows; i++) {
        free(bt->rows[i].nexts);
        free(bt->rows[i].cumulative);
    }
    free(bt->rows);
    free(bt->chain);
}

/* ── 샘플링 ── */

/* 누적 확률 배열에서 이진 탐색으로 샘플링 */
static int sample_from_cumulative(Pair *entries, double *cumul, int n) {
    double r = (double)rand() / RAND_MAX;
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (cumul[mid] < r)
            lo = mid + 1;
        else
            hi = mid;
    }
    return entries[lo].codepoint;
}

int main(void)
{
    SetConsoleOutputCP(65001);
    srand((unsigned int)time(NULL));

    /* ── 1. 파일 읽기 ── */
    FILE *fp = fopen("../korean_dict/words_only.txt", "rb");
    if (!fp) {
        fprintf(stderr, "../korean_dict/words_only.txt를 열 수 없습니다.\n");
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *buf = (unsigned char *)malloc(file_size + 1);
    fread(buf, 1, file_size, fp);
    buf[file_size] = 0;
    fclose(fp);

    int offset = 0;
    if (file_size >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
        offset = 3;

    /* ── 2. 바이그램 카운트 + 단어 시작 글자 빈도 ── */
    BigramTable bt;
    bigram_init(&bt);

    /* 시작 글자 빈도: freq_start[cp - HANGUL_BASE] */
    int *freq_start = (int *)calloc(HANGUL_COUNT, sizeof(int));
    long long total_start = 0;
    long long total_bigrams = 0;

    int prev_cp = -1;  /* 직전 한글 글자, -1이면 단어 시작 */
    int i = offset;
    while (i < file_size) {
        int cp;
        int len = utf8_decode(buf + i, &cp);

        if (is_hangul(cp)) {
            if (prev_cp == -1) {
                /* 단어 첫 글자 */
                freq_start[cp - HANGUL_BASE]++;
                total_start++;
            } else {
                /* 바이그램: prev_cp -> cp */
                bigram_add(&bt, prev_cp, cp);
                total_bigrams++;
            }
            prev_cp = cp;
        } else {
            /* 한글이 아닌 문자 (줄바꿈, 공백 등) → 단어 경계 */
            prev_cp = -1;
        }
        i += len;
    }
    free(buf);

    printf("바이그램 카운트 완료\n");
    printf("  고유 이전 글자 수: %d\n", bt.num_rows);
    printf("  총 바이그램 쌍:   %lld\n", total_bigrams);
    printf("  단어 시작 글자:   %lld회\n\n", total_start);

    /* ── 3. 누적 확률 테이블 구축 ── */

    /* 3-a. 바이그램 테이블 */
    bigram_build_cumulative(&bt);

    /* 3-b. 시작 글자 테이블 */
    StartFreq *starts = (StartFreq *)malloc(sizeof(StartFreq) * HANGUL_COUNT);
    int num_starts = 0;
    for (int j = 0; j < HANGUL_COUNT; j++) {
        if (freq_start[j] > 0) {
            starts[num_starts].codepoint = HANGUL_BASE + j;
            starts[num_starts].count = freq_start[j];
            num_starts++;
        }
    }
    free(freq_start);

    double *start_cumul = (double *)malloc(sizeof(double) * num_starts);
    {
        long long running = 0;
        for (int j = 0; j < num_starts; j++) {
            running += starts[j].count;
            start_cumul[j] = (double)running / total_start;
        }
    }

    /* ── 4. 텍스트 생성 ── */
    FILE *fout = fopen("output_bigram.txt", "w");
    if (!fout) {
        fprintf(stderr, "output_bigram.txt를 열 수 없습니다.\n");
        return 1;
    }

    printf("생성된 텍스트 (%d글자):\n", GEN_LENGTH);

    /* 첫 글자: 시작 글자 분포에서 샘플링 */
    int current_cp;
    {
        double r = (double)rand() / RAND_MAX;
        int idx = 0;
        for (int j = 0; j < num_starts; j++) {
            if (r <= start_cumul[j]) { idx = j; break; }
        }
        current_cp = starts[idx].codepoint;
    }
    utf8_print(stdout, current_cp);
    utf8_print(fout, current_cp);

    for (int k = 1; k < GEN_LENGTH; k++) {
        /* 현재 글자의 바이그램 row 조회 */
        BigramRow *row = NULL;
        unsigned int h = hash_cp(current_cp);
        int idx = bt.bucket[h];
        while (idx != -1) {
            if (bt.rows[idx].from_cp == current_cp) {
                row = &bt.rows[idx];
                break;
            }
            idx = bt.chain[idx];
        }

        if (row && row->num_nexts > 0) {
            /* 바이그램 분포에서 다음 글자 샘플링 */
            current_cp = sample_from_cumulative(row->nexts, row->cumulative, row->num_nexts);
        } else {
            /* 후속 글자가 없음 → 새 단어 시작 */
            double r = (double)rand() / RAND_MAX;
            int si = 0;
            for (int j = 0; j < num_starts; j++) {
                if (r <= start_cumul[j]) { si = j; break; }
            }
            current_cp = starts[si].codepoint;
        }

        utf8_print(stdout, current_cp);
        utf8_print(fout, current_cp);

        if ((k + 1) % 20 == 0) {
            printf("\n");
            fprintf(fout, "\n");
        }
    }

    printf("\n\n결과가 output_bigram.txt에 저장되었습니다.\n");
    fclose(fout);

    /* ── 정리 ── */
    free(starts);
    free(start_cumul);
    bigram_free(&bt);

    return 0;
}
