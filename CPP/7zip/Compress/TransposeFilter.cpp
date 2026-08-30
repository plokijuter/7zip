// TransposeFilter.cpp -- filtre de transposition par octet pour enregistrements de taille fixe

#include "StdAfx.h"

#include "../../../C/Transpose.h"

#include "../../Common/MyBuffer.h"
#include "../../Common/MyCom.h"

#include "../ICoder.h"

#include "../Common/RegisterCodec.h"

namespace NCompress {
namespace NTranspose {

struct CTranspose
{
  // _R == 0 : mode AUTOMATIQUE, la periode sera devinee sur les premieres
  // donnees vues puis figee pour tout le flux.
  unsigned _R;
  CByteBuffer _tmp;
  CTranspose(): _R(0) {}
  Byte *Tmp(size_t need)
  {
    if (_tmp.Size() < need)
      _tmp.Alloc(need);
    return (Byte *)_tmp;
  }
};

#ifndef Z7_EXTRACT_ONLY

class CEncoder Z7_final:
  public ICompressFilter,
  public ICompressSetCoderProperties,
  public ICompressWriteCoderProperties,
  public CMyUnknownImp,
  CTranspose
{
  Z7_IFACES_IMP_UNK_3(
      ICompressFilter,
      ICompressSetCoderProperties,
      ICompressWriteCoderProperties)
};

Z7_COM7F_IMF(CEncoder::Init()) { return S_OK; }

Z7_COM7F_IMF2(UInt32, CEncoder::Filter(Byte *data, UInt32 size))
{
  // Mode automatique : on devine la periode sur le premier bloc vu, puis on la
  // fige. 7-Zip (v23+) reecrit les proprietes du codec APRES Code(), donc le R
  // decouvert ici sera bien inscrit dans l'archive.
  if (_R == 0)
  {
    // On ne fige la decision qu'avec assez de donnees sous les yeux ; sinon on
    // rend la main pour etre rappele avec un tampon plus grand.
    if (size < TRANSPOSE_SAMPLE)
      return 0;
    _R = Transpose_DetectR(data, size);
  }

  // Aucune periode franche : on ne touche a rien. Le filtre est alors neutre
  // et ne peut pas degrader la compression.
  if (_R == 1)
    return size;

  // en dessous d'un bloc complet, on ne convertit rien
  if (_R == 0 || size < TRANSPOSE_BLOCK)
    return 0;
  const SizeT used = Transpose_Encode(_R, data, size, Tmp(TRANSPOSE_BLOCK));
  return (UInt32)used;
}

Z7_COM7F_IMF(CEncoder::SetCoderProperties(const PROPID *propIDs, const PROPVARIANT *props, UInt32 numProps))
{
  unsigned R = _R;
  for (UInt32 i = 0; i < numProps; i++)
  {
    const PROPVARIANT &prop = props[i];
    const PROPID propID = propIDs[i];
    if (propID >= NCoderPropID::kReduceSize)
      continue;
    if (prop.vt != VT_UI4)
      return E_INVALIDARG;
    switch (propID)
    {
      case NCoderPropID::kDefaultProp:
        if (prop.ulVal < TRANSPOSE_MIN_R || prop.ulVal > TRANSPOSE_MAX_R)
          return E_INVALIDARG;
        R = prop.ulVal;
        break;
      case NCoderPropID::kNumThreads: break;
      case NCoderPropID::kLevel: break;
      default: return E_INVALIDARG;
    }
  }
  _R = R;
  return S_OK;
}

Z7_COM7F_IMF(CEncoder::WriteCoderProperties(ISequentialOutStream *outStream))
{
  // _R == 0 signifie que Filter() n'a jamais rien vu (flux vide) : on inscrit
  // l'identite, jamais une valeur non initialisee.
  const unsigned r = (_R == 0) ? 1 : _R;
  const Byte prop = (Byte)(r - 1);
  return outStream->Write(&prop, 1, NULL);
}

#endif

class CDecoder Z7_final:
  public ICompressFilter,
  public ICompressSetDecoderProperties2,
  public CMyUnknownImp,
  CTranspose
{
  Z7_IFACES_IMP_UNK_2(
      ICompressFilter,
      ICompressSetDecoderProperties2)
};

Z7_COM7F_IMF(CDecoder::Init()) { return S_OK; }

Z7_COM7F_IMF2(UInt32, CDecoder::Filter(Byte *data, UInt32 size))
{
  if (_R == 1)
    return size;
  if (size < TRANSPOSE_BLOCK)
    return 0;
  const SizeT used = Transpose_Decode(_R, data, size, Tmp(TRANSPOSE_BLOCK));
  return (UInt32)used;
}

Z7_COM7F_IMF(CDecoder::SetDecoderProperties2(const Byte *props, UInt32 size))
{
  if (size != 1)
    return E_INVALIDARG;
  _R = (unsigned)props[0] + 1;
  if (_R < TRANSPOSE_MIN_R)
    return E_INVALIDARG;
  return S_OK;
}

REGISTER_FILTER_E(Transpose,
    CDecoder(),
    CEncoder(),
    0xC, "Transpose")

}}
