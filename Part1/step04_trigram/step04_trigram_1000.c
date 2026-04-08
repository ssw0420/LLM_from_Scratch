/*
 * Step 4: 트라이그램 모델 (BOS/EOS 통합 + 사전 검증)
 *
 * 바이그램: P(next | prev)     — 직전 1글자로 예측
 * 트라이그램: P(next | prev2, prev1) — 직전 2글자로 예측
 *
 * 키가 단일 코드포인트에서 (cp1, cp2) 쌍으로 확장된다.
 * BOS/EOS 토큰으로 단어 경계를 표현하는 구조는 바이그램과 동일.
 *
 *   (BOS, BOS)    → 첫글자      : 단어 시작
 *   (BOS, 첫글자)  → 둘째글자    : 시작 바이그램 컨텍스트
 *   (글자i, 글자j) → 글자k       : 일반 트라이그램
 *   (글자n-1, 글자n) → EOS       : 단어 종료
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define HANGUL_BASE  0xAC00
#define HANGUL_END   0xD7A3
#define GEN_WORDS    1000

#define CP_BOS  0x0002
#define CP_EOS  0x0001

/* ── 자료구조 ── */

typedef struct {
    int codepoint;
    int count;
} Pair;

typedef struct {
    int cp1, cp2;       /* 키: 직전 2글자 */
    Pair *nexts;
    int num_nexts;
    int cap_nexts;
    int total;
    double *cumulative;
} TrigramRow;

#define ROW_HASH_SIZE 131071  /* 소수: 바이그램보다 행이 훨씬 많으므로 크게 */
typedef struct {
    TrigramRow *rows;
    int num_rows;
    int cap_rows;
    int bucket[ROW_HASH_SIZE];
    int *chain;
} TrigramTable;

/* ── 사전 해시셋 ── */

#define DICT_HASH_SIZE 524287

typedef struct DictNode {
    char *word;
    struct DictNode *next;
} DictNode;

typedef struct {
    DictNode **buckets;
    int count;
} DictSet;

static unsigned int dict_hash(const char *s) {
    unsigned int h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h % DICT_HASH_SIZE;
}

static void dict_init(DictSet *ds) {
    ds->buckets = (DictNode **)calloc(DICT_HASH_SIZE, sizeof(DictNode *));
    ds->count = 0;
}

static void dict_insert(DictSet *ds, const char *word) {
    unsigned int h = dict_hash(word);
    DictNode *node = (DictNode *)malloc(sizeof(DictNode));
    node->word = _strdup(word);
    node->next = ds->buckets[h];
    ds->buckets[h] = node;
    ds->count++;
}

static int dict_contains(DictSet *ds, const char *word) {
    unsigned int h = dict_hash(word);
    DictNode *node = ds->buckets[h];
    while (node) {
        if (strcmp(node->word, word) == 0)
            return 1;
        node = node->next;
    }
    return 0;
}

static void dict_free(DictSet *ds) {
    for (int i = 0; i < DICT_HASH_SIZE; i++) {
        DictNode *node = ds->buckets[i];
        while (node) {
            DictNode *next = node->next;
            free(node->word);
            free(node);
            node = next;
        }
    }
    free(ds->buckets);
}

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

static int utf8_encode(int cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    return 0;
}

static void utf8_print(FILE *fp, int cp) {
    char tmp[4];
    int len = utf8_encode(cp, tmp);
    fwrite(tmp, 1, len, fp);
}

static int is_hangul(int cp) {
    return cp >= HANGUL_BASE && cp <= HANGUL_END;
}

/* ── 트라이그램 해시맵 ── */

/*
 * 키가 (cp1, cp2) 쌍이므로 해시도 두 값을 결합한다.
 * 바이그램의 hash_cp(single) → trigram의 hash_pair(cp1, cp2)
 */
static unsigned int hash_pair(int cp1, int cp2) {
    unsigned int h = (unsigned int)cp1 * 65537 + (unsigned int)cp2;
    return h % ROW_HASH_SIZE;
}

static void trigram_init(TrigramTable *tt) {
    tt->num_rows = 0;
    tt->cap_rows = 8192;
    tt->rows = (TrigramRow *)malloc(sizeof(TrigramRow) * tt->cap_rows);
    tt->chain = (int *)malloc(sizeof(int) * tt->cap_rows);
    for (int i = 0; i < ROW_HASH_SIZE; i++)
        tt->bucket[i] = -1;
}

static TrigramRow *trigram_get_row(TrigramTable *tt, int cp1, int cp2) {
    unsigned int h = hash_pair(cp1, cp2);
    int idx = tt->bucket[h];
    while (idx != -1) {
        if (tt->rows[idx].cp1 == cp1 && tt->rows[idx].cp2 == cp2)
            return &tt->rows[idx];
        idx = tt->chain[idx];
    }
    if (tt->num_rows == tt->cap_rows) {
        tt->cap_rows *= 2;
        tt->rows = (TrigramRow *)realloc(tt->rows, sizeof(TrigramRow) * tt->cap_rows);
        tt->chain = (int *)realloc(tt->chain, sizeof(int) * tt->cap_rows);
    }
    idx = tt->num_rows++;
    tt->rows[idx].cp1 = cp1;
    tt->rows[idx].cp2 = cp2;
    tt->rows[idx].nexts = NULL;
    tt->rows[idx].num_nexts = 0;
    tt->rows[idx].cap_nexts = 0;
    tt->rows[idx].total = 0;
    tt->rows[idx].cumulative = NULL;
    tt->chain[idx] = tt->bucket[h];
    tt->bucket[h] = idx;
    return &tt->rows[idx];
}

static void trigram_add(TrigramTable *tt, int cp1, int cp2, int to_cp) {
    TrigramRow *row = trigram_get_row(tt, cp1, cp2);
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

static void trigram_build_cumulative(TrigramTable *tt) {
    for (int i = 0; i < tt->num_rows; i++) {
        TrigramRow *row = &tt->rows[i];
        if (row->num_nexts == 0) continue;
        row->cumulative = (double *)malloc(sizeof(double) * row->num_nexts);
        long long running = 0;
        for (int j = 0; j < row->num_nexts; j++) {
            running += row->nexts[j].count;
            row->cumulative[j] = (double)running / row->total;
        }
    }
}

static TrigramRow *trigram_find_row(TrigramTable *tt, int cp1, int cp2) {
    unsigned int h = hash_pair(cp1, cp2);
    int idx = tt->bucket[h];
    while (idx != -1) {
        if (tt->rows[idx].cp1 == cp1 && tt->rows[idx].cp2 == cp2)
            return &tt->rows[idx];
        idx = tt->chain[idx];
    }
    return NULL;
}

static void trigram_free(TrigramTable *tt) {
    for (int i = 0; i < tt->num_rows; i++) {
        free(tt->rows[i].nexts);
        free(tt->rows[i].cumulative);
    }
    free(tt->rows);
    free(tt->chain);
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

int main(void)
{
    SetConsoleOutputCP(65001);
    srand((unsigned int)time(NULL));

    /* ── 1. 사전 로드 ── */
    DictSet dict;
    dict_init(&dict);
    {
        FILE *fp = fopen("../korean_dict/words_only.txt", "rb");
        if (!fp) {
            fprintf(stderr, "../korean_dict/words_only.txt를 열 수 없습니다.\n");
            return 1;
        }

        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        char *raw = (char *)malloc(fsize + 1);
        fread(raw, 1, fsize, fp);
        raw[fsize] = 0;
        fclose(fp);

        int start = 0;
        if (fsize >= 3 && (unsigned char)raw[0] == 0xEF &&
            (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
            start = 3;

        char *p = raw + start;
        while (*p) {
            char *eol = p;
            while (*eol && *eol != '\n' && *eol != '\r')
                eol++;

            int len = (int)(eol - p);
            if (len > 0) {
                char tmp = *eol;
                *eol = 0;
                dict_insert(&dict, p);
                *eol = tmp;
            }

            if (*eol == '\r') eol++;
            if (*eol == '\n') eol++;
            p = eol;
        }
        free(raw);
    }
    printf("사전 로드 완료: %d개 단어\n\n", dict.count);

    /* ── 2. 파일 읽기 ── */
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

    /* ── 3. 트라이그램 카운트 ── */
    /*
     * 단어 "가나다"에서 발생하는 트라이그램:
     *   (BOS, BOS) → 가      ← 단어 시작
     *   (BOS, 가)  → 나      ← 첫글자 이후 컨텍스트
     *   (가, 나)   → 다      ← 일반 트라이그램
     *   (나, 다)   → EOS     ← 단어 종료
     *
     * prev2, prev1으로 직전 2글자를 추적한다.
     * 단어 시작 시 prev2=BOS, prev1=BOS로 초기화.
     */
    TrigramTable tt;
    trigram_init(&tt);

    long long total_trigrams = 0;
    int prev2 = -1, prev1 = -1;  /* -1 = 단어 밖 */

    int i = offset;
    while (i < file_size) {
        int cp;
        int len = utf8_decode(buf + i, &cp);

        if (is_hangul(cp)) {
            if (prev1 == -1) {
                /* 단어 첫 글자: (BOS, BOS) → cp */
                trigram_add(&tt, CP_BOS, CP_BOS, cp);
                total_trigrams++;
                prev2 = CP_BOS;
                prev1 = cp;
            } else if (prev2 == CP_BOS && prev1 != CP_BOS) {
                /* 단어 둘째 글자: (BOS, 첫글자) → cp */
                trigram_add(&tt, CP_BOS, prev1, cp);
                total_trigrams++;
                prev2 = prev1;
                prev1 = cp;
            } else {
                /* 일반 전이: (prev2, prev1) → cp */
                trigram_add(&tt, prev2, prev1, cp);
                total_trigrams++;
                prev2 = prev1;
                prev1 = cp;
            }
        } else {
            /* 단어 경계: (prev2, prev1) → EOS */
            if (prev1 != -1) {
                trigram_add(&tt, prev2, prev1, CP_EOS);
                total_trigrams++;
            }
            prev2 = -1;
            prev1 = -1;
        }
        i += len;
    }
    if (prev1 != -1) {
        trigram_add(&tt, prev2, prev1, CP_EOS);
        total_trigrams++;
    }
    free(buf);

    /* BOS,BOS row 확인 */
    TrigramRow *bos_row = trigram_find_row(&tt, CP_BOS, CP_BOS);
    printf("트라이그램 카운트 완료 (BOS/EOS 통합)\n");
    printf("  고유 행(cp1,cp2 쌍) 수: %d\n", tt.num_rows);
    printf("  총 트라이그램 쌍:       %lld\n", total_trigrams);
    printf("  (BOS,BOS)에서 시작 가능한 글자: %d종\n",
           bos_row ? bos_row->num_nexts : 0);
    printf("  (BOS,BOS) 총 카운트(=단어 수): %d\n\n",
           bos_row ? bos_row->total : 0);

    /* ── 4. 트라이그램 테이블을 CSV로 저장 ── */
    {
        FILE *fcsv = fopen("output_trigram_table.csv", "w");
        if (fcsv) {
            /* UTF-8 BOM */
            fputc(0xEF, fcsv); fputc(0xBB, fcsv); fputc(0xBF, fcsv);
            fprintf(fcsv, "글자1,글자2,글자3,빈도\n");

            long long csv_rows = 0;
            for (int r = 0; r < tt.num_rows; r++) {
                TrigramRow *row = &tt.rows[r];
                for (int j = 0; j < row->num_nexts; j++) {
                    /* 글자1 */
                    if (row->cp1 == CP_BOS)
                        fprintf(fcsv, "<BOS>");
                    else
                        utf8_print(fcsv, row->cp1);
                    fprintf(fcsv, ",");
                    /* 글자2 */
                    if (row->cp2 == CP_BOS)
                        fprintf(fcsv, "<BOS>");
                    else
                        utf8_print(fcsv, row->cp2);
                    fprintf(fcsv, ",");
                    /* 다음글자 */
                    if (row->nexts[j].codepoint == CP_EOS)
                        fprintf(fcsv, "<EOS>");
                    else
                        utf8_print(fcsv, row->nexts[j].codepoint);
                    fprintf(fcsv, ",%d\n", row->nexts[j].count);
                    csv_rows++;
                }
            }
            fclose(fcsv);
            printf("트라이그램 테이블 → output_trigram_table.csv 저장 완료 (%lld행)\n\n",
                   csv_rows);
        }
    }

    /* ── 5. 누적 확률 테이블 구축 ── */
    trigram_build_cumulative(&tt);

    /* ── 6. 단어 생성 + 사전 검증 ── */
    /*
     * 생성 루프:
     *   1. prev2 = BOS, prev1 = BOS
     *   2. next = sample(tt, prev2, prev1)
     *   3. next가 EOS면 → 단어 끝
     *      아니면 → 출력, prev2 = prev1, prev1 = next, 2로 돌아감
     */
    FILE *fout = fopen("output_trigram_verify_1000.txt", "w");
    if (!fout) {
        fprintf(stderr, "output_trigram_verify_1000.txt를 열 수 없습니다.\n");
        return 1;
    }
    fputc(0xEF, fout); fputc(0xBB, fout); fputc(0xBF, fout);

    int hit = 0;

    printf("생성된 단어 (%d개):\n", GEN_WORDS);
    printf("-------------------------------\n");

    for (int w = 0; w < GEN_WORDS; w++) {
        char word_buf[256];
        int word_len = 0;

        int cur2 = CP_BOS, cur1 = CP_BOS;

        for (;;) {
            TrigramRow *row = trigram_find_row(&tt, cur2, cur1);
            if (!row || row->num_nexts == 0) break;

            int next = sample_from_cumulative(row->nexts, row->cumulative, row->num_nexts);
            if (next == CP_EOS) break;

            int enc_len = utf8_encode(next, word_buf + word_len);
            word_len += enc_len;
            if (word_len > 240) break;

            cur2 = cur1;
            cur1 = next;
        }
        word_buf[word_len] = 0;

        int found = dict_contains(&dict, word_buf);
        if (found) hit++;

        const char *mark = found ? "O" : "X";

        fprintf(fout, "%d\t%s\t%s\n", w + 1, word_buf, mark);
    }

    printf("-------------------------------\n");
    printf("적중: %d / %d (%.1f%%)\n", hit, GEN_WORDS,
           100.0 * hit / GEN_WORDS);

    fprintf(fout, "\n적중: %d / %d (%.1f%%)\n", hit, GEN_WORDS,
            100.0 * hit / GEN_WORDS);
    fclose(fout);

    printf("\n결과가 output_trigram_verify_1000.txt에 저장되었습니다.\n");

    trigram_free(&tt);
    dict_free(&dict);

    return 0;
}
