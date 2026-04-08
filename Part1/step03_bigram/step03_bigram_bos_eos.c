#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define HANGUL_BASE  0xAC00
#define HANGUL_END   0xD7A3
#define HANGUL_COUNT (HANGUL_END - HANGUL_BASE + 1)
#define GEN_WORDS    30

/*
 * 특수 토큰: 한글 완성형 범위 밖의 가상 코드포인트
 * BOS(Beginning of Sequence): 단어 시작을 나타내는 가상 토큰
 * EOS(End of Sequence):       단어 끝을 나타내는 가상 토큰
 *
 * BOS와 EOS를 바이그램 테이블에 통합하면
 * freq_start[] 같은 별도 구조가 필요 없어진다.
 *   - BOS → 첫글자  : 단어 시작 확률분포 (freq_start를 대체)
 *   - 마지막글자 → EOS : 단어 종료 확률
 *   - 글자 → 글자   : 기존 바이그램 전이
 */
#define CP_BOS  0x0002
#define CP_EOS  0x0001

/* ── 자료구조 ── */

typedef struct {
    int codepoint;  /* CP_EOS이면 "단어 끝" */
    int count;
} Pair;

typedef struct {
    int from_cp;
    Pair *nexts;
    int num_nexts;
    int cap_nexts;
    int total;
    double *cumulative;
} BigramRow;

#define ROW_HASH_SIZE 8192
typedef struct {
    BigramRow *rows;
    int num_rows;
    int cap_rows;
    int bucket[ROW_HASH_SIZE];
    int *chain;
} BigramTable;

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

/* ── 해시맵 ── */

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

static BigramRow *bigram_get_row(BigramTable *bt, int from_cp) {
    unsigned int h = hash_cp(from_cp);
    int idx = bt->bucket[h];
    while (idx != -1) {
        if (bt->rows[idx].from_cp == from_cp)
            return &bt->rows[idx];
        idx = bt->chain[idx];
    }
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

static void bigram_add(BigramTable *bt, int from_cp, int to_cp) {
    BigramRow *row = bigram_get_row(bt, from_cp);
    for (int i = 0; i < row->num_nexts; i++) {
        if (row->nexts[i].codepoint == to_cp) {
            row->nexts[i].count++;
            row->total++;
            return;
        }
    }
    if (row->num_nexts == row->cap_nexts) {
        row->cap_nexts = row->cap_nexts == 0 ? 8 : row->cap_nexts * 2;
        row->nexts = (Pair *)realloc(row->nexts, sizeof(Pair) * row->cap_nexts);
    }
    row->nexts[row->num_nexts].codepoint = to_cp;
    row->nexts[row->num_nexts].count = 1;
    row->num_nexts++;
    row->total++;
}

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

static BigramRow *bigram_find_row(BigramTable *bt, int from_cp) {
    unsigned int h = hash_cp(from_cp);
    int idx = bt->bucket[h];
    while (idx != -1) {
        if (bt->rows[idx].from_cp == from_cp)
            return &bt->rows[idx];
        idx = bt->chain[idx];
    }
    return NULL;
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

    /* ── 2. 바이그램 카운트 (BOS/EOS 통합) ── */
    /*
     * 모든 전이가 하나의 BigramTable에 들어간다:
     *   CP_BOS → 첫글자     (단어 시작 분포)
     *   글자   → 글자       (글자 간 전이)
     *   마지막글자 → CP_EOS  (단어 종료)
     *
     * freq_start[] 별도 배열이 더 이상 필요 없다.
     */
    BigramTable bt;
    bigram_init(&bt);

    long long total_bigrams = 0;

    int prev_cp = -1;
    int i = offset;
    while (i < file_size) {
        int cp;
        int len = utf8_decode(buf + i, &cp);

        if (is_hangul(cp)) {
            if (prev_cp == -1) {
                /* 단어 첫 글자: BOS → cp */
                bigram_add(&bt, CP_BOS, cp);
                total_bigrams++;
            } else {
                /* 글자 전이: prev → cp */
                bigram_add(&bt, prev_cp, cp);
                total_bigrams++;
            }
            prev_cp = cp;
        } else {
            /* 단어 경계: 직전 한글 → EOS */
            if (prev_cp != -1) {
                bigram_add(&bt, prev_cp, CP_EOS);
                total_bigrams++;
            }
            prev_cp = -1;
        }
        i += len;
    }
    /* 파일 끝 처리 */
    if (prev_cp != -1) {
        bigram_add(&bt, prev_cp, CP_EOS);
        total_bigrams++;
    }
    free(buf);

    /* BOS row 확인 */
    BigramRow *bos_row = bigram_find_row(&bt, CP_BOS);
    printf("바이그램 카운트 완료 (BOS/EOS 통합)\n");
    printf("  고유 행(from 글자) 수: %d\n", bt.num_rows);
    printf("  총 바이그램 쌍:       %lld\n", total_bigrams);
    printf("  BOS에서 시작 가능한 글자: %d종\n", bos_row ? bos_row->num_nexts : 0);
    printf("  BOS 총 카운트(=단어 수): %d\n\n", bos_row ? bos_row->total : 0);

    /* ── 3. 바이그램 테이블을 CSV로 저장 ── */
    {
        FILE *fcsv = fopen("output_bigram_table.csv", "w");
        if (fcsv) {
            /* UTF-8 BOM */
            fputc(0xEF, fcsv); fputc(0xBB, fcsv); fputc(0xBF, fcsv);
            fprintf(fcsv, "앞글자,뒷글자,빈도\n");
            for (int r = 0; r < bt.num_rows; r++) {
                BigramRow *row = &bt.rows[r];
                for (int j = 0; j < row->num_nexts; j++) {
                    /* 앞글자 */
                    if (row->from_cp == CP_BOS)
                        fprintf(fcsv, "<BOS>");
                    else
                        utf8_print(fcsv, row->from_cp);
                    fprintf(fcsv, ",");
                    /* 뒷글자 */
                    if (row->nexts[j].codepoint == CP_EOS)
                        fprintf(fcsv, "<EOS>");
                    else
                        utf8_print(fcsv, row->nexts[j].codepoint);
                    fprintf(fcsv, ",%d\n", row->nexts[j].count);
                }
            }
            fclose(fcsv);
            printf("바이그램 테이블 → output_bigram_table.csv 저장 완료\n\n");
        }
    }

    /* ── 4. 누적 확률 테이블 구축 ── */
    bigram_build_cumulative(&bt);

    /* ── 5. 단어 단위 텍스트 생성 ── */
    /*
     * 생성 루프가 극도로 단순해진다:
     *   1. current = CP_BOS
     *   2. next = sample(bt, current)
     *   3. next가 CP_EOS면 → 단어 끝
     *      아니면 → 출력하고 current = next, 2로 돌아감
     *
     * freq_start 배열 조회, 별도 시작 샘플링 로직이 전부 사라진다.
     */
    FILE *fout = fopen("output_bigram_bos_eos.txt", "w");
    if (!fout) {
        fprintf(stderr, "output_bigram_bos_eos.txt를 열 수 없습니다.\n");
        return 1;
    }

    printf("생성된 단어 (%d개):\n", GEN_WORDS);

    for (int w = 0; w < GEN_WORDS; w++) {
        int current = CP_BOS;  /* 항상 BOS에서 시작 */

        for (;;) {
            BigramRow *row = bigram_find_row(&bt, current);
            if (!row || row->num_nexts == 0) break;

            int next = sample_from_cumulative(row->nexts, row->cumulative, row->num_nexts);
            if (next == CP_EOS) break;  /* EOS가 샘플링되면 단어 끝 */

            utf8_print(stdout, next);
            utf8_print(fout, next);
            current = next;
        }

        printf("\n");
        fprintf(fout, "\n");
    }

    printf("\n결과가 output_bigram_bos_eos.txt에 저장되었습니다.\n");
    fclose(fout);

    bigram_free(&bt);

    return 0;
}
