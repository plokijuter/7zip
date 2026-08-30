/* Transpose.c -- Byte-transposition converter
   Domaine public. */

#include "Precomp.h"
#include <string.h>

#include "Transpose.h"

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

SizeT Transpose_Encode(unsigned R, Byte *data, SizeT size, Byte *tmp)
{
  const SizeT blk = (TRANSPOSE_BLOCK / R) * R;   /* multiple de R, fixe */
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

SizeT Transpose_Decode(unsigned R, Byte *data, SizeT size, Byte *tmp)
{
  const SizeT blk = (TRANSPOSE_BLOCK / R) * R;
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
