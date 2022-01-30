#include "TTMLCodecHandler.h"

bool TTMLCodecHandler::ReadNextSample(AP4_Sample& sample, AP4_DataBuffer& buf)
{
  uint64_t pts;
  uint32_t dur;

  if (m_ttml.Prepare(pts, dur))
  {
    buf.SetData(static_cast<const AP4_Byte*>(m_ttml.GetData()), m_ttml.GetDataSize());
    sample.SetDts(pts);
    sample.SetCtsDelta(0);
    sample.SetDuration(dur);
    return true;
  }
  else
    buf.SetDataSize(0);
  return false;
}

bool TTMLCodecHandler::Transform(AP4_UI64 pts,
                                 AP4_UI32 duration,
                                 AP4_DataBuffer& buf,
                                 AP4_UI64 timescale)
{
  return m_ttml.Parse(buf.GetData(), buf.GetDataSize(), timescale, m_ptsOffset);
}
