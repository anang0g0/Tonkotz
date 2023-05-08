// date 20200331:xgcdの終了条件が２つになってしまう。ogcdとxgcdで使い分ける。
// date : 20200326 鍵生成が det と deta で高い確率で一致する。detaは並列処理。
// date 20200229 : pattarson algorithm implementation ver 1.0
//  xgcd & osqrtを追加した
// date      :  20160310,20191218,20191220,20191221,20191223,20191224,20191225,20191229,20191230
// auther    : the queer who thinking about cryptographic future
// code name :  一変数多項式演算ライブラリのつもり
// status    : now in debugging (ver 0.8)
//  0ベクトルが出ないように生成多項式のトレースチェックを入れた。
// date      :  20160310,20210419
// auther    : the queer who thinking about cryptographic future
// code name : OVP - One Variable Polynomial library with OpenMP friendly
// status    : now in debugging (ver 0.9)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <assert.h>
#include <execinfo.h>

// #include "8192.h"
// #include "4096.h"
#include "global.h"
#include "struct.h"
#include "debug.c"
#include "chash.c"
#include "lu.c"
#include "sha3.c"
#include "inv_mat.c"
// #include "golay.c"

#define TH omp_get_max_threads()

extern unsigned long xor128(void);
extern int mlt(int x, int y);
extern int mltn(int n, int a);
extern void makeS();

// #pragma omp threadprivate(mat)
// シンドロームのコピー
unsigned short sy[K] = {0};

// Goppa多項式
static unsigned short g[K + 1] = {0};
//{1,0,0,0,0};
//{1,0,1,0,5};
//{1};
//{1, 0, 0, 0, 1, 0, 1};
//

MTX BB = {0};
MTX H = {0};

unsigned int AA = 0, B = 0; //, C = 0, A2 = 0;

unsigned short oinv2(unsigned short a)
{
    if (a == 0)
        return 0;

    return N - gf[a] +1;
}

// 有限体の元の逆数
unsigned short
oinv(unsigned short a)
{
    int i;

    if (a == 0)
        return -1;

    for (i = 0; i < N; i++)
    {
        if (gf[mlt(fg[a], i)] == 1)
            return (unsigned short)i;
    }

    printf("no return \n");
    //  exit (1);
}

// aに何をかけたらbになるか
unsigned short
equ(unsigned short a, unsigned short b)
{
    int i;

    for (i = 0; i < N; i++)
    {
        if (gf[mlt(fg[a], fg[i])] == b)
            break;
    }
    return i;
}

// OP型からベクトル型への変換
vec o2v(OP f)
{
    vec a = {0};
    int i;

    for (i = 0; i < K * E; i++)
    {
        if (f.t[i].a > 0 && f.t[i].n < K * E)
            a.x[f.t[i].n] = f.t[i].a;
    }

    return a;
}

// ベクトル型からOP型への変換
OP v2o(vec a)
{
    int i, j = 0;
    OP f = {0};

    // #pragma omp parallel for
    for (i = 0; i < K * E; i++)
    {
        if (a.x[i] > 0)
        {
            f.t[j].n = i;
            f.t[j++].a = a.x[i];
        }
    }

    return f;
}

// 停止コマンド
void wait(void)
{
    int a;                                     // 読み込む変数はローカルに取るべし
    printf(" (enter number and hit return) "); // 何か表示させたほうが良いだろう
    fflush(stdout);                            // just in case
    scanf("%d", &a);                           // fgets(line, LINESIZE, stdin); という手も
}

// OP型を正規化する
OP conv(OP f)
{
    vec v = {0};
    OP g = {0};

    v = o2v(f);
    g = v2o(v);

    return g;
}

// 多項式を表示する（OP型）
void oprintpol(OP f)
{
    int i, n;

    f = conv(f);
    n = odeg(f);
    printf("n=%d\n", n);
    printf("terms=%d\n", terms(f));
    printf("deg=%d\n", odeg(f));

    for (i = n; i > -1; i--)
    {
        if (f.t[i].a > 0)
            printf("%ux^%u+", f.t[i].a, f.t[i].n);
    }
}

void op_print_raw(const OP f)
{
    puts("op_print_raw:");
    for (int i = 0; i < DEG; i++)
    {
        if (f.t[i].a > 0)
            printf("[%d] %ux^%u\n", i, f.t[i].a, f.t[i].n);
    }
}

bool op_verify(const OP f)
{
    bool end = false;
    unsigned short n_max = 0;
    for (int i = 0; i < DEG; i++)
    {
        if (end && (f.t[i].n != 0 || f.t[i].a != 0))
        {
            op_print_raw(f);
            printf("found data after end: i=%d\n", i);
            print_trace();
            fflush(stdout);
            return false;
        }
        if (f.t[i].a == 0)
        {
            end = true;
            continue;
        }
        if (f.t[i].n + 1 <= n_max)
        {
            op_print_raw(f);
            printf("found invalid order: i=%d\n", i);
            print_trace();
            fflush(stdout);
            return false;
        }
        n_max = f.t[i].n + 1;
    }
    return true;
}

// 20200816:正規化したいところだがうまく行かない
// 多項式の足し算
OP oadd(OP f, OP g)
{
    // f = conv(f);
    // g = conv(g);
    assert(op_verify(f));
    assert(op_verify(g));

    vec a = {0}, b = {0}, c = {0};
    int i, j, k, l = 0;
    OP h = {0}, f2 = {0}, g2 = {0};

    a = o2v(f);
    b = o2v(g);

    // k=deg(o2v(f));
    // l=deg(o2v(g));

    for (i = 0; i < DEG; i++)
    {
        c.x[i] = a.x[i] ^ b.x[i];
        // h.t[i].a=f.t[i].a^g.t[i].a;
    }
    h = v2o(c);
    // h=conv(h);
    assert(op_verify(h));
    return h;
}

// 項の順序を降順に揃える
OP sort(OP f)
{
    oterm o = {0};
    int i, j, k;

    k = terms(f);
    for (i = 0; i < k + 1; ++i)
    {
        for (j = i + 1; j < k + 1; ++j)
        {
            if (f.t[i].n > f.t[j].n)
            {
                o = f.t[i];
                f.t[i] = f.t[j];
                f.t[j] = o;
            }
        }
    }

    return f;
}

// 多項式を項ずつ掛ける
vec oterml(vec f, oterm t)
{
    // f = conv(f);
    // assert(op_verify(f));
    int i, k, j;
    vec h = {0};
    vec test;
    unsigned int n;

    // f=conv(f);
    // k = deg (o2v(f));
    j = 0;
    for (i = 0; i < DEG; i++)
    {
        if (f.x[i] > 0)
            h.x[i + t.n] = gf[mlt(fg[f.x[i]], fg[t.a])];
    }

    // h = conv(h);
    // assert(op_verify(h));
    return h;
}

vec vadd(vec a, vec b)
{
    vec c = {0};
    int i, k;

    if (deg(a) >= deg(b))
    {
        k = deg(a) + 1;
    }
    else
    {

        k = deg(b) + 1;
    }
    for (i = 0; i < k; i++)
    {
        c.x[i] = a.x[i] ^ b.x[i];
    }
    // c.x[i] = a.x[i] ^ b.x[i];
    //  h = v2o (c);

    return c;
}

vec vmul(vec a, vec b)
{
    int i, j, k, l;
    vec c = {0};

    k = deg(a);
    l = deg(b);

    for (i = 0; i < k; i++)
    {
        for (j = 0; j < l; j++)
            if (a.x[i] > 0)
            {
                c.x[i + j] ^= gf[mlt(fg[a.x[i]], fg[b.x[j]])];
            }
    }

    return c;
}

// 多項式の掛け算
OP omul(OP f, OP g)
{
    // f = conv(f);
    // g = conv(g);
    assert(op_verify(f));
    assert(op_verify(g));
    int i, count = 0, k, l;
    oterm t = {0};
    vec h = {0}, e = {0}, r = {0};
    vec c = {0}, d;

    k = odeg(f);
    l = odeg(g);
    if (l > k)
    {
        k = l;
    }
    d = o2v(f);
    for (i = 0; i < k + 1; i++)
    {
        t = g.t[i];
        e = (oterml(d, t));
        h = vadd(h, e);
    }
    // assert(op_verify(h));
    return v2o(h);
}

// リーディグタームを抽出(default)
oterm LT(OP f)
{
    int i, k;
    oterm t = {0};

    // k = deg (o2v (f));
    for (i = 0; i < DEG; i++)
    {
        // printf("a=%d %d\n",f.t[i].a,f.t[i].n);
        if (f.t[i].a > 0)
        {
            t.n = f.t[i].n;
            t.a = f.t[i].a;
        }
    }

    return t;
}

// 多項式の最後の項を抽出
oterm LT2(OP f)
{
    int i, k;
    oterm t = {0};

    t.n = f.t[0].n;
    t.a = f.t[0].a;

    return t;
}

// 多項式を単行式で割る
oterm LTdiv(OP f, oterm t)
{
    oterm tt = {0}, s = {
                        0};

    tt = LT(f);
    if (tt.n < t.n)
    {
        s.n = 0;
        s.a = 0;
    }
    else if (tt.n == t.n)
    {
        s.n = 0;
        s.a = equ(t.a, tt.a);
    }
    else if (tt.n > t.n)
    {
        s.n = tt.n - t.n;
        s.a = equ(t.a, tt.a);
        // printf("%u\n",s.a);
    }
    else if (t.n == 0 && t.a > 0)
    {
        s.a = gf[mlt(fg[tt.a], oinv(t.a))];
        s.n = tt.n;
    }

    return s;
}

// モニック多項式にする
OP coeff(OP f, unsigned short d)
{
    int i, j, k;
    vec a, b;

    f = conv(f);
    k = odeg((f)) + 1;
    for (i = 0; i < k; i++)
        f.t[i].a = gf[mlt(fg[f.t[i].a], oinv(d))];

    return f;
}

// 多項式を表示する(default)
void printpol(OP a)
{
    int i, n;

    n = deg(o2v(a));

    // printf ("baka\n");
    assert(("baka\n", n >= 0));

    for (i = n; i > -1; i--)
    {
        if (a.t[i].a > 0)
        {
            // if(a.x[i]>1)
            printf("%u", a.t[i].a);
            if (i > 0)
                printf("x^%d", a.t[i].n);
            // if (i > 0)
            if (i > 0)
                printf("+");
        }
    }
    //  printf("\n");

    return;
}

// 多項式の剰余を取る
OP omod(OP f, OP g)
{
    int i = 0, j, n, k;
    OP h = {0}, e = {0};
    vec x, y;
    oterm a, b = {0}, c = {0};

    n = LT(g).n;

    //  assert (("baka^\n", LT (f).n != 0));

    //  assert (("baka(A)\n", LT (g).n != 0));

    if (LT(f).n < LT(g).n)
    {
        //    exit(1);
        return f;
    }

    // printf ("in omod\n");
    // exit(1);

    k = LT(g).n;
    b = LT(g);

    x = o2v(g);
    assert(("double baka\n", b.a != 0 && b.n != 0));
    while (LT(f).n > 0 && LT(g).n > 0)
    {

        c = LTdiv(f, b);
        h = v2o(oterml(x, c));
        f = oadd(f, h);
        if (odeg((f)) == 0 || odeg((g)) == 0)
        {
            //      printf("blake1\n");
            break;
        }

        if (c.n == 0 || b.n == 0)
            break;
    }

    return f;
}

// 多項式の商を取る
OP odiv(OP f, OP g)
{

    // f = conv(f);
    // g = conv(g);
    assert(op_verify(f));
    assert(op_verify(g));
    int i = 0, j, n, k;
    OP h = {0}, e = {0}, tt = {0};
    oterm a, b = {0}, c = {0};
    vec x, y;

    if (LT(f).n == 0 && LT(g).a == 0)
    {
        printf("baka^\n");
        // return f;
        exit(1);
    }
    if (LT(g).a == 0)
    {
        print_trace();
        exit(1);
    }
    if (LT(g).n == 0 && LT(g).a > 1)
        return coeff(f, LT(g).a);

    k = odeg(g);
    b = LT(g);
    if (b.a == 1 && b.n == 0)
        return f;
    if (b.a == 0 && b.n == 0)
    {
        printf("baka in odiv\n");
        exit(1);
    }
    if (odeg((f)) < odeg((g)))
    {
        return f;
        //  a=LT(f);
    }

    x = o2v(g);
    i = 0;
    while (LT(f).n > 0 && LT(g).n > 0)
    {
        c = LTdiv(f, b);
        assert(c.n < DEG);
        tt.t[i] = c;
        i++;

        h = v2o(oterml(x, c));

        f = oadd(f, h);
        if (odeg((f)) == 0 || odeg((g)) == 0)
        {
            // printf ("blake2\n");
            break;
        }

        if (c.n == 0)
            break;
    }

    // tt は逆順に入ってるので入れ替える
    OP ret = {0};
    int tt_terms = terms(tt);
    for (i = 0; i < tt_terms; i++)
    {
        ret.t[i] = tt.t[tt_terms - i - 1];
    }
    ret = conv(ret);
    assert(op_verify(ret));
    return ret;
}

// 多項式のべき乗
OP opow(OP f, int n)
{
    int i;
    OP g = {0};

    g = f;

    for (i = 1; i < n; i++)
        g = omul(g, f);

    return g;
}

// 多項式のべき乗余
OP opowmod(OP f, OP mod, int n)
{
    int i, j = 0;

    // 繰り返し２乗法
    for (i = 1; i < n + 1; i++)
    {
        f = omul(f, f);
        if (odeg(f) > odeg(mod))
            f = omod(f, mod);
    }

    return f;
}

// 多項式の代入値
unsigned short
trace(OP f, unsigned short x)
{
    int i, d;
    unsigned short u = 0;

    d = deg(o2v(f));

    for (i = 0; i < d + 1; i++)
    {
        u ^= gf[mlt(fg[f.t[i].a], mltn(f.t[i].n, fg[x]))];
    }

    return u;
}

// invert of polynomial
OP inv(OP a, OP n)
{
    OP d = {0}, x = {0}, s = {0}, q = {0}, r = {0}, t = {0}, u = {0}, v = {0}, w = {0}, tt = {0}, gcd = {0}, tmp = {0};
    oterm b = {0};
    vec vv = {0}, xx = {
                      0};

    if (odeg((a)) > odeg((n)))
    {
        tmp = a;
        a = n;
        n = tmp;
        printf("baka_i\n");
        // exit (1);
    }
    if (LT(a).a == 0)
    {
        printf(" a ga 0\n");
        exit(1);
    }

    tt = n;

    d = n;
    x.t[0].a = 0;
    x.t[0].n = 0;
    s.t[0].a = 1;
    s.t[0].n = 0;
    while (odeg((a)) > 0)
    {
        if (odeg((a)) > 0)
            r = omod(d, a);
        if (LT(a).a == 0)
            break;
        if (LT(a).a > 0)
            q = odiv(d, a);

        d = a;
        a = r;
        t = oadd(x, omul(q, s));
        ////printpol (o2v (a));
        // printf ("\nin roop a==================%d\n", odeg ((a)));
        // printf ("\n");

        x = s;
        s = t;
    }
    // exit(1);
    //  if(LT(a).a>0){
    d = a;
    a = r;
    ////printpol (o2v (a));
    // printf ("\nin roop a|==================%d\n", odeg ((a)));
    // printf ("\n");

    x = s;
    s = t;

    ////printpol (o2v (d));
    // printf ("\nout1================\n");
    gcd = d; // $\gcd(a, n)$
    printpol(gcd);
    printf(" =========gcd\n");
    // exit(1);
    // printf ("\n");
    ////printpol (o2v (n));
    // printf ("\n");
    // printf ("out2===============\n");

    printf("before odiv\n");
    // w=tt;

    b = LT(w);
    ////printpol (o2v (w));
    // printf ("\nw=======%d %d\n", b.a, b.n);
    // w=tt;
    v = oadd(x, n);
    ////printpol (o2v (v));
    // printf ("\n");
    /*
     if (LT (v).a == 0)
     {
     printf ("v=============0\n");
     }
     printf ("d==============\n");
   */
    //  } //end of a>0
    w = tt;
    ////printpol (o2v (v));
    // printf ("\n");
    // printf ("ss==============\n");
    //        exit(1);
    //  if(odeg((w))>0)
    if (LT(v).n > 0 && LT(w).n > 0)
    {
        u = omod(v, w);
    }
    else
    {
        // printpol (o2v (v));
        printf(" v===========\n");
        // printpol (o2v (x));
        printf(" x==0?\n");
        // printpol (o2v (n));
        printf(" n==0?\n");

        exit(1);
    }
    // caution !!
    if (LT(u).a > 0 && LT(d).a > 0)
    {
        u = odiv(u, d);
    }

    if (LT(u).a == 0 || LT(d).a == 0)
    {
        printf("inv div u or d==0\n");
        // exit(1);
    }
    // u=coeff(u,d.t[0].a);
    ////printpol (o2v (u));
    // printf ("\nu==================\n");
    if (LT(u).a == 0)
    {
        printf("no return at u==0\n");
        exit(1);
    }

    return u;
}

unsigned short
v2a(oterm a)
{
    int i, j;

    if (a.a == 0)
        return 0;

    // printf("aa=%d\n",a.a);
    for (j = 0; j < M; j++)
    {
        if (gf[j] == a.a && a.a > 0)
        {
            // printf("j==%d\n",j);
            return j - 1;
        }
    }
}

void printsage(vec a)
{
    int i, j, k;
    oterm b;

    printf("poly=");
    for (i = 0; i < DEG; i++)
    {
        if (a.x[i] > 0)
        {
            b.a = a.x[i];
            b.n = i;
            j = v2a(b);
            // printf("%d,==ba\n",b.a);
            // printf ("X**%d+", i); //for GF2
            printf("B('a^%d')*X**%d+", j, i); // for GF(2^m)
        }
    }
}

OP gcd(OP a, OP b)
{
    OP r = {0}, h = {0}, tmp = {0};

    h.t[0].a = 1;
    h.t[0].n = 0;

    if (odeg(a) < odeg(b))
    {
        tmp = a;
        a = b;
        b = tmp;
    }
    /*
  printpol((a));
  printf(" ========f\n");
  printpol((b));
  printf(" ========g\n");
*/
    /* 自然数 a > b を確認・入替 */
    if (odeg(a) < odeg(b))
    {
        tmp = a;
        a = b;
        b = tmp;
    }

    r = omod(a, b);
    while (odeg(r) > 0)
    {
        a = b;
        b = r;
        r = omod(a, b);
        if (LT(r).a == 0)
            return b;
    }

    if (LT(r).a == 0)
    {
        return b;
    }
    else
    {
        // if(LT(r).a>0)
        return h;
    }
}

// gcd for pattarson
OP zgcd(OP a, OP n)
{
    OP d = {0}, x = {0}, s = {0}, q = {0}, r = {0}, t = {0}, u = {0}, v = {0}, w = {0}, tt = {0}, gcd = {0}, rt = {0};
    oterm b = {0};
    vec vv = {0}, xx = {
                      0};

    if (odeg(a) > odeg(n))
    {
        rt = a;
        a = n;
        n = rt;
        printf("big is good\n");
        // exit (1);
    }
    if (LT(a).a == 0)
    {
        printf(" a ga 0\n");
        exit(1);
    }

    tt = n;

    d = n;
    x.t[0].a = 0;
    x.t[0].n = 0;
    s.t[0].a = 1;
    s.t[0].n = 0;
    while (LT(a).n > T)
    {

        r = omod(d, a);
        q = odiv(d, a);

        d = a;
        a = r;
        t = oadd(x, omul(q, s));

        x = s;
        s = t;
    }

    d = a;
    a = r;

    x = s;
    s = t;
    gcd = d; // $\gcd(a, n)$

    printpol((x));
    printf(" =======x\n");
    printpol((a));
    printf(" =======a\n");
    printpol((s));
    printf(" =======s\n");
    printpol((r));
    printf(" =======r\n");

    return x;
}

// GCD for decode
OP ogcd(OP xx, OP yy)
{
    OP tt;

    while (odeg(yy) > T - 1)
    {
        tt = omod(xx, yy);
        xx = yy;
        yy = tt;
    }

    printpol((yy));
    printf(" =========yy\n");
    printpol((tt));
    printf(" =========tt\n");

    return tt;
}

// test gcd
OP agcd(OP xx, OP yy)
{
    OP tt = {0}, tmp;

    if (deg(o2v(xx)) < deg(o2v(yy)))
    {
        tmp = xx;
        xx = yy;
        yy = tmp;
    }
    tt = omod(xx, yy);
    while (LT(tt).n > 0)
    {
        xx = yy;
        yy = tt;
        tt = omod(xx, yy);
    }

    return yy;
}

// error locater for decode
OP vx(OP f, OP g)
{
    OP h = {0}, ww = {
                    0};
    OP v[K] = {0}, vv = {
                       0};
    oterm a, b;
    int i, j;

    v[0].t[0].a = 0;
    v[0].t[1].n = 0;
    v[1].t[0].a = 1;
    v[1].t[1].n = 0;

    i = 0;

    while (1)
    {
        if (odeg((g)) == 0)
            break;
        h = omod(f, g);
        if (LT(g).a == 0)
            break;
        ww = odiv(f, g);
        v[i + 2] = oadd(v[i], omul(ww, v[i + 1]));
        f = g;
        g = h;

        vv = v[i + 2];

        if (odeg((vv)) == T)
            break;
        i++;
    }

    return vv;
}

// error locater for decode
OP sabun(OP f, OP g)
{
    OP h = {0}, ww = {
                    0};
    OP v[K] = {0}, vv = {
                       0};
    oterm a, b;
    int i, j;

    v[0].t[0].a = 0;
    v[0].t[1].n = 0;
    v[1].t[0].a = 1;
    v[1].t[1].n = 0;

    i = 0;

    while (1)
    {
        if (odeg((g)) == 0)
            break;
        h = omod(f, g);
        if (LT(g).a == 0)
            break;
        ww = odiv(f, g);
        v[i + 2] = oadd(v[i], omul(ww, v[i + 1]));
        f = g;
        g = h;

        vv = v[i + 2];

        if (odeg((vv)) == T * 2)
            break;
        i++;
    }

    return vv;
}

// 最終の項までの距離
int distance(OP f)
{
    int i, j = 0;

    for (i = 0; i < DEG; i++)
    {
        if (f.t[i].a > 0)
            j = i;
    }

    return j;
}

// 項の数
int terms(OP f)
{
    int i, count = 0;

    for (i = 0; i < DEG; i++)
        if (f.t[i].a > 0)
            count++;

    return count;
}

// 多項式の次数(degのOP型)
int odeg(OP f)
{
    int i, j = 0, k;

    if (f.t[0].a == 0)
        return 0;

    // k=terms(f);
    for (i = 0; i < DEG; i++)
    {
        if (j < f.t[i].n && f.t[i].a > 0)
            j = f.t[i].n;
    }

    return j;
}

// ０多項式かどうかのチェック
unsigned char
chk(OP f)
{
    int i, flg = 0;
    vec x = {0};

    x = o2v(f);
    for (i = 0; i < DEG; i++)
    {
        if (x.x[i] > 0)
        {
            flg = 1;
            return 1;
        }
    }
    if (flg == 0)
        return 0;

    exit(1);
}

OP kof(unsigned short c, OP f)
{
    int i, j, k;
    vec b = {0}, h = {0};
    OP g = {0};

    c = fg[c];
    b = o2v(f);
    k = deg(b);
    for (i = 0; i < k + 1; i++)
    {
        h.x[i] = gf[mlt(c, fg[b.x[i]])];
    }
    g = v2o(h);

    return g;
}

// 拡張ユークリッドアルゴリズム
EX xgcd(OP f, OP g)
{
    OP h = {0}, ww = {0}, *v, *u;
    oterm a, b;
    int i = 0, j, k, flg = 0, m = odeg(f);
    EX e = {0}, ee = {0};

    v = (OP *)malloc(sizeof(OP) * (DEG));
    u = (OP *)malloc(sizeof(OP) * (DEG));
    memset(v, 0, sizeof(OP) * DEG);
    memset(u, 0, sizeof(OP) * DEG);

    u[0].t[0].a = 1;
    u[0].t[0].n = 0;
    u[1].t[0].a = 0;
    u[1].t[0].n = 0;
    u[2].t[0].a = 1;
    u[2].t[0].n = 0;

    v[0].t[0].a = 0;
    v[0].t[0].n = 0;
    v[1].t[0].a = 1;
    v[1].t[0].n = 0;

    printpol((f));
    printf(" f===============\n");
    printpol((g));
    printf(" s===============\n");
    //  exit(1);

    k = 0;
    i = 0;
    while (LT(g).n > 0)
    // for (i = 0; i < T * 2; i++)
    {

        if ((odeg((g)) == 0 && LT(g).a == 0) || odeg((f)) == 0)
        {
            flg = 1;
            printf("v[%d]=%d skipped deg(g)==0!\n", i, odeg((v[i])));
            printpol((g));
            printf(" g========\n");
            e.d = f;
            e.v = v[i];
            e.u = u[i];

            free(v);
            free(u);
            // wait();
            return e;

            // exit (1);
            // return e;

            // break;
        }

        if (LT(g).n > 0)
            h = omod(f, g);
        printpol((h));
        printf(" ===hh\n");
        if (LT(g).a > 0)
            ww = odiv(f, g);
        printpol((ww));
        printf(" ===ww\n");

        v[i + 2] = oadd(v[i], omul(ww, v[i + 1]));
        u[i + 2] = oadd(u[i], omul(ww, u[i + 1]));
        printf("i+1=%d %d %d g=%d\n", i + 1, odeg((v[i + 2])), T - 1, odeg((g)));
        printpol((v[i + 2]));
        printf(" ==vvv\n");
        f = g;
        g = h;

        if (odeg(v[i + 2]) == m - 1)
        {
            // printf("vaka\n");
            // wait();
            e.d = f;
            e.v = kof(gf[oinv(LT(h).a)], v[i + 2]);
            e.u = u[i + 2];

            free(v);
            free(u);
            // wait();
            return e;
        }
        i++;
    }

    // printf ("i=%d\n", i);
    // wait();
    // oprintpol ((v[i]));
    printf("deg(v)=%d\n", odeg((v[i + 2])));
    printf(" v=============\n");
    printf("deg(u)=%d\n", odeg((u[i])));
    // printpol (o2v (u[i]));
    printf(" u=============\n");
    printf("deg(f)=%d\n", odeg((f)));
    printf(" f=============\n");
    // exit(1);
    //   if(deg(v[i])==T-1){
    e.d = f;
    e.v = kof(gf[oinv(LT(h).a)], v[i + 2]);
    e.u = u[i + 2];

    free(v);
    free(u);
    printf("end of fnc\n");
    //  wait ();

    return e;
}

OP init_pol(OP f)
{
    int i;

    for (i = 0; i < DEG; i++)
    {
        f.t[i].a = 0;
        f.t[i].n = 0;
    }

    return f;
}

// ランダム多項式の生成
static void
ginit(void)
{
    int j, count = 0, k = 0;
    unsigned short gg[K + 1] = {0};

    printf("in ginit\n");

    g[K] = 1;          // xor128();
    g[0] = rand() % N; // or N
    k = rand() % (K - 1);
    if (k > 0)
    {
        while (count < k)
        {
            printf("in whule\n");
            j = rand() % (K);
            if (j < K && j > 0 && g[j] == 0)
            {
                g[j] = rand() % N; // or N;
                count++;
            }
        }
    }

    for (j = 0; j < K + 1; j++)
        gg[j] = g[K - j];

    memcpy(g, gg, sizeof(g));
}

unsigned short
b2B(unsigned short b[E])
{
    int i;
    unsigned short a = 0;

    for (i = E - 1; i > -1; i--)
        a ^= (b[E - i - 1] << i);

    return a;
}

// 多項式の次数(default)
int deg(vec a)
{
    int i, n = 0, flg = 0;

    // #pragma omp parallel for
    for (i = 0; i < DEG; i++)
    {
        if (a.x[i] > 0)
        {
            n = i;
            flg = 1;
        }
    }
    if (flg == 0)
        return 0;

    return n;
}

// 整数からベクトル型への変換
vec i2v(unsigned int n)
{
    vec v = {0};
    int i = 0;

    while (n > 0)
    {
        v.x[i++] = n % 2;
        n = (n >> 1);
    }

    return v;
}

// ベクトル型から整数への変換
unsigned int
v2i(vec v)
{
    unsigned int d = 0, i, e = 0;

    for (i = 0; i < deg(v) + 1; i++)
    {
        e = v.x[i];
        d ^= (e << i);
    }

    return d;
}

// 配列からベクトル表現の多項式へ変換する
vec Setvec(int n)
{
    int i;
    vec v = {0};

    for (i = 0; i < n; i++)
    {
        v.x[n - 1 - i] = c[i];
    }

    return v;
}

void printvec(vec v)
{
    int i, j;

    for (i = 0; i < deg(v) + 1; i++)
    {
        // if (v.x[i] > 0)
        printf("%d:%d\n", i, v.x[i]);
    }
}

// 整数のべき乗
unsigned int
ipow(unsigned int q, unsigned int u)
{
    unsigned int i, m = 1;

    for (i = 0; i < u; i++)
        m *= q;

    printf("in ipow====%d\n", m);

    return m;
}

OP ww[T] = {0};

// chen探索
vec chen(OP f)
{
    vec e = {0};
    int i, count = 0, n, x = 0;
    unsigned short z;

    n = odeg((f));
    // exit(1);
    // #pragma omp parallel for private(i)
    for (x = 0; x < N; x++)
    {
        z = 0;
        // #pragma omp parallel for reduction (^:z)
        for (i = 0; i < n + 1; i++)
        {
            if (f.t[i].a > 0)
                z ^= gf[mlt(mltn(f.t[i].n, fg[x]), fg[f.t[i].a])];
        }
        if (z == 0)
        {
            e.x[count] = x;
            count++;
            printf("%d\n", x);
        }
    }
    // printpol(e);
    // printf(" ==eee!\n");
    // exit(1);

    return e;
}

int oequ(OP f, OP g)
{
    vec v, x;
    int i, flg = 0;

    v = o2v(f);
    x = o2v(g);
    for (i = 0; i < DEG; i++)
    {
        if (v.x[i] != x.x[i])
            return -1;
    }

    return 0;
}

// GF(2^m) then set m in this function.
int ben_or(OP f)
{
    int i, n, flg = 0;
    OP s = {0}, u = {0}, r = {0};
    vec v = {0}, x = {0};
    // if GF(8192) is 2^m and m==13 or if GF(4096) and m==12 if GF(16384) is testing
    int m = E;
    // m=12 as a for GF(4096)=2^12 defined @ gloal.h or here,for example m=4 and GF(16)

    v.x[1] = 1;
    s = v2o(v);
    r = s;
    n = deg(o2v(f));

    if (n == 0)
        return -1;

    i = 0;

    // r(x)^{q^i} square pow mod
    while (i < n / 2 + 1)
    {
        flg = 1;
        // irreducible over GH(8192) 2^13
        r = opowmod(r, f, m);

        // irreducible over GF2
        // r=omod(opow(r,2),f);

        u = oadd(r, s);
        if (deg(o2v(u)) == 0 && LT(u).a == 0)
            return -1;
        if (deg(o2v(u)) == 0 && LT(u).a == 1)
        {
            i++;
            flg = 0;
        }
        if (deg(o2v(u)) > 0)
            u = gcd(f, u);

        if (deg(o2v(u)) > 0)
            return -1;

        if (flg == 1)
            i++;
    }

    return 0;
}

// 配列の値を係数として多項式に設定する
OP setpol(unsigned short f[], int n)
{
    OP g;
    vec a;
    int i;

    memset(c, 0, sizeof(c));
    memcpy(c, f, 2 * n);
    a = Setvec(n);

    g = v2o(a);

    return g;
}

// バイナリ型パリティチェック行列を生成する
MTX bdet()
{
    int i, j, k, l;
    unsigned char dd[E * K] = {0};
    FILE *ff;
    MTX R = {0};

    // ff = fopen("Hb.key", "wb");

    for (i = 0; i < N; i++)
    {
        for (j = 0; j < K; j++)
        {
            l = mat[i][j];
            // #pragma omp parallel for
            for (k = 0; k < E; k++)
            {
                R.x[i][j * E + k] = l % 2;
                l = (l >> 1);
            }
        }
    }

    for (i = 0; i < N; i++)
    {
        // #pragma omp parallel for
        for (j = 0; j < E * K; j++)
        {
            printf("%d,", R.x[i][j]);
            // dd[j] = BH[j][i];
        }
        // fwrite(dd, 1, E * K, ff);
        printf("\n");
    }

    // fclose(ff);
    return R;
}

MTX bd2()
{
    int i, j, k, l;
    unsigned char dd[E * K] = {0};
    FILE *ff;
    vec v = {0};
    MTX R = {0};

    // ff = fopen("Hb.key", "wb");

    // memset(BB.z,0,sizeof(BB.z));
    for (i = 0; i < N; i++)
    {
        for (j = 0; j < K / 2 + 1; j++)
        {
            l = bm[i][j];
            printf("bm==%d %d\n", l, j);

            v = i2v(l);
            // #pragma omp parallel for
            for (k = 0; k < E; k++)
            {
                R.x[i][j * E + k] = v.x[k];
                // l = (l >> 1);
            }
        }
    }

    return R;
}

MT bin(unsigned short c[K])
{
    int i, j, count = 0;
    vec v = {0}, x = {0};
    MT n = {0};

    for (i = 0; i < K; i++)
    {
        memset(v.x, 0, sizeof(v.x));
        v = i2v(c[i]);
        for (j = 0; j < E; j++)
            n.v[j++] = v.x[i];
    }
    n.f = j;
    return n;
}

MT vin(unsigned short s[K * E])
{
    vec v = {0};
    int i, j, count = 0;
    MT n = {0};

    for (j = 0; j < K; j++)
    {
        for (i = 0; i < E; i++)
        {
            v.x[i] = s[count];
            count++;
        }
        n.v[i] = v2i(v);
    }
    n.f = K;

    return n;
}

// バイナリ型パリティチェック行列を生成する
void toBit(MTX L)
{
    int i, j, k, l;
    unsigned char dd[E * K] = {0};
    FILE *ff;

    // ff = fopen("Hb.key", "wb");

    for (i = 0; i < N; i++)
    {
        for (j = 0; j < K; j++)
        {
            l = L.x[i][j];
            printf("l=%d,", l);
            // #pragma omp parallel for
            for (k = 0; k < E; k++)
            {
                BB.x[i][j * E + k] = l % 2;
                l = (l >> 1);
            }
        }
        printf("\n");
    }
    // exit(1);
    /*
    for (i = 0; i < N; i++)
    {
        //#pragma omp parallel for
        for (j = 0; j < E * K; j++)
        {
            printf("%d,", BB.z[i][j]);
            //dd[j] = BH[j][i];
        }
        //fwrite(dd, 1, E * K, ff);
        printf("\n");
    }
*/
    // fclose(ff);
}

unsigned short HH[N][K];
unsigned short TE[N][K / 2 + 1];

MTX toByte(MTX SH, int kk)
{
    vec v = {0};
    int i, j, k, cnt;
    MTX R = {0};

    memset(HH, 0, sizeof(HH));
    printf("HH=");
    // exit(1);
    for (i = 0; i < N; i++)
    {
        printf("%d\n", i);
        // #pragma omp parallel for
        for (j = 0; j < kk; j++)
        {
            cnt = 0;
            for (k = j * E; k < j * E + E; k++)
                v.x[cnt++] = SH.x[i][k];

            HH[i][j] = v2i(v);
            R.x[i][j] = v2i(v);
            // printf("%d,", HH[i][j]);
            //= BH[j][i];
        }
        // fwrite(dd, 1, E * K, ff);
        // printf("\n");
    }
    printf("end of byte\n");
    // exit(1);
    // wait();

    return R;
}

// 秘密置換を生成する
void Pgen()
{
    unsigned int i, j;
    FILE *fp;

    // fp = fopen("P.key", "wb");
    for (i = 0; i < N; i++)
        P[i] = i;
    random_shuffle(P, SIZE_OF_ARRAY(P));
    // random_permutation(P);

    // for (i = 0; i < N; i++)
    //     P[i] = i;
    for (i = 0; i < N; i++)
        inv_P[P[i]] = i;
    // fwrite(P, 2, N, fp);
    // fclose(fp);
    /*
    for (i = 0; i < N; i++)
    printf ("%d,", P[i]);
    printf ("\n");
    exit(1);
    */
    // fp = fopen("inv_P.key", "wb");
    // fwrite(inv_P, 2, N, fp);
    // fclose(fp);
}

// ハッシュ１６進表示
static void
byte_to_hex(uint8_t b, char s[23])
{
    unsigned i = 1;
    s[0] = s[1] = '0';
    s[2] = '\0';
    while (b)
    {
        unsigned t = b & 0x0f;
        if (t < 10)
        {
            s[i] = '0' + t;
        }
        else
        {
            s[i] = 'a' + t - 10;
        }
        i--;
        b >>= 4;
    }
}

// 有限体の元の平方を計算する
int isqrt(unsigned short u)
{
    int i, j, k;

    for (i = 0; i < N; i++)
    {
        if (gf[mlt(i, i)] == u)
            return i;
    }

    printf("来ちゃいけないところに来ました\n");
    exit(1);
}

EX extgcd(OP a, OP b)
{

    OP s = {0}, sx = {0}, sy = {0}, t = {0}, tx = {0}, ty = {0}, tmp = {0};
    EX c = {0};

    if (odeg(b) > odeg(a))
    {
        tmp = a;
        a = b;
        b = tmp;
    }
    s = a;
    t = b;
    sx.t[0].a = 1;
    sx.t[0].n = 0;
    ty.t[0].a = 1;
    ty.t[0].n = 0;

    //  OP temp={0};
    tmp = omod(s, t);
    if (odeg(tmp) == 0)
    {
        c.d = t;
        c.v = tx;
        c.u = ty;
        printf("ppp\n");
        return c;
    }
    while (odeg(tmp) > 0)
    {
        printpol(((tmp)));
        printf(" ========omod\n");
        OP temp = odiv(s, t);
        OP u = oadd(s, omul(t, temp));
        OP ux = oadd(sx, omul(tx, temp));
        OP uy = oadd(sy, omul(ty, temp));
        /*
         */
        s = t;
        sx = tx;
        sy = ty;
        t = u;
        tx = ux;
        ty = uy;
        tmp = omod(s, t);
    }
    printpol((tmp));
    printf(" ========omod!\n");

    if (LT(tmp).a == 1)
    {
        c.d.t[0].a = 1;
        c.d.t[0].n = 0;
        // c.d=t;
        c.v = tx;
        c.u = ty;
        printf("bbb\n");
        return c;
    }
    if (LT(tmp).a == 0)
    {

        c.d = t;
        c.v = tx;
        c.u = ty;
        printf("ccc\n");

        return c;
    }
}

// 512bitの秘密鍵を暗号化
void encrypt(char buf[], unsigned char sk[64])
{
    const uint8_t *hash = {0};
    sha3_context c = {0};
    int image_size = 512, i;
    FILE *fp;
    //  unsigned short dd=0;

    printf("plain text=");
    for (i = 0; i < 64; i++)
        printf("%u,", sk[i]);
    printf("\n");
    //  puts(buf);
    // printf("\n");
    // exit(1);

    // scanf("%s",buf);
    sha3_Init256(&c);
    sha3_Update(&c, (char *)buf, strlen(buf));
    hash = sha3_Finalize(&c);

    // j=0;

    for (i = 0; i < 64; i++)
    {
        printf("%d", hash[i]);
        // char s[3];
        // byte_to_hex(hash[i],s);

        sk[i] ^= hash[i];
    }
    printf("\nencrypt sk=");
    for (i = 0; i < 64; i++)
        printf("%d,", sk[i]);
    printf("\n");

    fp = fopen("enc.sk", "wb");
    fwrite(sy, 2, K, fp);
    fwrite(sk, 1, 64, fp);
    fclose(fp);
}

void decrypt(OP w)
{
    FILE *fp;
    int i, j;
    unsigned char sk[64] = {0}, err[N] = {
                                    0};
    unsigned short buf[K] = {0}, tmp[K] = {
                                     0};
    OP f = {0}, r = {0};
    vec v = {0};
    const uint8_t *hash = {0};
    sha3_context c = {0};
    int image_size = 512;

    j = 0;
    fp = fopen("enc.sk", "rb");

    fread(tmp, 2, K, fp);
    fread(sk, 1, 64, fp);
    fclose(fp);

    for (i = 0; i < K; i++)
        buf[i] = tmp[K - i - 1];

    printf("in decrypt\n");
    f = setpol(buf, K);
    // v = pattarson(w, f);

    // elo(r);
    // exit(1);
    // v=o2v(r);

    j = 0;
    if (v.x[1] > 0 && v.x[0] == 0)
    {
        err[0] = 1;
        j++;
    }

    printf("j=%d\n", j);
    printf("after j\n");
    for (i = j; i < 2 * T; i++)
    {
        if (v.x[i] > 0 && v.x[i] < N)
        {
            err[v.x[i]] = 1;
        }
    }

    char buf0[8192] = {0}, buf1[10] = {
                               0};

    // #pragma omp parallel for
    for (i = 0; i < N; i++)
    {
        snprintf(buf1, 10, "%d", err[i]);
        strcat(buf0, buf1);
    }
    // puts (buf0);
    printf("vector=%d\n", strlen(buf0));
    // exit(1);
    printf("cipher sk2=");
    for (i = 0; i < 64; i++)
        printf("%u,", sk[i]);
    printf("\n");

    sha3_Init256(&c);
    sha3_Update(&c, (char *)buf0, strlen(buf0));
    hash = sha3_Finalize(&c);

    j = 0;
    printf("hash=");
    for (i = 0; i < 64; i++)
    {
        printf("%d", hash[i]);
        // char s[3];
        // byte_to_hex(hash[i],s);

        sk[i] ^= hash[i];
    }
    printf("\ndecript sk=");
    for (i = 0; i < 64; i++)
        printf("%u,", sk[i]);
    printf("\n");
    //  exit(1);

    return;
}

OP synd(unsigned short zz[], int kk)
{
    unsigned short syn[K] = {0}, s = 0;
    int i, j, t1;
    OP f = {0};

    printf("in synd2\n");

    for (i = 0; i < kk; i++)
    {
        syn[i] = 0;
        s = 0;
        // #pragma omp parallel num_threads(16)
        for (j = 0; j < M; j++)
        {
            s ^= gf[mlt(fg[zz[j]], fg[mat[j][i]])];
        }
        syn[i] = s;
        // printf ("syn%d,", syn[i]);
    }
    // printf ("\n");

    f = setpol(syn, kk);
    printpol((f));
    printf(" syn=============\n");
    //  exit(1);

    return f;
}

// 64バイト秘密鍵の暗号化と復号のテスト
void test(OP w, unsigned short zz[])
{
    int i;
    vec v = {0};
    const uint8_t *hash;
    sha3_context c;
    // int image_size=512;
    OP f = {0};
    FILE *fp;

    fp = fopen("aes.key", "rb");
    /*
     static char base64[] = {
     'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
     'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
     'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
     'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
     'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
     'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
     'w', 'x', 'y', 'z', '0', '1', '2', '3',
     '4', '5', '6', '7', '8', '9', '+', '/',
     };
   */
    char buf[8192] = {0}, buf1[10] = {
                              0};
    unsigned char sk[64] = {0};
    // unsigned short s[K]={0};
    // fread(sk,1,32,fp);
    for (i = 0; i < 64; i++)
        sk[i] = i + 1;

    for (i = 0; i < N; i++)
    {
        snprintf(buf1, 10, "%d", zz[i]);
        strcat(buf, buf1);
    }
    // puts (buf);
    printf("vector=%u\n", strlen(buf));
    // exit(1);

    printf("sk0=");
    for (i = 0; i < 64; i++)
        printf("%u,", sk[i]);
    printf("\n");
    // exit(1);

    f = synd(zz, K);
    v = o2v(f);
    // printf("v=");
    for (i = 0; i < K; i++)
    {
        sy[i] = v.x[i];
        printf("%d,", sy[i]);
    }
    printf("\n");

    encrypt(buf, sk);
    decrypt(w);

    sha3_Init256(&c);
    sha3_Update(&c, (char *)buf, strlen(buf));
    hash = sha3_Finalize(&c);
}

void readkey()
{
    FILE *fp, *fq;
    unsigned short dd[K * N] = {0};
    int i, j;

    // 鍵をファイルに書き込むためにはkey2を有効にしてください。
    //   key2(g);
    fp = fopen("sk.key", "rb");
    fread(g, 2, K + 1, fp);
    fclose(fp);
    // 固定した鍵を使いたい場合はファイルから読み込むようにしてください。
    fq = fopen("H.key", "rb");
    fread(dd, 2, K * N, fq);
    // #pragma omp parallel for
    for (i = 0; i < N; i++)
    {
        for (j = 0; j < K; j++)
            mat[i][j] = dd[K * i + j];
    }
    fclose(fq);
}

// OP sx={0},ty={0};

unsigned short vb[K * 2][N] = {0};
unsigned short gt[K * 2][K * 2] = {0};

void van(int kk)
{
    int i, j, k;

    printf("van der\n");

    for (i = 0; i < N; i++)
        vb[0][i] = 1;
    // #pragma omp parallel for private(i, j)
    for (i = 1; i < kk; i++)
    {
        for (j = 0; j < N; j++)
        {
            vb[i][j] = gf[mltn(i, fg[j])];
            printf("%d,", vb[i][j]);
        }
        printf("\n");
    }
}

void ogt(unsigned short pp[], int kk)
{
    int i, j, k;
    OP w = {0};

#pragma omp parallel for private(i, j)
    for (i = 0; i < kk; i++)
    {
        for (j = 0; j < kk - i; j++)
        {
            gt[i][j + i] = g[j];
        }
    }
    for (i = 0; i < kk; i++)
    {
        for (j = 0; j < kk; j++)
            printf("%d,", gt[i][j]);
        printf("\n");
    }
    // exit(1);
}

int isquad(OP w)
{
    int i, j, flg = 0;
    vec b = {0};

    b = o2v(w);
    for (i = 0; i < DEG; i++)
    {
        if (b.x[i] > 0 && i % 2 == 1)
            return 0;
    }

    return -1;
}

OP mkpol()
{
    int i, j, k, fail, flg, l, ii = 0;
    OP w = {0};

    do
    {
        fail = 0;
        j = 0;
        k = 0;
        flg = 0;
        l = 0;
        memset(g, 0, sizeof(g));
        // memset(ta, 0, sizeof(ta));
        memset(w.t, 0, sizeof(w));
        ginit();
        ii++;
        if (ii > 100)
        {
            printf("erro=%d\n", ii);
            exit(1);
        }

        for (i = 0; i < K; i++)
        {
            if (g[K - 1] > 0)
                flg = 1;
            if (i % 2 == 1 && g[i] > 0 && i < K)
                k++;
        }

        // 偶数項だけにならないようにする
        if ((k > 0 && flg == 0) || (k > 1 && flg == 1))
        // if(k>0)
        {
            w = setpol(g, K + 1);
            j = 1;
            // if(isquad(w)==-1)
            // exit(1);
        }
        // exit(1);

    } while (j == 0);

    printpol((w));
    printf(" ==g\n");
    // exit(1);

    return w;
}

unsigned short dd[N][N] = {0};

// Patterson & EEA 用（ランダム多項式、次元指定）
OP mkg(int kk)
{
    int i, j, k, l, ii = 0;
    OP w = {0};
    unsigned short tr[N] = {0};
    unsigned short ta[N] = {0};
    unsigned short po[K + 1] = {1, 0, 1, 0, 5};

aa:

    // printf("\n");

    // 既約性判定のためのBen-Orアルゴリズム。拡大体にも対応している。デフォルトでGF(8192)
    // 既約多項式しか使わない。

    l = -1;
    ii = 0;
    /*
    while (l == -1)
    {
        w = mkpol();
        l = ben_or(w);
        printf("irr=%d\n", l);
        if (ii > 300)
        {
            printf("too many error\n");
            exit(1);
        }
        ii++;
        //
    }
    */
    w = mkpol();
    memset(ta, 0, sizeof(ta));
    // g[0]=1;

    // 多項式の値が0でないことを確認
    for (i = 0; i < N; i++)
    {
        ta[i] = trace(w, i);
        if (ta[i] == 0)
        {
            printf("trace 0 @ %d\n", i);
            // fail = 1;
            goto aa;
        }
    }

    // 多項式を固定したい場合コメントアウトする。
    /*
  memset(ta, 0, sizeof(ta));
  w = setpol(po, K + 1);
  printpol((w));
  printf(" =poly\n");
//  exit(1);
  for (i = 0; i < N; i++)
  {    ta[i] = trace(w, i);
    if (ta[i] == 0)
    {
      printf("trace 0 @ %d\n", i);
      exit(1);
    }
  }
  */
    oprintpol(w);
    printf("\n");
    printsage(o2v(w));
    printf("\n");
    printf("sagemath で既約性を検査してください！\n");

    for (i = 0; i < N; i++)
    {
        tr[i] = oinv(ta[i]);
        // printf("%d,", tr[i]);
    }

    printpol((w));
    printf(" =irreducible\n");
    printsage(o2v(w));
    printf("\n");
    // wait();

    memset(vb, 0, sizeof(vb));
    memset(gt, 0, sizeof(gt));
    van(kk);
    ogt(po, kk);
    memset(mat, 0, sizeof(mat));

    //  wait();

    // #pragma omp parallel for

    printf("\nすげ、オレもうイキそ・・・\n");
    // keygen(g);
    // exit(1);

    for (j = 0; j < N; j++)
    {
        for (i = 0; i < K; i++)
        {
            ma[j][i] = gf[mlt(fg[vb[i][j]], tr[j])];
        }
        // printf("tr[%d]=%d\n",j,tr[j]);
    }

    unsigned short s;
#pragma omp parallel for default(none) private(i, j, k, s) shared(mat, gt, ma, gf, fg)
    for (i = 0; i < K; i++)
    {
        for (j = 0; j < N; j++)
        {
            s = 0;

            for (k = 0; k < K; k++)
                s ^= gf[mlt(fg[gt[k][i]], fg[ma[j][k]])];
            // printf("%d,",s);
            mat[j][i] = s;
        }
        printf("\n");
    }
    printf("\n");
    // exit(1);

    for (j = 0; j < N; j++)
    {
        for (i = 0; i < K; i++)
            printf("%d,", mat[j][i]);
        printf("\n");
    }
    printf("\n");
    // wait();

    return w;
}

// BMA 専用（多項式と次元指定）
OP mkc(OP w, int kk)
{
    int i, j, k, l, ii = 0;

    unsigned short tr[N] = {0};
    unsigned short ta[N] = {0};
    vec v = {0};
    unsigned short po[K + 1] = {1, 0, 1, 0, 5};
    // OP w={0};
    OP r = {0};

aa:

    // printf("\n");
    memset(mat, 0, sizeof(mat));
    // 既約性判定のためのBen-Orアルゴリズム。拡大体にも対応している。デフォルトでGF(8192)
    // 既約多項式しか使わない。

    l = -1;
    ii = 0;
    // irreducible goppa code (既役多項式が必要なら、ここのコメントを外すこと。)

    while (l == -1)
    {
        w = mkpol();
        l = ben_or(w);
        printf("irr=%d\n", l);
        if (ii > 300)
        {
            printf("too many error\n");
            exit(1);
        }
        ii++;
        //
    }

    // separable goppa code
    // w = mkpol();
    // r = omul(w, w);

    memset(ta, 0, sizeof(ta));
    // w = setpol(g, K + 1);
    printpol((r));
    // printf(" =poly\n");

    // 多項式の値が0でないことを確認
    for (i = 0; i < N; i++)
    {
        ta[i] = trace(r, i);
        if (ta[i] == 0)
        {
            printf("trace 0 @ %d\n", i);
            // fail = 1;
            goto aa;
        }
    }
    for (i = 0; i < N; i++)
    {
        tr[i] = oinv(ta[i]);
        // printf("%d,", tr[i]);
    }
    memset(g, 0, sizeof(g));
    // g[0] = 1;

    // 多項式を固定したい場合コメントアウトする。
    oprintpol(r);
    printf("\n");
    printsage(o2v(r));
    printf("\n");
    printf("sagemath で既約性を検査してください！\n");
    memset(v.x, 0, sizeof(v.x));
    //  v=rev(w);
    van(kk);
    //  v=o2v(w);
    ogt(g, kk);

    // wait();

    // #pragma omp parallel for

    printf("\nすげ、オレもうイキそ・・・\n");
    // keygen(g);
    // exit(1);

    for (j = 0; j < N; j++)
    {
        for (i = 0; i < kk; i++)
        {
            mat[j][i] = gf[mlt(fg[vb[i][j]], tr[j])];
        }
        // printf("tr[%d]=%d\n",j,tr[j]);
    }

    // printf("\n");
    // exit(1);
    /*
    for (j = 0; j < N; j++)
    {
        for (i = 0; i < kk; i++)
            printf("%d,", mat[j][i]);
        printf("\n");
    }
    //exit(1);
    //wait();
*/

    return w;
}

/* input: in0, in1 in GF((2^m)^t)*/
/* output: out = in0*in1 */
void GF_mul(unsigned short *out, unsigned short *in0, unsigned short *in1)
{
  int i, j;

  unsigned short prod[K * 2 - 1] = {0};

  for (i = 0; i < K * 2 - 1; i++)
    prod[i] = 0;

  for (i = 0; i < K; i++)
  {
    for (j = 0; j < K; j++)
      prod[i + j] ^= gf[mlt(fg[in0[i]], fg[in1[j]])];
  }
  //

  for (i = (K - 1) * 2; i >= K; i--)
  {
    if(K==512){
        //GF(2^512) from sage
        prod[i - K + 8] ^= prod[i];
        prod[i - K + 5] ^= prod[i];
        prod[i - K + 2] ^= prod[i];
        prod[i - K + 0] ^= prod[i];
    }
    // GF(2^256) from sage
    if(K==256){
    prod[i - K + 10] ^= prod[i];
    prod[i - K + 5] ^= prod[i];
    prod[i - K + 2] ^= prod[i];
    prod[i - K + 0] ^= prod[i];
    }    
    if(K==128){
       //128
        prod[i - K + 7] ^= prod[i];
        prod[i - K + 2] ^= prod[i];
        prod[i - K + 1] ^= prod[i];
        prod[i - K + 0] ^= prod[i];
    }
    /*
    //x^64+1x^3+1x^1+37x^0
        prod[i - K + 3] ^= prod[i];
        prod[i - K + 1] ^= prod[i];
        prod[i - K + 0] ^= gf_mul(prod[i], (unsigned short) 2);
    */
    if(K==32){
    //32
        prod[i - K + 15] ^= prod[i];
        prod[i - K + 9] ^= prod[i];
        prod[i - K + 7] ^= prod[i];
        prod[i - K + 4] ^= prod[i];
        prod[i - K + 3] ^= prod[i];
        prod[i - K + 0] ^= prod[i];
    }
    if(K==16){
    //16
        prod[i - K + 5] ^= prod[i];
        prod[i - K + 3] ^= prod[i];
        prod[i - K + 2] ^= prod[i];
        prod[i - K + 0] ^= prod[i];
    }
    if(K==8){
        prod[i - K + 4] ^= prod[i];
        prod[i - K + 3] ^= prod[i];
        prod[i - K + 2] ^= prod[i];
        prod[i - K + 0] ^= prod[i];
    }
    if(K==6){
        prod[i - K + 4] ^= prod[i];
        prod[i - K + 3] ^= prod[i];
        prod[i - K + 1] ^= prod[i];
        prod[i - K + 0] ^= prod[i];
    if(K==4){
        prod[i - K + 1] ^= prod[i];
        prod[i - K + 0] ^= prod[i];
    }
  }
  }

  for (i = 0; i < K; i++)
    out[i] = prod[i];
}


/* input: f, element in GF((2^m)^t) */
/* output: out, minimal polynomial of f */
/* return: 0 for success and -1 for failure */
int mykey(unsigned short *out, vec x)
{
  unsigned short mata[K + 1][K] = {0};
  MTX a = {0};
  int i, j, k;

  // fill matrix

  mata[0][0] = 1;

  for (i = 1; i < K; i++)
    mata[0][i] = 0;

  for (i = 0; i < K; i++)
    mata[1][i] = x.x[i];

  for (j = 2; j <= K; j++)
  {
    // for(i=0;i<128;i++)
    // mat[j][i]=gf[mlt(fg[mat[j-1][i]],fg[x.x[i]])];
    GF_mul(mata[j], mata[j - 1], x.x);

    // for(i=0;i<K;i++)
    // printf("%d,",mat[j][i]);
    // printf("\n");
  }
  // exit(1);
  //
  for (i = 0; i < K; i++)
  {
    for (j = 0; j < K + 1; j++)
    {
      a.x[i][j] = mata[j][i];
      printf("%d,", mata[j][i]);
    }
    printf("\n");
  }
  printf("\n");
  // exit(1);

  vec v = {0};
  v = renritu(a);
  // printsage(x);
  // printf("\n");
  // for(i=0;i<T;i++)
  // v.x[T-i-1]=x.x[i];
  // printsage(v);
  // printf("\n");

  for (i = 0; i < K; i++)
  {
    out[i] = v.x[i];
    printf("%d,", out[i]);
  }
  printf("\n");
  // exit(1);
}


OP vv(int kk)
{
    int i, j,ll=-1;
    OP r; // = mkpol();
    unsigned short tr[N];
    unsigned short ta[N] = {0};

    printf("van der\n");


    for (i = 0; i < N; i++)
    {
        mat[i][0] = vb[0][i] = 1;
    }
    // #pragma omp parallel for private(i, j)
    for (i = 1; i < kk; i++)
    {
        for (j = 0; j < N; j++)
        {
            vb[i][j] = gf[mltn(i, j)];
            // printf("%d,", vb[i][j]);
        }
        // printf("\n");
    }
    
   //van(kk);
    int l = -1;
    vec pp = {0}, tt = {0};

aa:

/*
    while (l < 0)
    {
        for (i = 0; i < K; i++)
            pp.x[i] = rand() % N;
        mykey(tt.x, pp);
        tt.x[K] = 1;
        l = ben_or(v2o(tt));
        if (l == 0)
        {
            printf("\n");
            printsage(tt);
            printf(" ==irr\n");
            // exit(1);
        }
    }
    r = v2o(tt);
    */
    // exit(1);
     ll=-1;
      while(ll<0){
          r = mkpol();
          ll=ben_or((r));
      }
  
    for (i = 0; i < N; i++)
    {
        ta[i] = trace(r, i);
        if (ta[i] == 0)
        {
            printf("trace 0 @ %d\n", i);
            // fail = 1;
            goto aa;
        }
    }

    for (i = 0; i < N; i++)
    {
        tr[i] = oinv(ta[i]);
        // printf("%d,", tr[i]);
    }

    // memset(g, 0, sizeof(g));
    // g[0] = 1;

    // 多項式を固定したい場合コメントアウトする。
    // ogt(g, kk);

    // wait();

    // #pragma omp parallel for

    printf("\nすげ、オレもうイキそ・・・\n");
    // keygen(g);
    // exit(1);

    for (i = 0; i < N; i++)
    {
        for (j = 0; j < kk; j++)
        {
            mat[i][j] = gf[mlt(fg[vb[j][i]], tr[i])];
        }
    }
    for (i = 0; i < K; i++)
    {
        for (j = 0; j < N; j++)
            printf("%d,", mat[j][i]);
        printf("\n");
    }

    return r;
}


//
OP mkd(OP w, int kk)
{
    int i, j, k, l, ii = 0;

    unsigned short tr[N] = {0};
    unsigned short ta[N] = {0};
    vec v = {0};
    unsigned short po[K + 1] = {1, 0, 1, 0, 5};
    // OP w={0};
    OP r = {0};

aa:

    // printf("\n");
    memset(mat, 0, sizeof(mat));
    // 既約性判定のためのBen-Orアルゴリズム。拡大体にも対応している。デフォルトでGF(8192)
    // 既約多項式しか使わない。

    l = -1;
    ii = 0;
    // irreducible goppa code (既役多項式が必要なら、ここのコメントを外すこと。)

    while (l == -1)
    {
        w = mkpol();
        l = ben_or(w);
        printf("irr=%d\n", l);
        if (ii > 300)
        {
            printf("too many error\n");
            exit(1);
        }
        ii++;
        //
    }

    // separable goppa code
    // w = mkpol();
    r = w;
    //  r=omul(w,w);
    memset(ta, 0, sizeof(ta));
    // w = setpol(g, K + 1);
    printpol((r));
    // printf(" =poly\n");

    // 多項式の値が0でないことを確認
    for (i = 0; i < N; i++)
    {
        ta[i] = trace(r, i);
        if (ta[i] == 0)
        {
            printf("trace 0 @ %d\n", i);
            // fail = 1;
            goto aa;
        }
    }
    for (i = 0; i < N; i++)
    {
        tr[i] = oinv(ta[i]);
        // printf("%d,", tr[i]);
    }
    memset(g, 0, sizeof(g));
    g[0] = 1;

    // 多項式を固定したい場合コメントアウトする。
    oprintpol(r);
    printf("\n");
    printsage(o2v(r));
    printf("\n");
    printf("sagemath で既約性を検査してください！\n");
    memset(v.x, 0, sizeof(v.x));
    //  v=rev(w);
    van(kk);
    //  v=o2v(w);
    ogt(g, kk);

    // wait();

    // #pragma omp parallel for

    printf("\nすげ、オレもうイキそ・・・\n");
    // keygen(g);
    // exit(1);

    for (i = 0; i < N; i++)
    {
        for (j = 0; j < kk; j++)
        {
            mat[i][j] = gf[mlt(fg[vb[j][i]],tr[i])];
        }
    }

    // printf("\n");
    // exit(1);
    /*
    for (j = 0; j < N; j++)
    {
        for (i = 0; i < kk; i++)
            printf("%d,", mat[j][i]);
        printf("\n");
    }
    //exit(1);
    //wait();
*/

    return w;
}

void half(int kk)
{
    int i, j, k;

    for (i = 0; i < N; i++)
        bm[i][0] = mat[i][0];
    for (i = 1; i < kk; i++)
    {
        for (j = 0; j < N; j++)
            bm[j][i] = mat[j][i * 2 - 1];
    }
    /*
    for (i = 0; i < N; i++)
    {
        for (j = 0; j < kk+1; j++)
            printf("%d,", bm[i][j]);
        printf("  ==bm\n");
    }
*/
    // exit(1);
}

// Niederreiter暗号の公開鍵を作る(RS)
MTX mk_pub()
{
    int i, j, k, l;
    FILE *fp;
    unsigned char dd[E * K] = {0};
    OP w = {0};
    MTX Z = {0}, FX = {0}, G_bin = {0}, G_int = {0}, R = {0}, O = {0};

    w = mkd(w, K * 2);
    // w = mkg(K);
    half(K / 2 + 1);

    oprintpol(w);
    printf("\n");
    printsage(o2v(w));
    printf("\n");
    printf("sagemath で既約性を検査してください！\n");

    R = bd2();
    printf("deco_rev= ");
    for (i = 0; i < (K / 2 + 1) * E; i++)
        printf("%d,", R.x[1][i] ^ R.x[2][i] ^ R.x[3][i]);
    printf("\n");
    Pgen();
    do
    {
        memset(inv_S.x, 0, sizeof(inv_S.x));
        memset(S.x, 0, sizeof(S.x));
        for (i = 0; i < (K / 2 + 1) * E; i++)
        {
            for (j = 0; j < (K / 2 + 1) * E; j++)
                S.x[i][j] = xor128() % 2;
        }
    } while (mkRS(S, inv_S.x) == -1);
    // mkS();
    // O = toByte(R, K / 2 + 1);
    //   exit(1);
    Z = mulmat(S, R, 2);
    printf("Zz=\n");
    for (j = 0; j < N; j++)
    {
        for (i = 0; i < (K / 2 + 1) * E; i++)
        {
            // G.z[j][i] = Z.w[P[j]][i];
            G_bin.x[j][i] = Z.x[P[j]][i];
            // Z.x[j][i]=Z.x[j][i];
            // printf("%d.",inv_S.w[j][i]);
        }
        // printf("\n");
    }
    // printf("\n");

    FX = toByte(G_bin, K / 2 + 1);
    for (i = 0; i < N; i++)
    {
        for (j = 0; j < K / 2 + 1; j++)
            G_int.x[i][j] = FX.x[i][j];
    }

    return G_int;
}

// Niederreiter暗号の公開鍵を作る(Goppa)
MTX pk_gen()
{
    int i, j, k, l;
    FILE *fp;
    unsigned char dd[E * K] = {0};
    OP w = {0};
    MTX R = {0}, R_bin = {0}, O = {0}, Q = {0}, O_bin = {0};

    w = vv(K); //mkd(w, K);
    // w = mkg(K);
    // half(K / 2 + 1);

    oprintpol(w);
    printf("\n");
    printsage(o2v(w));
    printf("\n");
    printf("sagemath で既約性を検査してください！\n");

    Q = bdet();

    Pgen();
    do
    {
        memset(inv_S.x, 0, sizeof(inv_S.x));
        memset(S.x, 0, sizeof(S.x));
        for (i = 0; i < K * E; i++)
        {
            for (j = 0; j < K * E; j++)
                S.x[i][j] = xor128() % 2;
        }
    } while (is_reg(S, inv_S.x) == -1);

    // makeS();
    //   exit(1);
    H = mulmat(S, Q, 1);
    for (i = 0; i < K * E; i++)
    {
        for (j = 0; j < N; j++)
        {
            // O.z[j][i] = H.z[P[j]][i];
            O_bin.x[j][i] = H.x[P[j]][i];
        }
    }
    R = toByte(O_bin, K);
    for (i = 0; i < N; i++)
    {
        for (j = 0; j < K * E; j++)
        {
            R_bin.x[i][j] = O_bin.x[i][j];
            // printf("%d,",R_bin.x[i][j]);
        }
    }

    // exit(1);

    return R;
}

OP dec(unsigned short ss[])
{
    int i, j, k;
    vec v = {0};
    OP s = {0};
    unsigned ch[K * E] = {0};
    unsigned char h2o[K * E] = {0};

    printf("!1\n");
    for (i = 0; i < K; i++)
    {
        v = i2v(ss[i]);
        for (j = 0; j < E; j++)
            ch[i * E + j] = v.x[j];
    }
    for (i = 0; i < K * E; i++)
        printf("%d", ch[i]);
    printf("\n");

    unsigned short uk[K] = {0};

    for (i = 0; i < K * E; i++)
    {
        for (j = 0; j < K * E; j++)
            h2o[i] ^= (ch[j] & inv_S.x[i][j]);
    }
    // for (i = 0; i < K * E; i++)
    // printf("%d,", h2o[i]);
    // printf("\n");

    for (i = 0; i < K; i++)
    {
        memset(v.x, 0, sizeof(v.x));
        for (j = 0; j < E; j++)
            v.x[j] = h2o[i * E + j];
        uk[i] = v2i(v);
    }
    for (i = 0; i < K; i++)
        printf("%d,", uk[i]);
    printf("\n");
    //    exit(1);
    s = setpol(uk, K);

    return s;
}

// 鍵生成
void key2(unsigned short g[])
{
    FILE *fp;
    unsigned short dd[K] = {0};
    int i, j, k;

    printf("鍵を生成中です。４分程かかります。\n");
    fp = fopen("H.key", "wb");
    i = 0;

    mkg(K);

    // exit(1);
    for (i = 0; i < N; i++)
    {
        for (j = 0; j < K; j++)
            dd[j] = mat[i][j];
        fwrite(dd, 2, K, fp);
    }
    fclose(fp);
    fp = fopen("sk.key", "wb");
    fwrite(g, 2, K + 1, fp);
    fclose(fp);
}

int printerr(OP r)
{
    int count, i, j, k;

    count = 0;

    for (i = 0; i < T; i++)
    {

        if (i > 0 && r.t[i].n == 0)
        {
            printf("err baka-z\n");
            // return -1;
            exit(1);
        }

        if (r.t[i].a > 0 && i > 0) // == r.t[i].n)
        {
            printf("err=%d %d %s\n", r.t[i].a, r.t[i].n, "お");
            count++;
        }
        if (i == 0)
        {
            printf("\nerr=%d %d %s\n", r.t[i].a, r.t[i].n, "う");
            count++;
        }
        // zz[r.t[i].n]=r.t[i].a;
    }
    if (count != T)
    {
        printf("error pattarn too few %d\n", count);
        return -1;
    }
    // exit(1);

    return count;
}

int elo2(OP r)
{
    int count, i, j, k;

    count = 0;

    unsigned char x[N] = {0}, yy[N] = {0};
    for (i = 0; i < T; i++)
    {

        if (i > 0 && r.t[i].n == 0)
        {
            printf("err baka-z\n");
            count++;
            // return -1;
            // exit(1);
        }

        if (r.t[i].a > 0 && i > 0) // == r.t[i].n)
        {
            x[r.t[i].n] = r.t[i].a;
            // printf("err=%d %d %s\n", r.t[i].a, r.t[i].n, "お");
            count++;
        }
        if (i == 0)
        {
            x[r.t[i].n] = r.t[i].a;
            // printf("\nerr=%d %d %s\n", r.t[i].a, r.t[i].n, "う");
            count++;
        }
        // zz[r.t[i].n]=r.t[i].a;
    }
    if (count < T)
    {
        printf("err is too few\n");
        exit(1);
    }
    for (i = 0; i < N; i++)
        yy[i] = x[P[i]];
    for (i = 0; i < N; i++)
    {
        if (yy[i] > 0)
            printf("err= %d\n", i);
    }

    return count;
}

int ero(vec v)
{
    int i, j, count = 0;

    for (i = 0; i < T * 2; i++)
    {
        if (i == 0)
        {
            printf("error position=%d %d う\n", i, v.x[i]);
            count++;
        }
        if (i > 0 && v.x[i] > 0)
        {
            printf("error position=%d %d お\n", i, v.x[i]);
            count++;
        }
        if (i > 0 && v.x[i] == 0)
        {
            printf("baka %d %d\n", i, v.x[i]);
            printf("v.x[K-1]=%d\n", v.x[K - 1]);
            /*
        printpol((w));
        printf(" ============goppa\n");
        printsage(o2v(w));
        printf(" ============sage\n");
        printsage(o2v(f));
        printf(" ============syn\n");
        printpol((f));
        printf(" ==========synd\n");
        printf("{");
        for (k = 0; k < N; k++)
        {
          if (z1[k] > 0)
            printf("%d,", z1[k]);
        }
        printf("};\n");
        //AA++;
        //wait();
        */
            break;
            //
            // exit (1);
        }
        int cnt = 0;
        /*
      for (k = 0; k < N; k++)
      {
        if (z1[k] > 0)
        {
          if (k != v.x[cnt])
          {
            printf("%d,%d\n", k, v.x[cnt]);
            printsage(o2v(w));
            printf(" ========w\n");
            AA++;
            break;
            //exit(1);
          }
          cnt++;
        }
      }
*/
    }

    if (count == T * 2)
    {
        printf("err=%dっ!! \n", count);
        B++;
    }
    if (count < T * 2)
    {
        printf("error is too few\n");

        AA++;
        // memcpy (zz, z1, sizeof (zz));
        /*
      printf("{");
      for (i = 0; i < D; i++)
        printf("%d,", z1[i]);
      printf("};\n");
      printpol((w));
      printf(" =========goppa\n");
      printsage(o2v(w));
      printf(" =========sage\n");
      printsage(o2v(f));
      printf(" =========syn\n");
      printpol((f));
      printf(" ==========synd\n");
      */
        printf("へげえええーっ\n");
        // exit(1);
        exit(1);
    }

    return count;
}

int ero2(vec v)
{
    int i, j, count = 0;
    unsigned short ya[N] = {0}, xa[N] = {0};

    for (i = 0; i < T; i++)
    {
        if (i == 0)
        {
            xa[v.x[i]] = 1;
            // printf("error position=%d %d う\n", i, v.x[i]);
            count++;
        }
        if (i > 0 && v.x[i] > 0)
        {
            xa[v.x[i]] = 1;
            // printf("error position=%d %d お\n", i, v.x[i]);
            count++;
        }
        if (i > 0 && v.x[i] == 0)
        {
            printf("baka %d %d\n", i, v.x[i]);
            printf("v.x[K-1]=%d\n", v.x[K - 1]);
            break;
            //
            // exit (1);
        }
    }

    int cnt = 0;
    for (i = 0; i < N; i++)
        ya[i] = xa[P[i]];
    for (i = 0; i < N; i++)
    {
        if (ya[i] > 0 && i == 0)
        {
            printf("error position=%d う\n", i);
            cnt = 1;
        }
        else if (ya[i] > 0)
        {
            if (cnt == 0)
            {
                printf("error position=%d う\n", i);
                cnt = 1;
            }
            else
            {
                printf("error position=%d お\n", i);
            }
        }
    }
    // exit(1);

    if (count == T)
    {
        printf("err=%dっ!! \n", count);
        B++;
    }
    if (count < T)
    {
        printf("error is too few\n");

        AA++;
        // memcpy (zz, z1, sizeof (zz));
        /*
      printf("{");
      for (i = 0; i < D; i++)
        printf("%d,", z1[i]);
      printf("};\n");
      printpol((w));
      printf(" =========goppa\n");
      printsage(o2v(w));
      printf(" =========sage\n");
      printsage(o2v(f));
      printf(" =========syn\n");
      printpol((f));
      printf(" ==========synd\n");
      */
        printf("へげえええーっ\n");
        // exit(1);
        exit(1);
    }

    return count;
}

int ero3(vec v)
{
    int i, j, count = 0;
    unsigned short ya[N] = {0}, xa[N] = {0};

    for (i = 0; i < T; i++)
    {
        if (i == 0)
        {
            xa[v.x[i]] = 1;
            // printf("error position=%d %d う\n", i, v.x[i]);
            count++;
        }
        if (i > 0 && v.x[i] > 0)
        {
            xa[v.x[i]] = 1;
            // printf("error position=%d %d お\n", i, v.x[i]);
            count++;
        }
        if (i > 0 && v.x[i] == 0)
        {
            printf("baka %d %d\n", i, v.x[i]);
            printf("v.x[K-1]=%d\n", v.x[K - 1]);
            break;
            //
            // exit (1);
        }
    }

    int cnt = 0;
    for (i = 0; i < N; i++)
        ya[i] = xa[i];
    for (i = 0; i < N; i++)
    {
        if (ya[i] > 0 && i == 0)
        {
            printf("error position=%d う\n", i);
            cnt = 1;
        }
        else if (ya[i] > 0)
        {
            if (cnt == 0)
            {
                printf("error position=%d う\n", i);
                cnt = 1;
            }
            else
            {
                printf("error position=%d お\n", i);
            }
        }
    }
    // exit(1);

    if (count == T)
    {
        printf("err=%dっ!! \n", count);
        B++;
    }
    if (count < T)
    {
        printf("error is too few\n");

        AA++;
        // memcpy (zz, z1, sizeof (zz));
        /*
      printf("{");
      for (i = 0; i < D; i++)
        printf("%d,", z1[i]);
      printf("};\n");
      printpol((w));
      printf(" =========goppa\n");
      printsage(o2v(w));
      printf(" =========sage\n");
      printsage(o2v(f));
      printf(" =========syn\n");
      printpol((f));
      printf(" ==========synd\n");
      */
        printf("へげえええーっ\n");
        // exit(1);
        exit(1);
    }

    return count;
}

void mkerr(unsigned short *z1, int num)
{
    int j, l;

    j = 0;

    memset(z1, 0, sizeof(z1));

    while (j < num)
    {
        l = rand() % N;
        // printf ("l=%d\n", l);
        if (0 == z1[l])
        {
            z1[l] = 1;
            // printf("l=%d\n", l);
            j++;
        }
    }
}

void fun()
{
    unsigned short i, k;

    OP qq = {0};
    for (i = 0b10001; i < 0b11111 + 1; i++)
    {
        qq = v2o(i2v(i));
        k = ben_or(qq);
        if (k == 0)
        {
            printpol((qq));
            printf(" =irreducible\n");
        }
    }
}

vec newhalf(unsigned short e[K])
{
    int i, j, k;
    vec v = {0};
    unsigned short t[K] = {0};

    for (i = 0; i < K / 2 + 1; i++)
    {
        t[i] = e[i];
        printf("e=%d\n", e[i]);
    }
    for (i = 0; i < K / 2 + 1; i++)
    {
        printf("t=%d\n", t[i]);
    }
    // exit(1);

    v.x[0] = t[0];
    v.x[1] = t[1];
    k = 2;
    for (i = 2; i < K; i++)
    {
        if (i % 2 == 1)
        {
            v.x[i] = t[k];
            k++;
        }
        if (i % 2 == 0)
            v.x[i] = gf[mlt(fg[v.x[i / 2]], fg[v.x[i / 2]])];
    }

    return v;
}

vec bfd(unsigned short ss[])
{
    int i, j, k, count = 0;
    vec v = {0};
    OP s = {0};
    unsigned ch[K * E * 2] = {0};
    unsigned char h2o[K * E * 2] = {0};

    // count=(K/2+1)*E-1;
    for (i = 0; i < (K / 2) + 1; i++)
    {
        v = i2v(ss[i]);
        for (j = 0; j < E; j++)
        {
            ch[count] = v.x[j];
            count++;
        }
    }
    printf("bfd_bin=\n");
    for (i = 0; i < (K / 2 + 1) * E; i++)
        printf("%d", ch[i]);
    printf("\n");
    // exit(1);

    unsigned short uk[K] = {0};

    for (i = 0; i < (K / 2 + 1) * E; i++)
    {
        for (j = 0; j < (K / 2 + 1) * E; j++)
            h2o[i] ^= (ch[j] & inv_S.x[i][j]);
    }

    printf("deco_bin=\n");
    for (i = 0; i < (K / 2 + 1) * E; i++)
        printf("%d,", h2o[i]);
    printf("\n");

    // count=(K/2+1)*E-1;

    count = 0;
    for (i = 0; i < (K / 2) + 1; i++)
    {
        memset(v.x, 0, sizeof(v.x));
        for (j = 0; j < E; j++)
        {
            v.x[j] = h2o[count];
            count++;
        }
        uk[i] = v2i(v);
    }

    printf("bm_int=\n");
    for (i = 0; i < K / 2 + 1; i++)
        printf("%d,", bm[1][i] ^ bm[2][i] ^ bm[3][i]);
    printf("\n");

    printf("u_int=\n");
    for (i = 0; i < K / 2 + 1; i++)
        printf("%d,", uk[i]);
    printf("\n");
    // exit(1);

    s = setpol(uk, K / 2 + 1);
    v = o2v(s);

    return v;
}

vec sin2(unsigned short zz[], MTX R)
{
    int i, j;
    OP s = {0};
    vec v = {0};
    unsigned short ss[K] = {0};

    for (i = 0; i < N; i++)
    {
        if (zz[i] > 0)
        {
            for (j = 0; j < K; j++)
            {
                // ss[j]
                v.x[j] ^= R.x[i][j];
                printf("%d,", R.x[i][j]);
            }
        }
        printf("\n");
    }

    return v;
}

unsigned short logx(unsigned short u)
{
    unsigned short i;

    return oinv(u);

    printf("baka-von\n");
}

OP bma(unsigned short s[], int kk)
{
    int i, j, k, ll = 0, l, d[2 * K + 1] = {0};
    OP lo[2 * K + 1] = {0}, b[2 * K + 1] = {0}, t[2 * K + 1] = {0}, a = {0}, f = {0}, h = {0}, g = {0}, hh = {0};
    vec v = {0}, x = {0}, w = {0};

    x.x[1] = 1;
    h = v2o(x);
    v.x[0] = 1;
    f = v2o(x);
    lo[0] = v2o(v);
    b[0] = lo[0];
    ll = 0;
    for (j = 1; j < T * 2 + 1; j++)
    {
        v = o2v(lo[j - 1]);
        k = 0;
        // printpol(v);
        // printf(" ==lo\n");

        l = deg(o2v(lo[j - 1]));
        for (i = 1; i < l + 1; i++)
        {
            k ^= gf[mlt(fg[v.x[i]], fg[s[j - i]])];
            // printf("v[%d]=%d\n", i, v.x[i]);
        }
        d[j] = s[j] ^ k;
        // printf("d[%d]=%d\n", j, d[j]);
        if (d[j] == 0)
        {
            lo[j] = lo[j - 1];
            b[j] = omul(b[j - 1], h);
            // ll=j-1;
        }
        else // if (d[j] > 0)
        {
            g = omul(kof(d[j], h), b[j - 1]);
            t[j] = oadd(lo[j - 1], g);
            lo[j] = t[j];
            if (ll * 2 > (j - 1))
            {
                // lo[j]=t[j];
                b[j] = omul(b[j - 1], h);
            }
            else // if(2*ll <= j)
            {
                // printpol((t[j]));
                // printf("==t[%d]\n", j);
                b[j] = kof(gf[oinv(d[j])], lo[j - 1]);
                // lo[j]=t[j];
                ll = j - ll;

                if (j == 2 * T)
                {
                    if (!(d[T * 2 - 1] == 0 && d[T * 2 - 3] == 0 && odeg(lo[j - 1]) == T) || !(odeg(lo[j - 1]) == T))
                    {
                        if ((d[T * 2 - 1] == 0 && odeg(lo[j - 2]) == T - 1))
                        {
                            lo[j - 1] = omul(lo[j - 2], h);
                            // printpol((lo[j - 1]));
                            // printf("\n");
                        }
                    }
                    break;
                }
            }
        }
        printf("l=%d\n", ll);
        k = 0;
        // printpol((b[j]));
        // printf(" ==b[%d]\n", j);
    }

    k = 0;
    int count = 0;
    // printpol((lo[j - 1]));
    // printf(" ==coef\n");
    if (odeg(lo[j - 1]) == T)
    {
        x = chen(lo[j - 1]);
    }
    else
    {
        printf("baka\n");
        exit(1);
        // return -1;
    }
    // exit(1);
    for (i = 0; i < deg(x) + 1; i++)
    {
        if (x.x[i] >= 0)
        {
            printf("xx[%d]=1\n", (x.x[i]));
            count++;
        }
        //

        if (x.x[i] == 0)
            k++;
        if (k > 1)
        {
            printf("baka0\n");
            // printvec((x));
            // for (i = 0; i < N; i++)
            // printf("%d,", zz[i]);
            exit(1);
            // return f;
        }
    }
    if (count < T)
    {
        printf("vaka in bms %d\n", count);
        exit(1);
    }

    // return count;
    return lo[j - 1];
}

vec rev(OP f)
{
    unsigned short i, tmp, j = 0, c[512] = {0}, d[512] = {0}, count = 0;
    vec v = {0}, x = {0};
    OP w = {0};

    v = o2v(f);

    j = odeg(f) + 1;
    printf("d=");
    for (i = 0; i < j; i++)
    {
        // d[f.t[i].n]=f.t[i].a;
        // c[count]=f.t[i].a;
        d[j - 1 - i] = v.x[i];
        printf("%d,", v.x[i]);
        count++;
    }
    printf("\n");
    printf("c=");
    // memset(v.x,0,sizeof(v.x));
    for (i = 0; i < count; i++)
    {
        x.x[i] = d[i];
        printf("%d,", d[i]);
    }
    printf("\n");
    // for(i=0;i<count;i++)
    // v.x[d[count-i-1]]=d[i];
    w = setpol(v.x, K + 1);
    // v=o2v(w);
    printpol((w));
    printf(" ==rev?\n");
    // exit(1);
    // f=v2o(v);

    return x;
}

OP sendrier(unsigned short zz[N], int kk)
{
    unsigned short syn[K / 2 + 1] = {0}, s = 0, rt[K * 3] = {0};
    int i, j, k;
    OP f = {0};
    vec v = {0}, x[K * 2] = {0};

    for (j = 0; j < N; j++)
    {
        if (zz[j] > 0)
        {
            // memcpy(syn, bm[j], sizeof(syn));
            printf("bm_in_sen= %d || ", j);
            for (i = 0; i < K / 2 + 1; i++)
            {
                syn[i] ^= bm[j][i];
                printf("%d,", syn[i]);
            }
        }
    }
    printf("\n");
    v = newhalf(syn);
    // printf("%d\n",j);
    for (k = 0; k < kk; k++)
        rt[k] = v.x[k];
    //}
    // exit(1);
    // printf ("%d\n", j);
    // printf ("\n");
    //}

    f = setpol(rt, kk);
    // printpol((f));
    // printf(" syn=============\n");

    return f;
}

OP sendrier2(unsigned short zz[N], MTX L)
{
    unsigned short syn[K + 1] = {0}, s[K + 1] = {0}, rt[K / 2 + 1] = {0}, uu[(K / 2 + 1) * E] = {0}, es[(K / 2 + 1) * E] = {0};
    int i, j, k, count = 0;
    OP f = {0}, w = {0};
    vec v = {0}, x = {0}, u = {0}, t = {0};
    unsigned short tmp[(K / 2 + 1) * E] = {0}, m[K + 1] = {0};

    for (j = 0; j < N; j++)
    {
        if (zz[j] > 0)
        {
            // for(i=0;i<(K/2)+1;i++){

            // memcpy(syn, L.w[j], sizeof(syn));

            for (k = 0; k < K / 2 + 1; k++)
            {
                rt[k] ^= L.x[j][k];
                // rt[k] = bm[j][k];
            }
        }
    }
    for (i = 0; i < K / 2 + 1; i++)
        printf("%d,", rt[i]);
    printf("\n");
    // exit(1);

    x = bfd(rt);
    for (i = 0; i < K / 2 + 1; i++)
    {
        u.x[K / 2 - i] = x.x[i];
    }
    for (i = 0; i < K / 2 + 1; i++)
        printf("%d,%d\n", u.x[i], x.x[i]);
    printf("\n");
    // exit(1);

    printf("rt=\n");
    for (i = 0; i < K / 2 + 1; i++)
        printf("%d,", u.x[i]);
    printf("\n");
    printf("bm_in se2 == %d || ", j);
    // exit(1);
    v = newhalf(u.x);

    printf("P= ");
    for (i = 0; i < N; i++)
        printf("%d,", P[i]);
    printf("\n");

    memset(s, 0, sizeof(s));
    for (i = 0; i < K; i++)
        s[i + 1] = v.x[i];

    printf("rt_deco= ");
    f = bma(s, K);
    x = chen(f);
    ero3(x);
    exit(1);
    wait();

    f = setpol(syn, K);

    return f;
}

MTX A2M(unsigned short A[N][K])
{
    int i, j;
    MTX J = {0};

    for (i = 0; i < N; i++)
    {
        for (j = 0; j < K; j++)
            J.x[i][j] = A[i][j];
    }

    return J;
}

// 言わずもがな
int main(void)
{
    int i;
    unsigned short zz[N] = {0};
    OP f = {0}, r = {0}, w = {0}, r1 = {0};
    vec v, x = {0};
    MTX R = {0}, O = {0};
    unsigned short s[K + 1] = {0,2,2,2,0};

    if (K > N)
        printf("configuration error! K is bigger than N\n");

    // （謎）
    memset(mat, 0, sizeof(mat));
    srand(clock());

    // 公開鍵を生成する(Niederreiterとは異なる) // 鍵サイズK : Goppa Code
     R = pk_gen();
    //mkd(w, K);
    // エラーベクトルの初期化
    memset(zz, 0, sizeof(zz));
    // 重みTのエラーベクトルを生成する
    mkerr(zz, T);
    // 暗号文の生成(s=eH)
     x = sin2(zz, R);
    // 復号化１(m'=sS^{-1})
     r = dec(x.x);
    //r = synd(zz, K);
    v = o2v(r);
    for (i = 0; i < K; i++)
        s[i + 1] = v.x[i];

    // Berlekamp-Massey Algorithm
    f = bma(s, K);
    x = chen(f);
    // 平文の表示(m=m'P^{-1})
    ero2(x);
    for (i = 0; i < N; i++)
        if (zz[i] > 0)
            printf("err=%d\n", i);
    exit(1);
    // wait();

    // debugging
    O = mk_pub(); // 鍵サイズ(K/2 Reed-Solomon)
    memset(zz, 0, sizeof(zz));
    // mkerr(zz, T);
    for (i = 0; i < T; i++)
        zz[i] = 1;
    r1 = sendrier2(zz, O);
    x = o2v(r1);
    for (i = 0; i < K; i++)
        s[i + 1] = x.x[i];
    // for (i = 0; i < K; i++)
    //     printf("%d,", s[i]);
    // printf("\n");
    f = bma(s, K);
    x = chen(f);
    ero2(x);
    printf("aaa\n");
    exit(1);
    for (i = 0; i < N; i++)
        if (zz[i] > 0)
            printf("%d,", i);
    printf("\n");

    if (odeg(f) < T)
    {
        printpol((r));
        printf("==goppa\n");
        for (i = 0; i < N; i++)
            printf("%d,", zz[i]);
        printf("\n");
        exit(1);
    }

    return 0;
}
