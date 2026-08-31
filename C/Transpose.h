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
   Indispensable : 7-Zip n'utilise pas les memes tailles de tampon a la
   compression et a la decompression. Sans bloc fixe, la transposition n'est
   pas reversible.

   La taille est choisie a l'encodage selon la taille du flux, puis INSCRITE
   dans l'archive : les deux cotes utilisent donc la meme, quels que soient
   leurs tampons.

   Pourquoi la faire varier : le dernier bloc incomplet du flux n'est jamais
   transpose (le filtre ne sait pas qu'il est le dernier), et ces octets bruts
   coutent cher. Sur un petit fichier un bloc de 64 Ko laisse jusqu'a 12 % des
   donnees non traitees ; un bloc court limite la perte. Sur un gros fichier
   la queue est negligeable et un bloc long donne de meilleures colonnes. */
#define TRANSPOSE_EXP_MIN 12          /*  4 Ko */
#define TRANSPOSE_EXP_MAX 16          /* 64 Ko */
#define TRANSPOSE_EXP_DEF 16          /* defaut si la taille est inconnue */
#define TRANSPOSE_BLOCK (1u << TRANSPOSE_EXP_MAX)   /* tampon temporaire max */

/* Choisit l'exposant du bloc pour un flux de taille donnee. */
unsigned Transpose_PickExp(UInt64 size);

/* Transpose (size / R) enregistrements complets, en place, via un tampon temporaire.
   Les (size % R) octets de fin sont laisses tels quels.
   Renvoie le nombre d'octets effectivement convertis. */
SizeT Transpose_Encode(unsigned R, unsigned exp, Byte *data, SizeT size, Byte *tmp);
SizeT Transpose_Decode(unsigned R, unsigned exp, Byte *data, SizeT size, Byte *tmp);

/* Taille de l'echantillon analyse pour deviner la periode. */
#define TRANSPOSE_SAMPLE 65536

/* Devine la taille d'enregistrement R par autocorrelation.
   Renvoie 1 si aucune periode franche ne ressort : dans ce cas le filtre
   se comporte en identite plutot que de risquer d'empirer la compression. */
unsigned Transpose_DetectR(const Byte *data, SizeT size);

/* Mode CALCUL : au lieu de se fier a l'heuristique, on compresse reellement un
   echantillon avec chaque R candidat (plus R=1) et on garde le vainqueur.
   Plus lent, mais c'est une mesure et non une supposition. */
#define TRANSPOSE_MEASURE_SAMPLE (4u << 20)   /* 4 Mo : il faut plusieurs blocs pour
                                                 voir si la transposition casse
                                                 la redondance a longue portee */
#define TRANSPOSE_MEASURE_CANDS  8
/* Codeur servant a la mesure. Il DOIT etre celui qui suivra reellement le
   filtre : LZMA et PPMd ne preferent pas le meme R, et se tromper de codeur
   de mesure conduit a des choix aberrants (mesure : jusqu'a x19 de perte). */
#define TRANSPOSE_PROBE_LZMA 0
#define TRANSPOSE_PROBE_PPMD 1
unsigned Transpose_MeasureR(const Byte *data, SizeT size, unsigned exp, unsigned probe);

/* Choix de R sur le fichier ENTIER, pour une passe prealable.
   On ne cherche pas a deviner juste : on compresse pour de vrai a R=1 et aux
   rares candidats retenus, et on garde le plus petit. R=1 etant toujours en
   lice, degrader devient impossible par construction — pas seulement rare.
   Un filtre en flux ne peut pas faire cela : il ne voit jamais plus que le
   tampon de FilterCoder, et le verdict s'inverse avec la taille de
   l'echantillon (mesure : R=12 gagne sur 2 Mo, perd sur 3,5 Mo). */

/* Au-dela de ce rapport de compression sans filtre, transposer n'apporte
   jamais rien : mesure sur 55 fichiers, aucun gain rate a partir de 10x. */
#define TRANSPOSE_RATIO_GUARD 10
unsigned Transpose_ChooseR_Full(const Byte *data, SizeT size, unsigned exp, unsigned probe, int partial);

/* Au-dela de cette taille on ne lit pas tout le fichier : la decision porte
   alors sur un prefixe, et la garantie de non-degradation ne tient plus —
   un prefixe peut mentir (mesure : R=12 gagne sur 2 Mo d'un fichier de 3,5 Mo
   et perd sur le fichier entier). Sur un prefixe on exige donc une marge
   franche avant d'accepter la transposition. */
#define TRANSPOSE_FULL_LIMIT (64u << 20)
#define TRANSPOSE_PREFIX_MARGIN 0.80

EXTERN_C_END

#endif
