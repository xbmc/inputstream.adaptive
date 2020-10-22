/*
 *      Copyright (C) 2020 peak3d
 *      http://www.peak3d.de
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "DASHTree.h"
#include "../log.h"
#include <fstream>
#include <iomanip>
#include <sstream>

std::string testfile;

void Log(const LogLevel loglevel, const char* format, ...)
{
}

void print_hex_string(std::ostream& os, std::string const str)
{
  std::ios_base::fmtflags f( os.flags() );

  os << "{";

  for (auto f : str)
  {
    os << " 0x" << std::hex << std::setw(2)
       << std::setfill('0') << static_cast<unsigned short>(f & 0x00FF);
  }

  os << " }";

  os.flags( f );
}

bool adaptive::AdaptiveTree::download(const char* url,
                                      const std::map<std::string, std::string>& manifestHeaders,
                                      void* opaque,
                                      bool scanEffectiveURL)
{
  FILE* f = fopen(testfile.c_str(), "rb");
  if (!f)
    return false;

   // read the file
  static const unsigned int CHUNKSIZE = 16384;
  char buf[CHUNKSIZE];
  size_t nbRead;

  while ((nbRead = fread(buf, 1, CHUNKSIZE, f)) > 0 && ~nbRead && write_data(buf, nbRead, opaque))
    ;

  fclose(f);

  SortTree();

  return nbRead == 0;
}

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    printf("Usage: <filename>)");
    exit(1);
  }

  adaptive::DASHTree tree;
  //only widevine supported for this test
  tree.supportedKeySystem_ = "urn:uuid:EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED";

  testfile = argv[1];
  if (!tree.open("", ""))
  {
    printf("open() failed %s", argv[1]);
    exit(1);
  }

  //Write the raw structure of the tree
  std::stringstream sstreamCur;
  sstreamCur << "Root: available_time_: " << tree.available_time_
            << ", base_time_: " << tree.base_time_ << ", firstStartNumber_: " << tree.firstStartNumber_
            << ", has_overall_seconds_: " << tree.has_overall_seconds_
            << ", has_timeshift_buffer_:" << tree.has_timeshift_buffer_
            << ", publish_time_: " << tree.publish_time_ << ", stream_start_: " << tree.stream_start_
            << ", #periods: " << tree.periods_.size() << "\n";

  for (const adaptive::AdaptiveTree::Period* period : tree.periods_)
  {
    sstreamCur << "Period: base_url_: " << period->base_url_ << ", duration_ : " << period->duration_
              << ", encryptionState_: " << period->encryptionState_ << ", id_: " << period->id_
              << ", included_types_: " << period->included_types_
              << ", need_secure_decoder_:" << period->need_secure_decoder_
              << ", start_: " << period->start_ << ", startNumber_: " << period->startNumber_
              << ", startPTS_: " << period->startPTS_ << ", #psshsets: " << period->psshSets_.size()
              << ", #adaptationSets: " << period->adaptationSets_.size() << "\n";
    for (const adaptive::AdaptiveTree::Period::PSSH pssh : period->psshSets_)
    {
      sstreamCur << "\tPSSH: defaultKID_: ";

      print_hex_string(sstreamCur, pssh.defaultKID_);

      sstreamCur << ", iv: ";

      print_hex_string(sstreamCur, pssh.iv);

      sstreamCur << ", media_: " << pssh.media_ << ", pssh_: " << pssh.pssh_ << "\n";
    }
    for (const adaptive::AdaptiveTree::AdaptationSet* adp : period->adaptationSets_)
    {
      sstreamCur << "\tADP: audio_track_id_: " << adp->audio_track_id_
                << ", base_url_: " << adp->base_url_ << ", codecs_: " << adp->codecs_
                << ", default_: " << adp->default_ << ", duration_: " << adp->duration_
                << ", forced_: " << adp->forced_ << ", group_: " << adp->group_
                << ", id_: " << adp->id_ << ", impaired_: " << adp->impaired_
                << ", language_: " << adp->language_ << ", mimeType_: " << adp->mimeType_
                << ", name_: " << adp->name_ << ", original_: " << adp->original_
                << ", startNumber_: " << adp->startNumber_ << ", startPTS_: " << adp->startPTS_
                << ", type_: " << adp->type_
                << ", #segment_durations_: " << adp->segment_durations_.size()
                << ", #representations_: " << adp->representations_.size() << "\n";
      sstreamCur << "\t\tSegTpl: duration: " << adp->segtpl_.duration
                << ", initialization: " << adp->segtpl_.initialization
                << ", media: " << adp->segtpl_.media
                << ", presentationTimeOffset: " << adp->segtpl_.presentationTimeOffset
                << ", timescale:" << adp->segtpl_.timescale << "\n";
      sstreamCur << "\t\t\tSegment durations:";
      for (const uint32_t dur : adp->segment_durations_.data)
        sstreamCur << ", " << dur;
      sstreamCur << "\n";

      for (const adaptive::AdaptiveTree::Representation* rep : adp->representations_)
      {
        sstreamCur << "\t\t\tRep: aspect_: " << rep->aspect_ << ", bandwidth_ : " << rep->bandwidth_
                  << ", channelCount_ : " << static_cast<unsigned int>(rep->channelCount_) << ", codecs_ : " << rep->codecs_
                  << ", codec_private_data_ : " << rep->codec_private_data_
                  << ", containerType_ : " << static_cast<unsigned int>(rep->containerType_) << ", duration_ : " << rep->duration_
                  << ", flags_ : " << rep->flags_ << ", fpsRate_ : " << rep->fpsRate_
                  << ", fpsScale_ : " << rep->fpsScale_ << ", hdcpVersion_ : " << rep->hdcpVersion_
                  << ", height_ : " << rep->height_ << ", id : " << rep->id
                  << ", indexRangeMax_ : " << rep->indexRangeMax_ << ", indexRangeMin_ : " << rep->indexRangeMin_
                  << ", nalLengthSize_ : " << static_cast<unsigned int>(rep->nalLengthSize_) << ", pssh_set_ : " << rep->pssh_set_
                  << ", ptsOffset_ : " << rep->ptsOffset_ << ", samplingRate_ : " << rep->samplingRate_
                  << ", source_url_ : " << rep->source_url_ << ", startNumber_ : " << rep->startNumber_
                  << ", timescale_ : " << rep->timescale_ << ", timescale_ext_ : " << rep->timescale_ext_
                  << ", timescale_int_ : " << rep->timescale_int_ << ", url_ : " << rep->url_
                  << ", width_ : " << rep->width_ << "\n";
        sstreamCur << "\t\t\t\tSegTpl: duration: " << rep->segtpl_.duration
                  << ", initialization: " << rep->segtpl_.initialization
                  << ", media: " << rep->segtpl_.media
                  << ", presentationTimeOffset: " << rep->segtpl_.presentationTimeOffset
                  << ", timescale:" << rep->segtpl_.timescale << "\n";
        sstreamCur << "\t\t\t\tInit: pssh_set_: " << rep->initialization_.pssh_set_
                  << std::hex << ", range_begin_: 0x" << rep->initialization_.range_begin_
                  << ", range_end_: 0x" << rep->initialization_.range_end_
                  << std::dec << ", startPTS_: " << rep->initialization_.startPTS_
                  << ", url:" << (rep->initialization_.url ? rep->initialization_.url : "NULL")
                  << "\n";
        for (const adaptive::AdaptiveTree::Segment &seg : rep->segments_.data)
        {
          sstreamCur << "\t\t\t\tSeg: pssh_set_: " << seg.pssh_set_ << std::hex
                    << ", range_begin_: 0x" << seg.range_begin_ << ", range_end_: 0x"
                    << seg.range_end_ << std::dec << ", startPTS_: " << seg.startPTS_
                    << ", url:" << (seg.url ? seg.url : "NULL") << "\n";
        }
      }
    }
  }

  // Write current test results to _generated
  std::string fn_current = argv[1];
  fn_current += "_current";

  FILE* f_current = fopen(fn_current.c_str(), "wb");
  if (!f_current)
  {
    printf("cannot create %s", fn_current.c_str());
    exit(1);
  }
  fwrite(sstreamCur.str().c_str(), 1, sstreamCur.str().size(), f_current);
  fclose(f_current);

  // Read the file _target which contains the valid results
  std::string fn_target = argv[1];
  fn_target += "_target";
  std::stringstream sstreamTgt;

  std::ifstream streamTarget(fn_target, std::ios::in | std::ios::binary);
  if (!streamTarget.is_open())
  {
    printf("cannot read %s", fn_target.c_str());
    exit(1);
  }
  sstreamTgt << streamTarget.rdbuf();

  return sstreamTgt.str() == sstreamCur.str() ? 0 : -1;
}
