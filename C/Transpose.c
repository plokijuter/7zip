/* Transpose.c -- Byte-transposition converter
   Domaine public. */

#include "Precomp.h"
#include <string.h>

#include "Transpose.h"
#include "LzmaEnc.h"
#include "Alloc.h"
#include "Ppmd7.h"

/* encode : data[n][R] -> data[R][n]  (n = size / R) */
/* un seul bloc de blk octets (blk multiple de R) */
static void Enc1(unsigned R, Byte *data, SizeT blk, Byte *tmp)
{
  const SizeT n = blk / R;
  SizeT i, c;
  for (c = 0; c < R; c++)
  {
    const Byte *src = data + c;
    Byte *dst = tmp + c * n;
    for (i = 0; i < n; i++)
      dst[i] = src[i * R];
  }
  memcpy(data, tmp, blk);
}

unsigned Transpose_PickExp(UInt64 size)
{
  /* On veut que la queue non traitee (au pire un bloc entier) reste petite
     devant le fichier : on vise un bloc d'au plus 1/32 du flux. */
  unsigned e = TRANSPOSE_EXP_MIN;
  while (e < TRANSPOSE_EXP_MAX && ((UInt64)1 << (e + 1)) * 32 <= size)
    e++;
  return e;
}

SizeT Transpose_Encode(unsigned R, unsigned exp, Byte *data, SizeT size, Byte *tmp)
{
  const SizeT blk = (((SizeT)1 << exp) / R) * R;   /* multiple de R, fixe */
  SizeT done = 0;
  if (R < TRANSPOSE_MIN_R || blk == 0)
    return 0;
  while (size - done >= blk)
  {
    Enc1(R, data + done, blk, tmp);
    done += blk;
  }
  return done;   /* le reste est laisse a l'appelant */
}

/* decode : data[R][n] -> data[n][R] */
static void Dec1(unsigned R, Byte *data, SizeT blk, Byte *tmp)
{
  const SizeT n = blk / R;
  SizeT i, c;
  for (c = 0; c < R; c++)
  {
    const Byte *src = data + c * n;
    Byte *dst = tmp + c;
    for (i = 0; i < n; i++)
      dst[i * R] = src[i];
  }
  memcpy(data, tmp, blk);
}

SizeT Transpose_Decode(unsigned R, unsigned exp, Byte *data, SizeT size, Byte *tmp)
{
  const SizeT blk = (((SizeT)1 << exp) / R) * R;
  SizeT done = 0;
  if (R < TRANSPOSE_MIN_R || blk == 0)
    return 0;
  while (size - done >= blk)
  {
    Dec1(R, data + done, blk, tmp);
    done += blk;
  }
  return done;
}

/* --- Detection de la periode ------------------------------------------------
   On NE cherche PAS une periodicite : on cherche si transposer AIDE, ce qui
   n'est pas la meme chose. Mesure sur echantillon (validee contre la verite
   terrain sur 30 fichiers) : l'ecart absolu moyen entre octets distants de R.

     mad(L) = moyenne de |data[i] - data[i-L]|

   Transposer avec R rend adjacents les octets distants de R. Cela n'aide que
   si mad(R) est NETTEMENT plus bas que mad(1), c'est-a-dire si une colonne est
   plus homogene que le flux brut. Sans marge franche on renvoie 1 = identite.

   Un detecteur par autocorrelation a ete essaye puis REJETE : il voyait des
   periodes partout (bandes laterales des harmoniques) et degradait 7 fichiers
   sur 10, parfois lourdement (107 607 -> 137 309 octets). La periodicite d'un
   signal ne dit rien sur l'homogeneite de ses colonnes. */

/* mad(R) doit valoir au plus 70 % de mad(1) pour que le filtre s'active. */
#define TRANSPOSE_MARGE_NUM 70
#define TRANSPOSE_MARGE_DEN 100

static UInt64 Transpose_Mad(const Byte *d, SizeT n, unsigned L)
{
  UInt64 s = 0;
  SizeT i;
  for (i = L; i < n; i++)
  {
    const int diff = (int)d[i] - (int)d[i - L];
    s += (UInt64)(diff < 0 ? -diff : diff);
  }
  return s;
}

unsigned Transpose_DetectR(const Byte *data, SizeT size)
{
  SizeT n = (size < TRANSPOSE_SAMPLE) ? size : TRANSPOSE_SAMPLE;
  unsigned maxlag = TRANSPOSE_MAX_R;
  unsigned L, best = 1;
  double mad1, bestv;

  /* il faut au moins quelques enregistrements du plus grand R teste */
  if (n < (SizeT)4 * maxlag)
    return 1;

  mad1 = (double)Transpose_Mad(data, n, 1) / (double)(n - 1);
  if (mad1 <= 0)
    return 1;   /* flux constant : rien a gagner */

  bestv = mad1;
  for (L = 2; L <= maxlag; L++)
  {
    const double v = (double)Transpose_Mad(data, n, L) / (double)(n - L);
    if (v < bestv)
    {
      bestv = v;
      best = L;
    }
  }

  if (bestv * TRANSPOSE_MARGE_DEN > mad1 * TRANSPOSE_MARGE_NUM)
    return 1;   /* pas de colonne franchement plus homogene : on ne touche a rien */
  return best;
}

/* --- Mode calcul ------------------------------------------------------------
   L'heuristique ci-dessus se trompe parfois (elle est volontairement prudente).
   Ici on tranche par la mesure : on transpose un echantillon avec chacun des
   R les plus prometteurs, on le compresse pour de vrai en LZMA, et on garde
   celui qui rend le plus petit resultat. R=1 est toujours en lice, donc le
   mode calcul ne peut pas etre pire que ne rien faire. */

static SizeT Transpose_LzmaSize(const Byte *src, SizeT len)
{
  CLzmaEncProps props;
  Byte propsEnc[LZMA_PROPS_SIZE];
  SizeT propsSize = LZMA_PROPS_SIZE;
  SizeT destLen = len + len / 3 + 128;
  Byte *dest = (Byte *)ISzAlloc_Alloc(&g_Alloc, destLen);
  SRes res;
  if (!dest)
    return (SizeT)-1;
  LzmaEncProps_Init(&props);
  props.level = 5;                 /* rapide : on compare, on ne livre pas */
  props.dictSize = 1 << 20;
  props.numThreads = 1;
  res = LzmaEncode(dest, &destLen, src, len, &props, propsEnc, &propsSize, 0,
                   NULL, &g_Alloc, &g_BigAlloc);
  ISzAlloc_Free(&g_Alloc, dest);
  return (res == SZ_OK) ? destLen : (SizeT)-1;
}

/* Sortie qui ne garde rien : on ne veut que la TAILLE produite. */
typedef struct { IByteOut vt; UInt64 count; } CTransposeCountOut;
static void TransposeCountOut_Write(IByteOutPtr pp, Byte b)
{
  CTransposeCountOut *p = Z7_CONTAINER_FROM_VTBL(pp, CTransposeCountOut, vt);
  UNUSED_VAR(b)
  p->count++;
}

static SizeT Transpose_PpmdSize(const Byte *src, SizeT len)
{
  CPpmd7 ppmd;
  CTransposeCountOut out;
  out.vt.Write = TransposeCountOut_Write;
  out.count = 0;
  Ppmd7_Construct(&ppmd);
  if (!Ppmd7_Alloc(&ppmd, 16u << 20, &g_BigAlloc))
    return (SizeT)-1;
  ppmd.rc.enc.Stream = &out.vt;
  Ppmd7z_Init_RangeEnc(&ppmd);
  Ppmd7_Init(&ppmd, 16);
  Ppmd7z_EncodeSymbols(&ppmd, src, src + len);
  Ppmd7z_Flush_RangeEnc(&ppmd);
  Ppmd7_Free(&ppmd, &g_BigAlloc);
  return (SizeT)out.count;
}

static SizeT Transpose_ProbeSize(const Byte *src, SizeT len, unsigned probe)
{
  return (probe == TRANSPOSE_PROBE_PPMD) ? Transpose_PpmdSize(src, len)
                                         : Transpose_LzmaSize(src, len);
}

unsigned Transpose_MeasureR(const Byte *data, SizeT size, unsigned exp, unsigned probe)
{
  SizeT n = (size < TRANSPOSE_MEASURE_SAMPLE) ? size : TRANSPOSE_MEASURE_SAMPLE;
  unsigned cands[TRANSPOSE_MEASURE_CANDS];
  unsigned nc = 0, L, i, best = 1;
  double mad[TRANSPOSE_MAX_R + 1];
  SizeT bestSize;
  Byte *buf, *tmp;

  if (n < (SizeT)4 * TRANSPOSE_MAX_R)
    return 1;

  /* classement prealable : on ne mesure pas les 256 valeurs, seulement les
     plus prometteuses selon l'ecart absolu moyen. */
  for (L = 1; L <= TRANSPOSE_MAX_R; L++)
    mad[L] = (double)Transpose_Mad(data, n, L) / (double)(n - L);
  for (i = 0; i < TRANSPOSE_MEASURE_CANDS; i++)
  {
    unsigned pick = 0;
    double bv = 0;
    for (L = 2; L <= TRANSPOSE_MAX_R; L++)
      if (mad[L] >= 0 && (pick == 0 || mad[L] < bv)) { bv = mad[L]; pick = L; }
    if (!pick) break;
    cands[nc++] = pick;
    mad[pick] = -1;               /* retire du classement */
  }

  bestSize = Transpose_ProbeSize(data, n, probe);   /* reference : R=1, aucune transposition */
  if (bestSize == (SizeT)-1)
    return 1;

  buf = (Byte *)ISzAlloc_Alloc(&g_Alloc, n);
  tmp = (Byte *)ISzAlloc_Alloc(&g_Alloc, (size_t)1 << exp);
  if (!buf || !tmp)
  {
    if (buf) ISzAlloc_Free(&g_Alloc, buf);
    if (tmp) ISzAlloc_Free(&g_Alloc, tmp);
    return 1;
  }

  for (i = 0; i < nc; i++)
  {
    SizeT got;
    memcpy(buf, data, n);
    Transpose_Encode(cands[i], exp, buf, n, tmp);
    got = Transpose_ProbeSize(buf, n, probe);
    if (got != (SizeT)-1 && got < bestSize) { bestSize = got; best = cands[i]; }
  }

  ISzAlloc_Free(&g_Alloc, buf);
  ISzAlloc_Free(&g_Alloc, tmp);
  return best;
}
