/*****************************************************************
|
|    AP4 - File Processor
|
|    Copyright 2002-2008 Axiomatic Systems, LLC
|
|
|    This file is part of Bento4/AP4 (MP4 Atom Processing Library).
|
|    Unless you have obtained Bento4 under a difference license,
|    this version of Bento4 is Bento4|GPL.
|    Bento4|GPL is free software; you can redistribute it and/or modify
|    it under the terms of the GNU General Public License as published by
|    the Free Software Foundation; either version 2, or (at your option)
|    any later version.
|
|    Bento4|GPL is distributed in the hope that it will be useful,
|    but WITHOUT ANY WARRANTY; without even the implied warranty of
|    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
|    GNU General Public License for more details.
|
|    You should have received a copy of the GNU General Public License
|    along with Bento4|GPL; see the file COPYING.  If not, write to the
|    Free Software Foundation, 59 Temple Place - Suite 330, Boston, MA
|    02111-1307, USA.
|
****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Ap4Processor.h"
#include "Ap4AtomSampleTable.h"
#include "Ap4MovieFragment.h"
#include "Ap4FragmentSampleTable.h"
#include "Ap4TfhdAtom.h"
#include "Ap4AtomFactory.h"
#include "Ap4Movie.h"
#include "Ap4Array.h"
#include "Ap4Sample.h"
#include "Ap4TrakAtom.h"
#include "Ap4TfraAtom.h"
#include "Ap4TrunAtom.h"
#include "Ap4TrexAtom.h"
#include "Ap4TkhdAtom.h"
#include "Ap4SidxAtom.h"
#include "Ap4MehdAtom.h"
#include "Ap4MdhdAtom.h"
#include "Ap4TrafAtom.h"
#include "Ap4DataBuffer.h"
#include "Ap4Debug.h"

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
struct AP4_SampleLocator {
    AP4_SampleLocator() : 
        m_TrakIndex(0), 
        m_SampleTable(NULL), 
        m_SampleIndex(0), 
        m_ChunkIndex(0) {}
    AP4_Ordinal          m_TrakIndex;
    AP4_AtomSampleTable* m_SampleTable;
    AP4_Ordinal          m_SampleIndex;
    AP4_Ordinal          m_ChunkIndex;
    AP4_Sample           m_Sample;
};

struct AP4_SampleCursor {
    AP4_SampleCursor() : m_EndReached(false) {}
    AP4_SampleLocator m_Locator;
    bool              m_EndReached;
};

struct AP4_AtomLocator {
	AP4_AtomLocator(AP4_Atom* atom, AP4_UI64 offset) :
        m_Atom(atom),
        m_Offset(offset){}
    AP4_Atom*		m_Atom;
    AP4_UI64		m_Offset;
};

/*----------------------------------------------------------------------
|   AP4_DefaultFragmentHandler
+---------------------------------------------------------------------*/
class AP4_DefaultFragmentHandler: public AP4_Processor::FragmentHandler {
public:
    AP4_DefaultFragmentHandler(AP4_Processor::TrackHandler* track_handler) :
        m_TrackHandler(track_handler) {}
    AP4_Result ProcessSample(AP4_DataBuffer& data_in,
                             AP4_DataBuffer& data_out);
                             
private:
    AP4_Processor::TrackHandler* m_TrackHandler;
};

/*----------------------------------------------------------------------
|   AP4_DefaultFragmentHandler::ProcessSample
+---------------------------------------------------------------------*/
AP4_Result 
AP4_DefaultFragmentHandler::ProcessSample(AP4_DataBuffer& data_in, AP4_DataBuffer& data_out)
{
    if (m_TrackHandler == NULL) {
        data_out.SetData(data_in.GetData(), data_in.GetDataSize());
        return AP4_SUCCESS;
    }
    return m_TrackHandler->ProcessSample(data_in, data_out);
}

/*----------------------------------------------------------------------
|   AP4_Processor::~AP4_Processor
+---------------------------------------------------------------------*/
AP4_Processor::~AP4_Processor() {
	m_ExternalTrackData.DeleteReferences();
	delete m_MoovAtom;
	m_MoovAtom = 0;
}

/*----------------------------------------------------------------------
|   FindFragmentMapEntry
+---------------------------------------------------------------------*/
AP4_UI64 AP4_Processor::FindFragmentMapEntry(AP4_UI64 fragment_offset) {
    int first = 0;
    int last = fragment_map_.ItemCount();
    while (first < last) {
        int middle = (last+first)/2;
        AP4_UI64 middle_value = fragment_map_[middle].before;
        if (fragment_offset < middle_value) {
            last = middle;
        } else if (fragment_offset > middle_value) {
            first = middle+1;
        } else {
            return fragment_map_[middle].after;
        }
    }
    
	return fragment_offset;
}

/*----------------------------------------------------------------------
|   AP4_Processor::ProcessFragments
+---------------------------------------------------------------------*/
AP4_Result
AP4_Processor::ProcessFragment( AP4_ContainerAtom*		    moof,
                                AP4_SidxAtom*			    sidx,
                                AP4_Position			    sidx_position,
                                AP4_ByteStream&			    output,
								AP4_Array<AP4_Position>&    moof_positions,
								AP4_Array<AP4_Position>&    mdat_positions)
{
	unsigned int fragment_index = 0;
    
    //AP4_UI64           mdat_payload_offset = atom_offset+atom->GetSize()+AP4_ATOM_HEADER_SIZE;
    AP4_Sample         sample;
    AP4_DataBuffer     sample_data_in;
    AP4_DataBuffer     sample_data_out;
    AP4_Result         result = AP4_SUCCESS;
        
    // parse the moof
    //AP4_MovieFragment* fragment = new AP4_MovieFragment(moof);

    // process all the traf atoms
    AP4_Array<AP4_Processor::FragmentHandler*> handlers;
    AP4_Array<AP4_FragmentSampleTable*> sample_tables;

	for (; AP4_Atom* child = moof->GetChild(AP4_ATOM_TYPE_TRAF, handlers.ItemCount());) {
		AP4_TrafAtom* traf = AP4_DYNAMIC_CAST(AP4_TrafAtom, child);

		PERTRACK &track_data(m_TrackData[traf->GetInternalTrackId()]);
		AP4_TrakAtom* trak = track_data.track_handler->GetTrakAtom();
		AP4_TrexAtom* trex = track_data.track_handler->GetTrexAtom();

		// create the handler for this traf
		AP4_Processor::FragmentHandler* handler = CreateFragmentHandler(trak, trex, traf,
			*(m_StreamData[track_data.streamId].stream),
			moof_positions[track_data.streamId]);
		if (handler) {
			result = handler->ProcessFragment();
			if (AP4_FAILED(result)) return result;
		}
		handlers.Append(handler);

		// create a sample table object so we can read the sample data
		AP4_FragmentSampleTable* sample_table = new AP4_FragmentSampleTable(
			traf,
			trex,
      traf->GetInternalTrackId(),
			m_StreamData[track_data.streamId].stream,
			moof_positions[traf->GetInternalTrackId()],
			mdat_positions[traf->GetInternalTrackId()],
			0);
		sample_tables.Append(sample_table);

		// let the handler look at the samples before we process them
		if (handler)
			result = handler->PrepareForSamples(sample_table);
		if (AP4_FAILED(result))
			return result;
	}
             
	output.Buffer();
		
	// write the moof
    AP4_UI64 moof_out_start = 0;
    output.Tell(moof_out_start);
    moof->Write(output);
        
    // remember the location of this fragment
	FragmentMapEntry map_entry = { moof_positions[0], moof_out_start };
    fragment_map_.Append(map_entry);

    // write an mdat header
    AP4_Position mdat_out_start;
    AP4_UI64 mdat_size = AP4_ATOM_HEADER_SIZE;
    output.Tell(mdat_out_start);
    output.WriteUI32(0);
    output.WriteUI32(AP4_ATOM_TYPE_MDAT);

    // process all track runs
    for (unsigned int i=0; i<handlers.ItemCount(); i++) {
        AP4_Processor::FragmentHandler* handler = handlers[i];

        // get the track ID
        AP4_ContainerAtom* traf = AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->GetChild(AP4_ATOM_TYPE_TRAF, i));
        if (traf == NULL) continue;
        AP4_TfhdAtom* tfhd = AP4_DYNAMIC_CAST(AP4_TfhdAtom, traf->GetChild(AP4_ATOM_TYPE_TFHD));
            
        // compute the base data offset
        AP4_UI64 base_data_offset;
        if (tfhd->GetFlags() & AP4_TFHD_FLAG_BASE_DATA_OFFSET_PRESENT) {
            base_data_offset = mdat_out_start+AP4_ATOM_HEADER_SIZE;
        } else {
            base_data_offset = moof_out_start;
        }
            
        // build a list of all trun atoms
        AP4_Array<AP4_TrunAtom*> truns;
        for (AP4_List<AP4_Atom>::Item* child_item = traf->GetChildren().FirstItem();
                                        child_item;
                                        child_item = child_item->GetNext()) {
            AP4_Atom* child_atom = child_item->GetData();
            if (child_atom->GetType() == AP4_ATOM_TYPE_TRUN) {
                AP4_TrunAtom* trun = AP4_DYNAMIC_CAST(AP4_TrunAtom, child_atom);
                truns.Append(trun);
            }
        }    
        AP4_Ordinal   trun_index        = 0;
        AP4_Ordinal   trun_sample_index = 0;
        AP4_TrunAtom* trun = truns[0];
        trun->SetDataOffset((AP4_SI32)((mdat_out_start+mdat_size)-base_data_offset));
            
        // write the mdat
        for (unsigned int j=0; j<sample_tables[i]->GetSampleCount(); j++, trun_sample_index++) {
            // advance the trun index if necessary
            if (trun_sample_index >= trun->GetEntries().ItemCount()) {
                trun = truns[++trun_index];
                trun->SetDataOffset((AP4_SI32)((mdat_out_start+mdat_size)-base_data_offset));
                trun_sample_index = 0;
            }
                
            // get the next sample
            result = sample_tables[i]->GetSample(j, sample);
            if (AP4_FAILED(result)) return result;
            sample.ReadData(sample_data_in);
                
            m_TrackData[sample_tables[i]->GetInteralTrackId()].dts = sample.GetDts();
            
            // process the sample data
            if (handler) {
                result = handler->ProcessSample(sample_data_in, sample_data_out);
                if (AP4_FAILED(result)) return result;

                // write the sample data
                result = output.Write(sample_data_out.GetData(), sample_data_out.GetDataSize());
                if (AP4_FAILED(result)) return result;

                // update the mdat size
                mdat_size += sample_data_out.GetDataSize();
                    
                // update the trun entry
                trun->UseEntries()[trun_sample_index].sample_size = sample_data_out.GetDataSize();
            } else {
                // write the sample data (unmodified)
                result = output.Write(sample_data_in.GetData(), sample_data_in.GetDataSize());
                if (AP4_FAILED(result)) return result;

                // update the mdat size
                mdat_size += sample_data_in.GetDataSize();
            }
        }

        if (handler) {
            // update the tfhd header
            if (tfhd->GetFlags() & AP4_TFHD_FLAG_BASE_DATA_OFFSET_PRESENT) {
                tfhd->SetBaseDataOffset(mdat_out_start+AP4_ATOM_HEADER_SIZE);
            }
            if (tfhd->GetFlags() & AP4_TFHD_FLAG_DEFAULT_SAMPLE_SIZE_PRESENT) {
                tfhd->SetDefaultSampleSize(trun->GetEntries()[0].sample_size);
            }
                
            // give the handler a chance to update the atoms
            handler->FinishFragment();
        }
    }

    // update the mdat header
    AP4_Position mdat_out_end;
    output.Tell(mdat_out_end);
#if defined(AP4_DEBUG)
    AP4_ASSERT(mdat_out_end-mdat_out_start == mdat_size);
#endif
	if (AP4_FAILED(result = output.Seek(mdat_out_start)))
		return result;
    output.WriteUI32((AP4_UI32)mdat_size);
    output.Seek(mdat_out_end);
        
    // update the moof if needed
	if (AP4_FAILED(result = output.Seek(moof_out_start)))
		return result;
    moof->Write(output);
    output.Seek(mdat_out_end);
        
    // update the sidx if we have one
    if (sidx && fragment_index < sidx->GetReferences().ItemCount()) {
        if (fragment_index == 0) {
            sidx->SetFirstOffset(moof_out_start-(sidx_position+sidx->GetSize()));
        }
        AP4_LargeSize fragment_size = mdat_out_end-moof_out_start;
        AP4_SidxAtom::Reference& sidx_ref = sidx->UseReferences()[fragment_index];
        sidx_ref.m_ReferencedSize = (AP4_UI32)fragment_size;
    }
        
    // cleanup
    //delete fragment;
        
    for (unsigned int i=0; i<handlers.ItemCount(); i++) {
        delete handlers[i];
    }
    for (unsigned int i=0; i<sample_tables.ItemCount(); i++) {
        delete sample_tables[i];
    }
	if (AP4_FAILED(result = output.Flush()))
		return result;
    
    return AP4_SUCCESS;
}

/*----------------------------------------------------------------------
|   AP4_Processor::CreateFragmentHandler
+---------------------------------------------------------------------*/
AP4_Processor::FragmentHandler* 
AP4_Processor::CreateFragmentHandler(AP4_TrakAtom*      /* trak */,
                                     AP4_TrexAtom*      /* trex */,
                                     AP4_ContainerAtom* traf,
                                     AP4_ByteStream&    /* moof_data   */,
                                     AP4_Position       /* moof_offset */)
{
    // find the matching track handler
    for (unsigned int i=0; i<m_TrackData.ItemCount(); i++) {
        AP4_TfhdAtom* tfhd = AP4_DYNAMIC_CAST(AP4_TfhdAtom, traf->GetChild(AP4_ATOM_TYPE_TFHD));
        if (tfhd && m_TrackData[i].new_id == tfhd->GetTrackId()) {
			return new AP4_DefaultFragmentHandler(m_TrackData[i].track_handler);
        }
    }
    
    return NULL;
}

/*----------------------------------------------------------------------
|   AP4_Processor::Process
+---------------------------------------------------------------------*/
AP4_Result
AP4_Processor::Process(AP4_ByteStream&   input, 
                       AP4_ByteStream&   output,
                       AP4_ByteStream*   fragments,
                       ProgressListener* listener,
                       AP4_AtomFactory&  atom_factory)
{
    // read all atoms.
    // keep all atoms except [mdat]
    // keep a ref to [moov]
    // put [moof] atoms in a separate list
    AP4_AtomParent              top_level;
    AP4_MoovAtom*               moov = NULL;
    AP4_ContainerAtom*          mfra = NULL;
    AP4_SidxAtom*               sidx = NULL;
    AP4_List<AP4_AtomLocator>   frags;
    AP4_UI64                    stream_offset = 0;
    bool                        in_fragments = false;
    unsigned int                sidx_count = 0;
    for (AP4_Atom* atom = NULL;
        AP4_SUCCEEDED(atom_factory.CreateAtomFromStream(input, atom));
        input.Tell(stream_offset)) {
        if (atom->GetType() == AP4_ATOM_TYPE_MDAT) {
            delete atom;
            continue;
        } else if (atom->GetType() == AP4_ATOM_TYPE_MOOV) {
            moov = AP4_DYNAMIC_CAST(AP4_MoovAtom, atom);
            if (fragments) break;
        } else if (atom->GetType() == AP4_ATOM_TYPE_MFRA) {
            mfra = AP4_DYNAMIC_CAST(AP4_ContainerAtom, atom);
            continue;
        } else if (atom->GetType() == AP4_ATOM_TYPE_SIDX) {
            // don't keep the index, it is likely to be invalidated, we will recompute it later
            ++sidx_count;
            if (sidx == NULL) {
                sidx = AP4_DYNAMIC_CAST(AP4_SidxAtom, atom);
            } else {
                delete atom;
                continue;
            }
        } else if (atom->GetType() == AP4_ATOM_TYPE_SSIX) {
            // don't keep the index, it is likely to be invalidated
            delete atom;
            continue;
        } else if (!fragments && (in_fragments || atom->GetType() == AP4_ATOM_TYPE_MOOF)) {
            in_fragments = true;
            frags.Add(new AP4_AtomLocator(atom, stream_offset));
			break;
        }
        top_level.AddChild(atom);
    }

    // check that we have at most one sidx (we can't deal with multi-sidx streams here
    if (sidx_count > 1) {
        top_level.RemoveChild(sidx);
        delete sidx;
        sidx = NULL;
    }
    
    // if we have a fragments stream, get the fragment locators from there
    if (fragments) {
        stream_offset = 0;
        for (AP4_Atom* atom = NULL;
            AP4_SUCCEEDED(atom_factory.CreateAtomFromStream(*fragments, atom));
            fragments->Tell(stream_offset)) {
            if (atom->GetType() == AP4_ATOM_TYPE_MDAT) {
                delete atom;
                continue;
            }
            frags.Add(new AP4_AtomLocator(atom, stream_offset));
        }
    }
    
    // initialize the processor
    AP4_Result result = Initialize(top_level, input);
    if (AP4_FAILED(result)) return result;

    // process the tracks if we have a moov atom
    AP4_Array<AP4_SampleLocator> locators;
    AP4_Cardinal                 track_count       = 0;
    AP4_List<AP4_TrakAtom>*      trak_atoms        = NULL;
    AP4_LargeSize                mdat_payload_size = 0;
    AP4_SampleCursor*            cursors           = NULL;
    if (moov) {
        // build an array of track sample locators
        trak_atoms = &moov->GetTrakAtoms();
        track_count = trak_atoms->ItemCount();
        cursors = new AP4_SampleCursor[track_count];
		m_TrackData.SetItemCount(track_count);
		m_StreamData.SetItemCount(1);
		m_StreamData[0].stream = &input;

		unsigned int index = 0;
        for (AP4_List<AP4_TrakAtom>::Item* item = trak_atoms->FirstItem(); item; item=item->GetNext()) {
            AP4_TrakAtom* trak = item->GetData();

            // find the stsd atom
            AP4_ContainerAtom* stbl = AP4_DYNAMIC_CAST(AP4_ContainerAtom, trak->FindChild("mdia/minf/stbl"));
            if (stbl == NULL) continue;
            
            // see if there's an external data source for this track
            AP4_ByteStream* trak_data_stream = &input;
            for (AP4_List<ExternalTrackData>::Item* ditem = m_ExternalTrackData.FirstItem(); ditem; ditem=ditem->GetNext()) {
                ExternalTrackData* tdata = ditem->GetData();
                if (tdata->m_TrackId == trak->GetId()) {
                    trak_data_stream = tdata->m_MediaData;
                    break;
                }
            }
			AP4_ContainerAtom *mvex = AP4_DYNAMIC_CAST(AP4_ContainerAtom, moov->GetChild(AP4_ATOM_TYPE_MVEX));
			AP4_TrexAtom*      trex = NULL;
			if (mvex) {
				for (AP4_List<AP4_Atom>::Item* item = mvex->GetChildren().FirstItem(); item; item = item->GetNext()) {
					AP4_Atom* atom = item->GetData();
					if (atom->GetType() == AP4_ATOM_TYPE_TREX) {
						trex = AP4_DYNAMIC_CAST(AP4_TrexAtom, atom);
						if (trex && trex->GetTrackId() == trak->GetId()) 
							break;
						trex = NULL;
					}
				}
			}
			// create the track handler    
            m_TrackData[index].track_handler	= CreateTrackHandler(trak, trex);
			m_TrackData[index].new_id = trak->GetId();

			cursors[index].m_Locator.m_TrakIndex   = index;
            cursors[index].m_Locator.m_SampleTable = new AP4_AtomSampleTable(stbl, *trak_data_stream);
            cursors[index].m_Locator.m_SampleIndex = 0;
            cursors[index].m_Locator.m_ChunkIndex  = 0;
            if (cursors[index].m_Locator.m_SampleTable->GetSampleCount()) {
                cursors[index].m_Locator.m_SampleTable->GetSample(0, cursors[index].m_Locator.m_Sample);
            } else {
                cursors[index].m_EndReached = true;
            }

            index++;            
        }

        // figure out the layout of the chunks
        for (;;) {
            // see which is the next sample to write
            AP4_UI64 min_offset = (AP4_UI64)(-1);
            int cursor = -1;
            for (unsigned int i=0; i<track_count; i++) {
                if (!cursors[i].m_EndReached &&
                    cursors[i].m_Locator.m_Sample.GetOffset() <= min_offset) {
                    min_offset = cursors[i].m_Locator.m_Sample.GetOffset();
                    cursor = i;
                }
            }

            // stop if all cursors are exhausted
            if (cursor == -1) break;

            // append this locator to the layout list
            AP4_SampleLocator& locator = cursors[cursor].m_Locator;
            locators.Append(locator);

            // move the cursor to the next sample
            locator.m_SampleIndex++;
            if (locator.m_SampleIndex == locator.m_SampleTable->GetSampleCount()) {
                // mark this track as completed
                cursors[cursor].m_EndReached = true;
            } else {
                // get the next sample info
                locator.m_SampleTable->GetSample(locator.m_SampleIndex, locator.m_Sample);
                AP4_Ordinal skip, sdesc;
                locator.m_SampleTable->GetChunkForSample(locator.m_SampleIndex,
                                                         locator.m_ChunkIndex,
                                                         skip, sdesc);
            }
        }

        // update the stbl atoms and compute the mdat size
        int current_track = -1;
        int current_chunk = -1;
        AP4_Position current_chunk_offset = 0;
        AP4_Size current_chunk_size = 0;
        for (AP4_Ordinal i=0; i<locators.ItemCount(); i++) {
            AP4_SampleLocator& locator = locators[i];
            if ((int)locator.m_TrakIndex  != current_track ||
                (int)locator.m_ChunkIndex != current_chunk) {
                // start a new chunk for this track
                current_chunk_offset += current_chunk_size;
                current_chunk_size = 0;
                current_track = locator.m_TrakIndex;
                current_chunk = locator.m_ChunkIndex;
                locator.m_SampleTable->SetChunkOffset(locator.m_ChunkIndex, current_chunk_offset);
            } 
            AP4_Size sample_size;
            TrackHandler* handler = m_TrackData[locator.m_TrakIndex].track_handler;
            if (handler) {
                sample_size = handler->GetProcessedSampleSize(locator.m_Sample);
                locator.m_SampleTable->SetSampleSize(locator.m_SampleIndex, sample_size);
            } else {
                sample_size = locator.m_Sample.GetSize();
            }
            current_chunk_size += sample_size;
            mdat_payload_size  += sample_size;
        }

        // process the tracks (ex: sample descriptions processing)
        for (AP4_Ordinal i=0; i<track_count; i++) {
            TrackHandler* handler = m_TrackData[i].track_handler;
            if (handler)
				handler->ProcessTrack();
        }
    }

    // finalize the processor
    Finalize(top_level);

    if (!fragments) {
        // calculate the size of all atoms combined
        AP4_UI64 atoms_size = 0;
        top_level.GetChildren().Apply(AP4_AtomSizeAdder(atoms_size));

        // see if we need a 64-bit or 32-bit mdat
        AP4_Size mdat_header_size = AP4_ATOM_HEADER_SIZE;
        if (mdat_payload_size+mdat_header_size > 0xFFFFFFFF) {
            // we need a 64-bit size
            mdat_header_size += 8;
        }
        
        // adjust the chunk offsets
        for (AP4_Ordinal i=0; i<track_count; i++) {
            AP4_TrakAtom* trak;
            trak_atoms->Get(i, trak);
            trak->AdjustChunkOffsets(atoms_size+mdat_header_size);
        }

        // write all atoms
        top_level.GetChildren().Apply(AP4_AtomListWriter(output));

        // write mdat header
        if (mdat_payload_size) {
            if (mdat_header_size == AP4_ATOM_HEADER_SIZE) {
                // 32-bit size
                output.WriteUI32((AP4_UI32)(mdat_header_size+mdat_payload_size));
                output.WriteUI32(AP4_ATOM_TYPE_MDAT);
            } else {
                // 64-bit size
                output.WriteUI32(1);
                output.WriteUI32(AP4_ATOM_TYPE_MDAT);
                output.WriteUI64(mdat_header_size+mdat_payload_size);
            }
        }        
    }
    
    // write the samples
    if (moov) {
        if (!fragments) {
#if defined(AP4_DEBUG)
            AP4_Position before;
            output.Tell(before);
#endif
            AP4_Sample     sample;
            AP4_DataBuffer data_in;
            AP4_DataBuffer data_out;
            for (unsigned int i=0; i<locators.ItemCount(); i++) {
                AP4_SampleLocator& locator = locators[i];
                locator.m_Sample.ReadData(data_in);
                TrackHandler* handler = m_TrackData[locator.m_TrakIndex].track_handler;
                if (handler) {
                    result = handler->ProcessSample(data_in, data_out);
                    if (AP4_FAILED(result)) return result;
                    output.Write(data_out.GetData(), data_out.GetDataSize());
                } else {
                    output.Write(data_in.GetData(), data_in.GetDataSize());            
                }

                // notify the progress listener
                if (listener) {
                    listener->OnProgress(i+1, locators.ItemCount());
                }
            }

#if defined(AP4_DEBUG)
            AP4_Position after;
            output.Tell(after);
            AP4_ASSERT(after-before == mdat_payload_size);
#endif
		}
		else
			m_StreamData[0].stream = fragments;
        
        // find the position of the sidx atom
        AP4_Position sidx_position = 0;
		if (sidx) {
			for (AP4_List<AP4_Atom>::Item* item = top_level.GetChildren().FirstItem();
				item;
				item = item->GetNext()) {
				AP4_Atom* atom = item->GetData();
				if (atom->GetType() == AP4_ATOM_TYPE_SIDX) {
					break;
				}
				sidx_position += atom->GetSize();
			}
		}
        
        // process the fragments, if any
		AP4_Array<AP4_Position> moof_offsets, mdat_offsets;
		moof_offsets.SetItemCount(1);
		mdat_offsets.SetItemCount(1);

		while (frags.ItemCount() > 0)
		{
			for (AP4_List<AP4_AtomLocator>::Item *locator(frags.FirstItem()); locator; locator = locator->GetNext())
			{
				AP4_ContainerAtom *moof(AP4_DYNAMIC_CAST(AP4_ContainerAtom, locator->GetData()->m_Atom));
				moof_offsets[0] = locator->GetData()->m_Offset;
				mdat_offsets[0] = moof_offsets[0] + moof->GetSize() + AP4_ATOM_HEADER_SIZE;

				result = ProcessFragment(moof, sidx, sidx_position, output, moof_offsets, mdat_offsets);
				if (AP4_FAILED(result))
					return result;
			}
			frags.DeleteReferences();

			AP4_Atom* atom = NULL;
			input.Tell(stream_offset);
			if (AP4_SUCCEEDED(atom_factory.CreateAtomFromStream(input, atom)))
			{
				if (atom->GetType() == AP4_ATOM_TYPE_MOOF)
					frags.Add(new AP4_AtomLocator(atom, stream_offset));
				else
					delete atom;
			}
		}

		// update the mfra if we have one
		if (mfra) {
			for (AP4_List<AP4_Atom>::Item* mfra_item = mfra->GetChildren().FirstItem(); mfra_item; mfra_item = mfra_item->GetNext())
			{
				if (mfra_item->GetData()->GetType() != AP4_ATOM_TYPE_TFRA)
					continue;
				AP4_TfraAtom* tfra = AP4_DYNAMIC_CAST(AP4_TfraAtom, mfra_item->GetData());
				if (tfra == NULL)
					continue;
				AP4_Array<AP4_TfraAtom::Entry>& entries = tfra->GetEntries();
				AP4_Cardinal entry_count = entries.ItemCount();
				for (unsigned int i = 0; i<entry_count; i++) {
					entries[i].m_MoofOffset = FindFragmentMapEntry(entries[i].m_MoofOffset);
				}
			}
		}

        // update and re-write the sidx if we have one
        if (sidx && sidx_position) {
            AP4_Position where = 0;
            output.Tell(where);
            output.Seek(sidx_position);
            result = sidx->Write(output);
            if (AP4_FAILED(result)) return result;
            output.Seek(where);
        }
        
        if (!fragments) {
            // write the mfra atom at the end if we have one
            if (mfra) {
                mfra->Write(output);
            }
        }
        
        // cleanup
        for (AP4_Ordinal i=0; i<track_count; i++)
            delete cursors[i].m_Locator.m_SampleTable;
        m_TrackData.Clear();
        delete[] cursors;
    }

    // cleanup
    frags.DeleteReferences();
    delete mfra;
    
    return AP4_SUCCESS;
}

AP4_Result
AP4_Processor::NormalizeTRAF(AP4_ContainerAtom *atom, AP4_UI32 start, AP4_UI32 end, AP4_UI32 &index)
{
	for (; AP4_Atom* child = atom->GetChild(AP4_ATOM_TYPE_TRAF, index);) {
		AP4_TrafAtom* traf = AP4_DYNAMIC_CAST(AP4_TrafAtom, child);
		AP4_TfhdAtom* tfhd = AP4_DYNAMIC_CAST(AP4_TfhdAtom, traf->GetChild(AP4_ATOM_TYPE_TFHD));
		while (start < end && m_TrackData[start].original_id != tfhd->GetTrackId())
			;
		tfhd->SetTrackId(m_TrackData[start].new_id);
		traf->SetInternalTrackId(start);
		++index;
	}
	return AP4_SUCCESS;
}

/*----------------------------------------------------------------------
|   AP4_Processor::MuxStream
+---------------------------------------------------------------------*/
AP4_Result
AP4_Processor::MuxStream(
	AP4_Array<AP4_ByteStream *> &input,
	AP4_ByteStream& output,
	AP4_UI08		partitions,
	AP4_AtomFactory& atom_factory)
{
	AP4_Result		result;
	AP4_UI64		stream_offset = 0;

	if (partitions & 1)
	{
		// read all atoms.
		// keep all atoms except [mdat]
		// keep a ref to [moov]
		// put [moof] atoms in a separate list
		AP4_AtomParent              top_level;
		AP4_Array<AP4_MoovAtom*>	moov;
		AP4_Size					track_count(0);

		for(AP4_Size streamid(0); streamid < input.ItemCount(); ++streamid)
		{
			for (AP4_Atom* atom = NULL; AP4_SUCCEEDED(atom_factory.CreateAtomFromStream(*input[streamid], atom)); input[streamid]->Tell(stream_offset))
			{
				if (atom->GetType() == AP4_ATOM_TYPE_MFRA) {
					delete atom;
					continue;
				}
				else if (atom->GetType() == AP4_ATOM_TYPE_SIDX) {
					delete atom;
					continue;
				}
				else if (atom->GetType() == AP4_ATOM_TYPE_SSIX) {
					delete atom;
					continue;
				}
				if (streamid == 0)
					top_level.AddChild(atom);
				else if (atom->GetType() != AP4_ATOM_TYPE_MOOV)
					delete atom;
				if (atom->GetType() == AP4_ATOM_TYPE_MOOV)
				{
					moov.Append(AP4_DYNAMIC_CAST(AP4_MoovAtom,atom));
					break;
				}
			}
			if (moov.ItemCount() == streamid)
				return AP4_ERROR_INVALID_FORMAT;
			
			while (AP4_SUCCEEDED(moov[streamid]->DeleteChild(AP4_ATOM_TYPE_PSSH, 0)));

			// Remove tracks we cannot handle
			for (AP4_List<AP4_TrakAtom>::Item *item(moov[streamid]->GetTrakAtoms().FirstItem()); item;)
				if (!item->GetData()->FindChild("mdia/minf/stbl"))
					moov[streamid]->GetTrakAtoms().Remove(item);
				else
					item = item->GetNext();
			track_count += moov[streamid]->GetTrakAtoms().ItemCount();
		}

		// initialize the processor
		if (AP4_FAILED(result = Initialize(top_level, *input[0]))) 
			return result;

		// process the tracks if we have a moov atom
		m_TrackData.SetItemCount(track_count);
		m_StreamData.SetItemCount(input.ItemCount());
	
		//NormalizeTREX(mvex, 0, m_TrackCounts[0], m_TrackCounts[1]);
		AP4_Cardinal internal_index(0);
		AP4_ContainerAtom *mvex_base(0);
		AP4_List<AP4_TrakAtom>::Item *item = NULL;

		for (AP4_Size streamid(0); streamid < input.ItemCount(); ++streamid)
		{
			m_StreamData[streamid].trackStart = internal_index;
			m_StreamData[streamid].stream = input[streamid];
			if (streamid)
				moov[0]->AddTrakAtoms(moov[streamid]->GetTrakAtoms(), item);
			else
				item = moov[streamid]->GetTrakAtoms().FirstItem();

			for (; item; item = item->GetNext())
			{
				PERTRACK &track_data(m_TrackData[internal_index]);
				track_data.original_id = item->GetData()->GetId();
				item->GetData()->SetId(track_data.new_id = internal_index + 1);

        if (AP4_MdhdAtom* mdhd = AP4_DYNAMIC_CAST(AP4_MdhdAtom, item->GetData()->FindChild("mdia/mdhd")))
          track_data.timescale = mdhd->GetTimeScale();
        else
          track_data.timescale = 1;
        
        AP4_ContainerAtom *mvex = AP4_DYNAMIC_CAST(AP4_ContainerAtom, moov[streamid]->GetChild(AP4_ATOM_TYPE_MVEX, 0));
				if (!mvex)
					return AP4_ERROR_INVALID_FORMAT;
				
				if (!item->GetData()->GetDuration())
				{
					AP4_MehdAtom *mehd(AP4_DYNAMIC_CAST(AP4_MehdAtom, mvex->GetChild(AP4_ATOM_TYPE_MEHD, 0)));
					item->GetData()->SetDuration(mehd? mehd->GetDuration():0);
				}
				
				AP4_TrexAtom *trex(NULL);
				unsigned int index(0);
				for (; !trex && (trex = AP4_DYNAMIC_CAST(AP4_TrexAtom, mvex->GetChild(AP4_ATOM_TYPE_TREX, index++)));)
					if(trex->GetTrackId() != track_data.original_id)
						trex = NULL;
				if (!trex)
					return AP4_ERROR_INVALID_FORMAT;

				if (mvex_base)
				{
					trex = AP4_DYNAMIC_CAST(AP4_TrexAtom, trex->Clone());
					mvex_base->AddChild(trex);
				}
				else
					mvex_base = mvex;
				trex->SetTrackId(track_data.new_id);

				track_data.track_handler = CreateTrackHandler(item->GetData(), trex);
				track_data.track_handler->ProcessTrack();
				track_data.streamId = streamid;
				++m_StreamData[streamid].trackCount;
				++internal_index;
			}
		}
		// We don't need the other moovs anymore.....
		moov.SetItemCount(1);

		AP4_MvhdAtom *mvhd(AP4_DYNAMIC_CAST(AP4_MvhdAtom, moov[0]->GetChild(AP4_ATOM_TYPE_MVHD, 0)));
		if (!mvhd->GetDuration())
		{
			AP4_MehdAtom *mehd(AP4_DYNAMIC_CAST(AP4_MehdAtom, mvex_base->GetChild(AP4_ATOM_TYPE_MEHD, 0)));
			mvhd->SetDuration(mehd ? mehd->GetDuration() : 0);
		}

		// finalize the processor
		Finalize(top_level);

		// calculate the size of all atoms combined
		AP4_UI64 atoms_size = 0;
		top_level.GetChildren().Apply(AP4_AtomSizeAdder(atoms_size));

		// write all atoms
		top_level.GetChildren().Apply(AP4_AtomListWriter(output));
		m_MoovAtom = moov[0];
		m_MoovAtom->Detach();
	}

	if (partitions & 2)
	{
		// process the fragments, if any
		result = AP4_SUCCESS;
		AP4_Array<AP4_UI64> moof_positions, mdat_positions;
		moof_positions.SetItemCount(input.ItemCount());
		mdat_positions.SetItemCount(input.ItemCount());

		for (;;)
		{
			AP4_ContainerAtom *moof = NULL;
			AP4_UI32 track_index(0);
			
#if 0			
      for (AP4_Cardinal streamid(0); streamid < input.ItemCount(); ++streamid)
			{
				AP4_Atom* atom = NULL;
				if (AP4_SUCCEEDED(input[streamid]->Tell(stream_offset)) && AP4_SUCCEEDED(atom_factory.CreateAtomFromStream(*input[streamid], atom)))
				{
					if (atom->GetType() != AP4_ATOM_TYPE_MOOF)
						return AP4_ERROR_INVALID_FORMAT;
					
					moof_positions[streamid] = stream_offset;
					mdat_positions[streamid] = stream_offset + atom->GetSize() + +AP4_ATOM_HEADER_SIZE;

					if (moof)
					{
						int index(0);
						for (; AP4_Atom* child = AP4_DYNAMIC_CAST(AP4_ContainerAtom, atom)->GetChild(AP4_ATOM_TYPE_TRAF, index++);)
							moof->AddChild(child->Clone());
						delete atom;
					}
					else
						moof = AP4_DYNAMIC_CAST(AP4_ContainerAtom, atom);
					NormalizeTRAF(AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof), m_StreamData[streamid].trackStart,
						m_StreamData[streamid].trackStart + m_StreamData[streamid].trackCount, track_index);
				}
				else
					delete atom;
			}
#else
      double mindts(9999999999.0);
      AP4_Cardinal nextStream(~0);
      for (AP4_Cardinal track(0); track < m_TrackData.ItemCount(); ++track)
        if ((double)m_TrackData[track].dts / m_TrackData[track].timescale  < mindts)
        {
          mindts = (double)m_TrackData[track].dts / m_TrackData[track].timescale;
          nextStream = m_TrackData[track].streamId;
        }
      
      AP4_Atom* atom = NULL;
      if (AP4_SUCCEEDED(result = input[nextStream]->Tell(stream_offset)) && AP4_SUCCEEDED(result = atom_factory.CreateAtomFromStream(*input[nextStream], atom)))
      {
        if (atom->GetType() != AP4_ATOM_TYPE_MOOF)
          return AP4_ERROR_INVALID_FORMAT;
			} else if (atom)
				return result;

      moof_positions[nextStream] = stream_offset;
      mdat_positions[nextStream] = stream_offset + atom->GetSize() + +AP4_ATOM_HEADER_SIZE;

      moof = AP4_DYNAMIC_CAST(AP4_ContainerAtom, atom);
      NormalizeTRAF(AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof), m_StreamData[nextStream].trackStart,
        m_StreamData[nextStream].trackStart + m_StreamData[nextStream].trackCount, track_index);
#endif
			if (!moof)
				break;
			
			if (AP4_FAILED(result = ProcessFragment(moof, NULL, 0, output, moof_positions, mdat_positions)))
				return result;
			
			delete moof;
			moof = NULL;
		}

		// cleanup
		m_TrackData.Clear();
		m_StreamData.Clear();
	}

	return AP4_SUCCESS;
}


AP4_Result
AP4_Processor::Mux(AP4_Array<AP4_ByteStream *> &input,
					AP4_ByteStream&   output,
					AP4_UI08		partitions,
					ProgressListener* listener,
					AP4_AtomFactory&  atom_factory)
{
	return MuxStream(input, output, partitions, atom_factory);
}

/*----------------------------------------------------------------------
|   AP4_Processor::Process
+---------------------------------------------------------------------*/
AP4_Result
AP4_Processor::Process(AP4_ByteStream&   input, 
                       AP4_ByteStream&   output,
                       ProgressListener* listener,
                       AP4_AtomFactory&  atom_factory)
{
    return Process(input, output, NULL, listener, atom_factory);
}

/*----------------------------------------------------------------------
|   AP4_Processor::Process
+---------------------------------------------------------------------*/
AP4_Result
AP4_Processor::Process(AP4_ByteStream&   fragments, 
                       AP4_ByteStream&   output,
                       AP4_ByteStream&   init,
                       ProgressListener* listener,
                       AP4_AtomFactory&  atom_factory)
{
    return Process(init, output, &fragments, listener, atom_factory);
}

/*----------------------------------------------------------------------
|   AP4_Processor:Initialize
+---------------------------------------------------------------------*/
AP4_Result 
AP4_Processor::Initialize(AP4_AtomParent&   /* top_level */,
                          AP4_ByteStream&   /* stream    */,
                          ProgressListener* /* listener  */)
{
    // default implementation: do nothing
	fragment_map_.Clear();
	m_TrackData.Clear();
	m_StreamData.Clear();

	delete m_MoovAtom;
	m_MoovAtom = 0;

	return AP4_SUCCESS;
}

/*----------------------------------------------------------------------
|   AP4_Processor:Finalize
+---------------------------------------------------------------------*/
AP4_Result 
AP4_Processor::Finalize(AP4_AtomParent&   /* top_level */,
                        ProgressListener* /* listener */ )
{
    // default implementation: do nothing
    return AP4_SUCCESS;
}

/*----------------------------------------------------------------------
|   AP4_Processor::TrackHandler Dynamic Cast Anchor
+---------------------------------------------------------------------*/
AP4_DEFINE_DYNAMIC_CAST_ANCHOR_S(AP4_Processor::TrackHandler, TrackHandler)
