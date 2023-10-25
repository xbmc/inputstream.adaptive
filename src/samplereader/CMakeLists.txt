set(SOURCES
  ADTSSampleReader.cpp
  FragmentedSampleReader.cpp
  SampleReaderFactory.cpp
  SubtitleSampleReader.cpp
  TSSampleReader.cpp
  WebmSampleReader.cpp
)

set(HEADERS
  ADTSSampleReader.h
  FragmentedSampleReader.h
  SampleReader.h
  SampleReaderFactory.h
  SubtitleSampleReader.h
  TSSampleReader.h
  WebmSampleReader.h
)

add_dir_sources(SOURCES HEADERS)
