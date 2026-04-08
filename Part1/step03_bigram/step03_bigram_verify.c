#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define HANGUL_BASE  0xAC00
#define HANGUL_END   0xD7A3
#define HANGUL_COUNT (HANGUL_END - HANGUL_BASE + 1)
#define GEN_WORDS    30

#define CP_BOS  0x0002
#define CP_EOS  0x0001

/* ── 바이그램 자료구조 (bos_eos.c와 동일) ── */

typedef struct {
    int codepoint;
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

/* ── 사전 해시셋 ── */

#define DICT_HASH_SIZE 524287  /* 소수: 42만 단어에 적절 */

typedef struct DictNode {
    char *word;
    struct DictNode *next;
} DictNode;

typedef struct {
    DictNode **buckets;  /* 힙 할당 (스택 오버플로 방지) */
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
    /* 중복 검사 생략 (사전 자체가 중복 제거됨) */
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

        /* 줄 단위로 파싱하여 해시셋에 삽입 */
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

            /* 줄바꿈 건너뛰기 */
            if (*eol == '\r') eol++;
            if (*eol == '\n') eol++;
            p = eol;
        }
        free(raw);
    }
    printf("사전 로드 완료: %d개 단어\n\n", dict.count);

    /* ── 2. 파일 읽기 (바이그램 학습용) ── */
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

    /* ── 3. 바이그램 카운트 (BOS/EOS 통합) ── */
    BigramTable bt;
    bigram_init(&bt);

    int prev_cp = -1;
    int i = offset;
    while (i < file_size) {
        int cp;
        int len = utf8_decode(buf + i, &cp);

        if (is_hangul(cp)) {
            if (prev_cp == -1)
                bigram_add(&bt, CP_BOS, cp);
            else
                bigram_add(&bt, prev_cp, cp);
            prev_cp = cp;
        } else {
            if (prev_cp != -1)
                bigram_add(&bt, prev_cp, CP_EOS);
            prev_cp = -1;
        }
        i += len;
    }
    if (prev_cp != -1)
        bigram_add(&bt, prev_cp, CP_EOS);
    free(buf);

    printf("바이그램 학습 완료\n\n");

    /* ── 4. 누적 확률 테이블 구축 ── */
    bigram_build_cumulative(&bt);

    /* ── 5. 단어 생성 + 사전 검증 ── */
    FILE *fout = fopen("output_bigram_verify.txt", "w");
    if (!fout) {
        fprintf(stderr, "output_bigram_verify.txt를 열 수 없습니다.\n");
        return 1;
    }
    /* UTF-8 BOM */
    fputc(0xEF, fout); fputc(0xBB, fout); fputc(0xBF, fout);

    int hit = 0;

    printf("생성된 단어 (%d개):\n", GEN_WORDS);
    printf("-------------------------------\n");

    for (int w = 0; w < GEN_WORDS; w++) {
        /* 단어를 버퍼에 모은다 */
        char word_buf[256];
        int word_len = 0;

        int current = CP_BOS;
        for (;;) {
            BigramRow *row = bigram_find_row(&bt, current);
            if (!row || row->num_nexts == 0) break;

            int next = sample_from_cumulative(row->nexts, row->cumulative, row->num_nexts);
            if (next == CP_EOS) break;

            int enc_len = utf8_encode(next, word_buf + word_len);
            word_len += enc_len;
            if (word_len > 240) break;  /* 안전 가드 */
            current = next;
        }
        word_buf[word_len] = 0;

        /* 사전 대조 */
        int found = dict_contains(&dict, word_buf);
        if (found) hit++;

        const char *mark = found ? "O" : "X";

        printf("%2d  %-20s %s\n", w + 1, word_buf, mark);
        fprintf(fout, "%d\t%s\t%s\n", w + 1, word_buf, mark);
    }

    printf("-------------------------------\n");
    printf("적중: %d / %d (%.1f%%)\n", hit, GEN_WORDS,
           100.0 * hit / GEN_WORDS);

    fprintf(fout, "\n적중: %d / %d (%.1f%%)\n", hit, GEN_WORDS,
            100.0 * hit / GEN_WORDS);
    fclose(fout);

    printf("\n결과가 output_bigram_verify.txt에 저장되었습니다.\n");

    bigram_free(&bt);
    dict_free(&dict);

    return 0;
}
