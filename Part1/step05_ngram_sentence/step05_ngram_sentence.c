/*
 * Step 5: 글자 단위 N-gram 문장 생성
 *
 * data_all.txt의 문장을 글자 단위 N-gram으로 학습하여 새 문장을 생성한다.
 * 공백, 쉼표, 마침표 등 모든 문자를 동등한 코드포인트 토큰으로 처리한다.
 * 각 줄(=문장)의 앞에 (N-1)개 BOS, 뒤에 EOS 1개를 패딩한다.
 *
 * 3-gram, 5-gram, 7-gram 세 가지를 순차적으로 학습·생성하여 비교한다.
 *
 *   3-gram: P(next | prev2, prev1)          — 직전 2글자
 *   5-gram: P(next | prev4, prev3, ..., prev1) — 직전 4글자
 *   7-gram: P(next | prev6, ..., prev1)     — 직전 6글자
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define CP_BOS  0x0002
#define CP_EOS  0x0001

#define MAX_ORDER   7       /* 최대 N (7-gram까지) */
#define GEN_COUNT   10      /* N별 생성할 문장 수 */
#define MAX_GEN_LEN 300     /* 생성 문장 최대 글자 수 (안전 제한) */

/* ── 자료구조 ── */

typedef struct {
    int codepoint;
    int count;
} Pair;

/*
 * NgramRow: 하나의 컨텍스트(N-1글자)에 대한 다음 글자 분포.
 * Step 4의 TrigramRow에서 (cp1, cp2)가 context[MAX_ORDER-1]로 일반화되었다.
 */
typedef struct {
    int context[MAX_ORDER - 1];
    int ctx_len;
    Pair *nexts;
    int num_nexts;
    int cap_nexts;
    int total;
    double *cumulative;
} NgramRow;

#define ROW_HASH_SIZE 2097143   /* 소수, ~2M — 7-gram의 컨텍스트 폭발 대비 */

typedef struct {
    NgramRow *rows;
    int num_rows;
    int cap_rows;
    int *bucket;
    int *chain;
} NgramTable;

/* ── UTF-8 유틸 (Step 4와 동일) ── */

static int utf8_decode(const unsigned char *buf, int *codepoint) {
    if (buf[0] < 0x80) {
        *codepoint = buf[0];
        return 1;
    } else if ((buf[0] & 0xE0) == 0xC0) {
        *codepoint = ((buf[0] & 0x1F) << 6) | (buf[1] & 0x3F);
        return 2;
    } else if ((buf[0] & 0xF0) == 0xE0) {
        *codepoint = ((buf[0] & 0x0F) << 12) | ((buf[1] & 0x3F) << 6) |
                     (buf[2] & 0x3F);
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

static void utf8_print_cp(FILE *fp, int cp) {
    char tmp[4];
    int len = utf8_encode(cp, tmp);
    fwrite(tmp, 1, len, fp);
}

/* ── N-gram 해시맵 ── */

/*
 * FNV-1a 해시를 컨텍스트 배열에 적용.
 * Step 4의 hash_pair(cp1,cp2) → hash_context(ctx[], len) 일반화.
 */
static unsigned int hash_context(const int *ctx, int len) {
    unsigned int h = 2166136261u;
    for (int i = 0; i < len; i++)
        h = (h ^ (unsigned int)ctx[i]) * 16777619u;
    return h % ROW_HASH_SIZE;
}

static int ctx_equal(const int *a, const int *b, int len) {
    for (int i = 0; i < len; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static void ngram_init(NgramTable *nt) {
    nt->num_rows = 0;
    nt->cap_rows = 16384;
    nt->rows = (NgramRow *)malloc(sizeof(NgramRow) * nt->cap_rows);
    nt->chain = (int *)malloc(sizeof(int) * nt->cap_rows);
    nt->bucket = (int *)malloc(sizeof(int) * ROW_HASH_SIZE);
    for (int i = 0; i < ROW_HASH_SIZE; i++)
        nt->bucket[i] = -1;
}

static NgramRow *ngram_get_row(NgramTable *nt, const int *ctx, int ctx_len) {
    unsigned int h = hash_context(ctx, ctx_len);
    int idx = nt->bucket[h];
    while (idx != -1) {
        if (nt->rows[idx].ctx_len == ctx_len &&
            ctx_equal(nt->rows[idx].context, ctx, ctx_len))
            return &nt->rows[idx];
        idx = nt->chain[idx];
    }
    /* 새 행 추가 */
    if (nt->num_rows == nt->cap_rows) {
        nt->cap_rows *= 2;
        nt->rows = (NgramRow *)realloc(nt->rows, sizeof(NgramRow) * nt->cap_rows);
        nt->chain = (int *)realloc(nt->chain, sizeof(int) * nt->cap_rows);
    }
    idx = nt->num_rows++;
    memcpy(nt->rows[idx].context, ctx, sizeof(int) * ctx_len);
    nt->rows[idx].ctx_len = ctx_len;
    nt->rows[idx].nexts = NULL;
    nt->rows[idx].num_nexts = 0;
    nt->rows[idx].cap_nexts = 0;
    nt->rows[idx].total = 0;
    nt->rows[idx].cumulative = NULL;
    nt->chain[idx] = nt->bucket[h];
    nt->bucket[h] = idx;
    return &nt->rows[idx];
}

static void ngram_add(NgramTable *nt, const int *ctx, int ctx_len, int next_cp) {
    NgramRow *row = ngram_get_row(nt, ctx, ctx_len);
    for (int i = 0; i < row->num_nexts; i++) {
        if (row->nexts[i].codepoint == next_cp) {
            row->nexts[i].count++;
            row->total++;
            return;
        }
    }
    if (row->num_nexts == row->cap_nexts) {
        row->cap_nexts = row->cap_nexts == 0 ? 8 : row->cap_nexts * 2;
        row->nexts = (Pair *)realloc(row->nexts, sizeof(Pair) * row->cap_nexts);
    }
    row->nexts[row->num_nexts].codepoint = next_cp;
    row->nexts[row->num_nexts].count = 1;
    row->num_nexts++;
    row->total++;
}

static void ngram_build_cumulative(NgramTable *nt) {
    for (int i = 0; i < nt->num_rows; i++) {
        NgramRow *row = &nt->rows[i];
        if (row->num_nexts == 0) continue;
        row->cumulative = (double *)malloc(sizeof(double) * row->num_nexts);
        long long running = 0;
        for (int j = 0; j < row->num_nexts; j++) {
            running += row->nexts[j].count;
            row->cumulative[j] = (double)running / row->total;
        }
    }
}

static NgramRow *ngram_find_row(NgramTable *nt, const int *ctx, int ctx_len) {
    unsigned int h = hash_context(ctx, ctx_len);
    int idx = nt->bucket[h];
    while (idx != -1) {
        if (nt->rows[idx].ctx_len == ctx_len &&
            ctx_equal(nt->rows[idx].context, ctx, ctx_len))
            return &nt->rows[idx];
        idx = nt->chain[idx];
    }
    return NULL;
}

static void ngram_free(NgramTable *nt) {
    for (int i = 0; i < nt->num_rows; i++) {
        free(nt->rows[i].nexts);
        free(nt->rows[i].cumulative);
    }
    free(nt->rows);
    free(nt->chain);
    free(nt->bucket);
}

/* ── 샘플링 (Step 4와 동일) ── */

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

/* ── 문장 데이터 ── */

typedef struct {
    int *cps;
    int len;
} Sentence;

static Sentence *read_sentences(const char *path, int *out_count) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *raw = (unsigned char *)malloc(fsize + 1);
    fread(raw, 1, fsize, fp);
    raw[fsize] = 0;
    fclose(fp);

    int start = 0;
    if (fsize >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF)
        start = 3;  /* BOM 건너뛰기 */

    /* 줄 수 카운트 */
    int line_count = 0;
    for (long i = start; i < fsize; i++)
        if (raw[i] == '\n') line_count++;
    if (fsize > start && raw[fsize - 1] != '\n') line_count++;

    Sentence *sents = (Sentence *)malloc(sizeof(Sentence) * (line_count + 1));
    int sent_idx = 0;

    int i = start;
    while (i < fsize) {
        int cap = 256;
        int *cps = (int *)malloc(sizeof(int) * cap);
        int len = 0;

        while (i < fsize && raw[i] != '\n' && raw[i] != '\r') {
            int cp;
            int bytes = utf8_decode(raw + i, &cp);
            if (cp > 0) {
                if (len == cap) {
                    cap *= 2;
                    cps = (int *)realloc(cps, sizeof(int) * cap);
                }
                cps[len++] = cp;
            }
            i += bytes;
        }
        if (i < fsize && raw[i] == '\r') i++;
        if (i < fsize && raw[i] == '\n') i++;

        if (len > 0) {
            sents[sent_idx].cps = cps;
            sents[sent_idx].len = len;
            sent_idx++;
        } else {
            free(cps);
        }
    }

    free(raw);
    *out_count = sent_idx;
    return sents;
}

static void free_sentences(Sentence *sents, int count) {
    for (int i = 0; i < count; i++)
        free(sents[i].cps);
    free(sents);
}

/* ── N-gram 학습 ── */

/*
 * 문장 "가 나"에 대해 3-gram 학습 예시:
 *   패딩: [BOS, BOS, '가', ' ', '나', EOS]
 *   슬라이딩 윈도우:
 *     (BOS, BOS) → '가'
 *     (BOS, '가') → ' '
 *     ('가', ' ') → '나'
 *     (' ', '나') → EOS
 */
static void train_ngram(NgramTable *nt, Sentence *sents, int sent_count, int N) {
    int ctx_len = N - 1;

    for (int s = 0; s < sent_count; s++) {
        Sentence *sent = &sents[s];

        int padded_len = ctx_len + sent->len + 1;
        int *padded = (int *)malloc(sizeof(int) * padded_len);

        /* (N-1)개 BOS + 문장 글자들 + EOS */
        for (int i = 0; i < ctx_len; i++)
            padded[i] = CP_BOS;
        for (int i = 0; i < sent->len; i++)
            padded[ctx_len + i] = sent->cps[i];
        padded[ctx_len + sent->len] = CP_EOS;

        /* 슬라이딩 윈도우: context[0..ctx_len-1] → next */
        for (int i = 0; i <= padded_len - N; i++) {
            ngram_add(nt, padded + i, ctx_len, padded[i + ctx_len]);
        }

        free(padded);
    }
}

/* ── 문장 생성 ── */

static void generate_sentence(NgramTable *nt, int N, FILE *out1, FILE *out2) {
    int ctx_len = N - 1;
    int context[MAX_ORDER - 1];

    for (int i = 0; i < ctx_len; i++)
        context[i] = CP_BOS;

    int gen_count = 0;
    while (gen_count < MAX_GEN_LEN) {
        NgramRow *row = ngram_find_row(nt, context, ctx_len);
        if (!row || row->num_nexts == 0) break;

        int next = sample_from_cumulative(row->nexts, row->cumulative,
                                          row->num_nexts);
        if (next == CP_EOS) break;

        utf8_print_cp(out1, next);
        if (out2) utf8_print_cp(out2, next);

        /* 컨텍스트 왼쪽 시프트: [a,b,c,d] → [b,c,d,next] */
        for (int i = 0; i < ctx_len - 1; i++)
            context[i] = context[i + 1];
        context[ctx_len - 1] = next;
        gen_count++;
    }
}

/* ── main ── */

int main(void)
{
    SetConsoleOutputCP(65001);
    srand((unsigned int)time(NULL));

    /* ── 1. 데이터 로드 ── */
    int sent_count = 0;
    Sentence *sents = read_sentences("../data/data_all.txt", &sent_count);
    if (!sents) {
        fprintf(stderr, "../data/data_all.txt\xEB\xA5\xBC \xEC\x97\xB4 \xEC\x88\x98 "
                        "\xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4.\n");
        return 1;
    }

    long long total_chars = 0;
    for (int i = 0; i < sent_count; i++)
        total_chars += sents[i].len;
    printf("=== Step 5: N-gram \xEB\xAC\xB8\xEC\x9E\xA5 \xEC\x83\x9D\xEC\x84\xB1 ===\n");
    printf("\xEB\x8D\xB0\xEC\x9D\xB4\xED\x84\xB0: %d\xEA\xB0\x9C \xEB\xAC\xB8\xEC\x9E\xA5, "
           "\xEC\xB4\x9D %lld\xEA\xB8\x80\xEC\x9E\x90\n\n", sent_count, total_chars);

    /* ── 2. 3-gram, 5-gram, 7-gram 순차 실행 ── */
    int orders[] = {3, 5, 7};
    const char *filenames[] = {
        "output_ngram_n3.txt",
        "output_ngram_n5.txt",
        "output_ngram_n7.txt"
    };

    for (int oi = 0; oi < 3; oi++) {
        int N = orders[oi];

        printf("========================================\n");
        printf("  %d-gram \xED\x95\x99\xEC\x8A\xB5 \xEC\xA4\x91...\n", N);

        NgramTable nt;
        ngram_init(&nt);
        train_ngram(&nt, sents, sent_count, N);
        ngram_build_cumulative(&nt);

        printf("  \xEA\xB3\xA0\xEC\x9C\xA0 \xEC\xBB\xA8\xED\x85\x8D\xEC\x8A\xA4\xED\x8A\xB8 "
               "\xEC\x88\x98: %d\n", nt.num_rows);
        printf("========================================\n");
        printf("  \xEC\x83\x9D\xEC\x84\xB1 \xEA\xB2\xB0\xEA\xB3\xBC (%d\xEA\xB0\x9C):\n\n",
               GEN_COUNT);

        FILE *fout = fopen(filenames[oi], "w");
        if (fout) {
            fputc(0xEF, fout); fputc(0xBB, fout); fputc(0xBF, fout);
            fprintf(fout, "%d-gram \xEB\xAC\xB8\xEC\x9E\xA5 \xEC\x83\x9D\xEC\x84\xB1 "
                          "\xEA\xB2\xB0\xEA\xB3\xBC\n", N);
            fprintf(fout, "\xEA\xB3\xA0\xEC\x9C\xA0 \xEC\xBB\xA8\xED\x85\x8D\xEC\x8A\xA4"
                          "\xED\x8A\xB8 \xEC\x88\x98: %d\n\n", nt.num_rows);
        }

        for (int g = 0; g < GEN_COUNT; g++) {
            printf("  [%2d] ", g + 1);
            if (fout) fprintf(fout, "[%2d] ", g + 1);

            generate_sentence(&nt, N, stdout, fout);

            printf("\n");
            if (fout) fprintf(fout, "\n");
        }

        printf("\n");
        if (fout) {
            fclose(fout);
            printf("  -> %s \xEC\xA0\x80\xEC\x9E\xA5 \xEC\x99\x84\xEB\xA3\x8C\n\n",
                   filenames[oi]);
        }

        ngram_free(&nt);
    }

    free_sentences(sents, sent_count);
    printf("\xEC\x99\x84\xEB\xA3\x8C.\n");
    return 0;
}
