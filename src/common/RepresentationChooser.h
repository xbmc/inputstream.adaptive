#pragma once
#include "AdaptiveStream.h"
#include "../SSD_dll.h"

struct DefaultRepresentationChooser : adaptive::AdaptiveTree::RepresentationChooser
{
  void SetDisplayDimensions(unsigned int w, unsigned int h);
  void SetMaxUserBandwidth(uint32_t max_user_bandwidth);
  void Prepare(bool secure_video_session);
  adaptive::AdaptiveTree::Representation* ChooseNextRepresentation(
    adaptive::AdaptiveTree::AdaptationSet* adp,
    adaptive::AdaptiveTree::Representation* rep,
    size_t *valid_segment_buffers_,
    size_t *available_segment_buffers_,
    uint32_t *assured_buffer_length_,
    uint32_t * max_buffer_length_,
    uint32_t rep_counter_);
  adaptive::AdaptiveTree::Representation* ChooseRepresentation(adaptive::AdaptiveTree::AdaptationSet* adp);
  double get_download_speed() const;
  double get_average_download_speed() const;
  void set_download_speed(double speed);

  uint16_t display_width_, display_height_;
  uint16_t width_, height_;
  uint32_t bandwidth_;

  uint16_t next_display_width_ = 0, next_display_height_ = 0;
  bool res_to_be_changed_ = 1;

  adaptive::AdaptiveTree::Representation *best_rep_, *min_rep_;//min_rep_ will be used for window-change detection

  std::chrono::steady_clock::time_point lastDimensionUpdated_ = std::chrono::steady_clock::now();

  bool ignore_display_;
  bool secure_video_session_;
  bool hdcp_override_;
  int max_resolution_, max_secure_resolution_;
  bool ignore_window_change_;

  uint32_t current_bandwidth_;
  uint32_t min_bandwidth_, max_bandwidth_;
  uint32_t assured_buffer_duration_;
  uint32_t max_buffer_duration_;

  double download_speed_, average_download_speed_;
  std::vector<SSD::SSD_DECRYPTER::SSD_CAPS> decrypter_caps_;
};