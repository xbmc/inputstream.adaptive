/*******************************************************
|   RepresentationChooser
********************************************************/

#include "RepresentationChooser.h"
#include "../log.h"


//Pending- The plan was to group representations, this will give an easier switching control.
//Streams will get filtered in 6 buckets: 240p, 480p, HD-720p, FHD-1080p, QHD-1440p, UHD-2160p
//This will help in better representation switching when lots of representations are provided by CDN
//HD-720p and above will opt for highest fps. Eg: 720p24, 720p30, 720p50, 720p60 are available, it will go for 720p60
//FHD-1080p and above will opt for HDR if available
/*enum GenRep
{
  R240P,
  R480P,
  HD,
  FHD,
  QHD,
  UHD
};*/
//vector<pair<int,int> > mapping_res_to_adp_;   //mapping to <  GenRep  ,adp->representations_ index>     . Will store 6 resolution buckets if available (90% match)
//This to be run during initialization function
//int GenRepPixel[6]={};
/*for(int i=0;i<6;i++)
  {
    uint32_t diff=INT_MAX, index=-1;
    for (std::vector<adaptive::AdaptiveTree::Representation*>::const_iterator
            br(adp->representations_.begin()),
        er(adp->representations_.end());
        br != er; ++br)
      {
      //Select for best match with error of 15%
        int res=   abs(static_cast<int>((*br)->width_ * (*br)->height_) -   static_cast<int>(GenRepPixel[i]);
        if( res  < 0.15*GenRepPixel[i] && res < diff)
          {
          diff=res;
          index= br- representations_.begin();
          }
      }
      if(index!=-1)
        mapping_res_to_adp_.push_back({i,index});
    }
*/



  //SetDisplayDimensions will be called upon changed dimension only (will be filtered beforehand by xbmc api calls to SetVideoResolution)
void DefaultRepresentationChooser::SetDisplayDimensions(unsigned int w, unsigned int h)
{
  if (res_to_be_changed_)
  {
    display_width_ = w;
    display_height_ = h;
    //Log(LOGLEVEL_DEBUG, "SetDisplayDimensions(unsigned int w=%u, unsigned int h=%u) ",w,h);

    width_ = ignore_display_ ? 8192 : display_width_;
    switch (secure_video_session_ ? max_secure_resolution_ : max_resolution_)
    {
    case 1:
      if (width_ > 640)
        width_ = 640;
      break;
    case 2:
      if (width_ > 960)
        width_ = 960;
      break;
    case 3:
      if (width_ > 1280)
        width_ = 1280;
      break;
    case 4:
      if (width_ > 1920)
        width_ = 1920;
      break;
    default:;
    }

    height_ = ignore_display_ ? 8192 : display_height_;
    switch (secure_video_session_ ? max_secure_resolution_ : max_resolution_)
    {
    case 1:
      if (height_ > 480)
        height_ = 480;
      break;
    case 2:
      if (height_ > 640)
        height_ = 640;
      break;
    case 3:
      if (height_ > 720)
        height_ = 720;
      break;
    case 4:
      if (height_ > 1080)
        height_ = 1080;
      break;
    default:;
    }
    next_display_width_ = display_width_;
    next_display_height_ = display_height_;
    res_to_be_changed_ = false;
  }
  else
  {
    next_display_width_ = w;
    next_display_height_ = h;
  }
  lastDimensionUpdated_ = std::chrono::steady_clock::now();
};

void DefaultRepresentationChooser::SetMaxUserBandwidth(uint32_t max_user_bandwidth)
{
  if (max_bandwidth_ == 0 || (max_user_bandwidth && max_bandwidth_ > max_user_bandwidth))
    max_bandwidth_ = max_user_bandwidth;
};

void DefaultRepresentationChooser::Prepare(bool secure_video_session)
{
  secure_video_session_ = secure_video_session;
  res_to_be_changed_ = true;
  SetDisplayDimensions(display_width_, display_height_);

  Log(LOGLEVEL_DEBUG, "Stream selection conditions: w: %u, h: %u, bw: %u", width_, height_,
    bandwidth_);
};

adaptive::AdaptiveTree::Representation* DefaultRepresentationChooser::ChooseNextRepresentation(
  adaptive::AdaptiveTree::AdaptationSet* adp,
  adaptive::AdaptiveTree::Representation* rep,
  size_t *valid_segment_buffers_,
  size_t *available_segment_buffers_,
  uint32_t *assured_buffer_length_,
  uint32_t * max_buffer_length_,
  uint32_t rep_counter_)   //to be called from ensuresegment only,  SEPERATED FOR FURTHER DEVELOPMENT, CAN BE MERGED AFTERWARDS
{
  if ((std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - lastDimensionUpdated_).count() > 15)
    && (!ignore_window_change_)
    && (!ignore_display_)
    && !(next_display_width_ == display_width_ && next_display_height_ == display_height_))
  {
    res_to_be_changed_ = true;
    Log(LOGLEVEL_DEBUG, "Updating new display resolution to: (w X h) : (%u X %u)", next_display_width_, next_display_height_);
    SetDisplayDimensions(next_display_width_, next_display_height_);
  }

  adaptive::AdaptiveTree::Representation *next_rep(0);//best_rep definition to be finalised
  unsigned int bestScore(~0);
  uint16_t hdcpVersion = 99;
  uint32_t hdcpLimit = 0;


  current_bandwidth_ = get_average_download_speed();
  Log(LOGLEVEL_DEBUG, "current_bandwidth_: %u ", current_bandwidth_);

  float buffer_hungry_factor = 1.0;// can be made as a sliding input
  buffer_hungry_factor = ((float)*valid_segment_buffers_ / (float)*assured_buffer_length_);
  buffer_hungry_factor = buffer_hungry_factor > 0.5 ? buffer_hungry_factor : 0.5;

  uint32_t bandwidth = (uint32_t)(buffer_hungry_factor*7.0*current_bandwidth_);
  Log(LOGLEVEL_DEBUG, "bandwidth set: %u ", bandwidth);

  if (*valid_segment_buffers_ >= *assured_buffer_length_)
  {
    return adp->best_rep_;
  }
  if ((*valid_segment_buffers_ > 6) && (bandwidth >= rep->bandwidth_ * 2) && (rep != adp->best_rep_) && (adp->best_rep_->bandwidth_ <= bandwidth)) //overwrite case, more internet data
  {
    *valid_segment_buffers_ = std::max(*valid_segment_buffers_ / 2, *valid_segment_buffers_ - rep_counter_);
    *available_segment_buffers_ = *valid_segment_buffers_; //so that ensure writes again with new rep
  }

  for (std::vector<adaptive::AdaptiveTree::Representation*>::const_iterator br(adp->representations_.begin()),
    er(adp->representations_.end());
    br != er; ++br)
  {
    unsigned int score;
    if (!hdcp_override_)
    {
      hdcpVersion = decrypter_caps_[(*br)->pssh_set_].hdcpVersion;
      hdcpLimit = decrypter_caps_[(*br)->pssh_set_].hdcpLimit;
    }

    if ((*br)->bandwidth_ <= bandwidth && (*br)->hdcpVersion_ <= hdcpVersion &&
      (!hdcpLimit || static_cast<uint32_t>((*br)->width_) * (*br)->height_ <= hdcpLimit) &&
      ((score = abs(static_cast<int>((*br)->width_ * (*br)->height_) -
        static_cast<int>(width_ * height_)) +
        static_cast<unsigned int>(sqrt(bandwidth - (*br)->bandwidth_))) < bestScore))
    {
      bestScore = score;
      next_rep = (*br);
    }
    else if (!adp->min_rep_ || (*br)->bandwidth_ < adp->min_rep_->bandwidth_)
      adp->min_rep_ = (*br);
  }
  if (!next_rep)
    next_rep = adp->min_rep_;

  //Log(LOGLEVEL_DEBUG, "NextRep bandwidth: %u ",next_rep->bandwidth_);

  return next_rep;
};

adaptive::AdaptiveTree::Representation* DefaultRepresentationChooser::ChooseRepresentation(adaptive::AdaptiveTree::AdaptationSet* adp) //to be called single time
{
  adaptive::AdaptiveTree::Representation *new_rep(0);
  unsigned int bestScore(~0), valScore(~0);
  uint16_t hdcpVersion = 99;
  uint32_t hdcpLimit = 0;


  uint32_t bandwidth = min_bandwidth_;
  if (current_bandwidth_ > bandwidth_)
    bandwidth = current_bandwidth_;
  if (max_bandwidth_ && bandwidth_ > max_bandwidth_)
    bandwidth = max_bandwidth_;

  bandwidth = static_cast<uint32_t>(bandwidth_ *
    (adp->type_ == adaptive::AdaptiveTree::VIDEO ? 0.9 : 0.1));

  for (std::vector<adaptive::AdaptiveTree::Representation*>::const_iterator
    br(adp->representations_.begin()),
    er(adp->representations_.end());
    br != er; ++br)
  {
    (*br)->assured_buffer_duration_ = assured_buffer_duration_;
    (*br)->max_buffer_duration_ = max_buffer_duration_;
    unsigned int score;
    if (!hdcp_override_)
    {
      hdcpVersion = decrypter_caps_[(*br)->pssh_set_].hdcpVersion;
      hdcpLimit = decrypter_caps_[(*br)->pssh_set_].hdcpLimit;
    }

    if ((*br)->bandwidth_ <= bandwidth && (*br)->hdcpVersion_ <= hdcpVersion &&
      (!hdcpLimit || static_cast<uint32_t>((*br)->width_) * (*br)->height_ <= hdcpLimit) &&
      ((score = abs(static_cast<int>((*br)->width_ * (*br)->height_) -
        static_cast<int>(width_ * height_)) +
        static_cast<unsigned int>(sqrt(bandwidth - (*br)->bandwidth_))) < bestScore))
    {
      bestScore = score;
      new_rep = (*br);
    }
    else if (!adp->min_rep_ || (*br)->bandwidth_ < adp->min_rep_->bandwidth_)
      adp->min_rep_ = (*br);

    if (((*br)->hdcpVersion_ <= hdcpVersion) && ((!hdcpLimit || static_cast<uint32_t>((*br)->width_) * (*br)->height_ <= hdcpLimit))
      && ((score = abs(static_cast<int>((*br)->width_ * (*br)->height_) - static_cast<int>(width_ * height_))) <= valScore))  //it is bandwidth independent(if multiple same resolution bandwidth, will select first rep)
    {
      valScore = score;
      if (!adp->best_rep_ || (*br)->bandwidth_ > adp->best_rep_->bandwidth_)
        adp->best_rep_ = (*br);
    }

  }
  if (!new_rep)
    new_rep = adp->min_rep_;
  if (!adp->best_rep_)
    adp->best_rep_ = adp->min_rep_;
  Log(LOGLEVEL_DEBUG, "ASSUREDBUFFERDURATION selected: %d ", new_rep->assured_buffer_duration_);
  Log(LOGLEVEL_DEBUG, "MAXBUFFERDURATION selected: %d ", new_rep->max_buffer_duration_);

  return new_rep;
};

double DefaultRepresentationChooser::get_download_speed() const { return download_speed_; };
double DefaultRepresentationChooser::get_average_download_speed() const { return average_download_speed_; };
void DefaultRepresentationChooser::set_download_speed(double speed)
{
  download_speed_ = speed;
  if (!average_download_speed_)
    average_download_speed_ = download_speed_;
  else
    average_download_speed_ = average_download_speed_ * 0.8 + download_speed_ * 0.2;
};
