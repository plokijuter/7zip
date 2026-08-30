/* Transpose.h -- Byte-transposition converter for fixed-size records
   Domaine public. Regroupe l'octet i de chaque enregistrement de R octets.
   Concu pour precede LZMA2 : les colonnes homogenes se compressent mieux. */

#ifndef ZIP7_INC_TRANSPOSE_H
#define ZIP7_INC_TRANSPOSE_H

#include "7zTypes.h"

EXTERN_C_BEGIN

/* R = 1 signifie IDENTITE : le filtre ne touche a rien.
   C'est le repli quand aucune periode nette n'est detectee, pour que le
   filtre ne puisse jamais degrader le fichier. */
#define TRANSPOSE_MIN_R 1
#define TRANSPOSE_MAX_R 256

/* Taille de bloc FIXE, independante du tampon de l'appelant.
   Indispensable : 7-Zip n'utilise pas les memes tailles de tampon
   a la compression et a la decompression. Sans bloc fixe, la
   transposition n'est pas reversible. */
#define TRANSPOSE_BLOCK 65536

/* Transpose (size / R) enregistrements complets, en place, via un tampon temporaire.
   Les (size % R) octets de fin sont laisses tels quels.
   Renvoie le nombre d'octets effectivement convertis. */
SizeT Transpose_Encode(unsigned R, Byte *data, SizeT size, Byte *tmp);
SizeT Transpose_Decode(unsigned R, Byte *data, SizeT size, Byte *tmp);

/* Taille de l'echantillon analyse pour deviner la periode. */
#define TRANSPOSE_SAMPLE 65536

/* Devine la taille d'enregistrement R par autocorrelation.
   Renvoie 1 si aucune periode franche ne ressort : dans ce cas le filtre
   se comporte en identite plutot que de risquer d'empirer la compression. */
unsigned Transpose_DetectR(const Byte *data, SizeT size);

EXTERN_C_END

#endif
